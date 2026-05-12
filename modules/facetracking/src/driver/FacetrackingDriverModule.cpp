#define _CRT_SECURE_NO_DEPRECATE
#include "CalibrationEngine.h"
#include "EyelidSync.h"
#include "FaceFrameReader.h"
#include "FaceTrackingDevice.h"
#include "HostSupervisor.h"
#include "Logging.h"
#include "VergenceLock.h"

#include "DriverModule.h"
#include "FeatureFlags.h"
#include "Protocol.h"
#include "ServerTrackedDeviceProvider.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <openvr_driver.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace facetracking {
namespace {

// Resolve the path to the C# host relative to the driver resources directory.
// SteamVR exposes the driver path via IVRProperties on the driver context; we
// fall back to a search-order heuristic if the property isn't available yet.
std::string ResolveHostExePath(vr::IVRDriverContext *driverContext)
{
    // Attempt to get the install directory from SteamVR.
    char buf[MAX_PATH] = {};
    vr::ETrackedPropertyError err = vr::TrackedProp_Success;
    vr::CVRPropertyHelpers *props = vr::VRProperties();
    if (props) {
        vr::PropertyContainerHandle_t systemContainer =
            props->TrackedDeviceToPropertyContainer(vr::k_unTrackedDeviceIndex_Hmd);
        (void)systemContainer; // unused if the driver context isn't initialised yet
    }

    // Use the driver's own module path as the anchor.
    // GetModuleFileNameA on our DLL gives us the driver DLL path; strip to the
    // containing directory and navigate to resources/facetracking/host/.
    HMODULE hSelf = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&ResolveHostExePath),
        &hSelf);

    if (hSelf) {
        char dllPath[MAX_PATH] = {};
        GetModuleFileNameA(hSelf, dllPath, MAX_PATH);
        std::string path(dllPath);
        // Walk up to the driver root (bin/win64/driver_openvrpair.dll -> ../..)
        for (int up = 0; up < 2; ++up) {
            auto sep = path.find_last_of("/\\");
            if (sep == std::string::npos) break;
            path = path.substr(0, sep);
        }
        path += "\\resources\\facetracking\\host\\OpenVRPair.FaceModuleHost.exe";
        return path;
    }
    return {};
}

class FacetrackingDriverModule final : public DriverModule
{
public:
    const char *Name()        const override { return "FaceTracking"; }
    uint32_t    FeatureMask() const override { return pairdriver::kFeatureFaceTracking; }
    const char *PipeName()    const override { return OPENVR_PAIRDRIVER_FACETRACKING_PIPE_NAME; }

    bool Init(DriverModuleContext &context) override
    {
        FtDrvOpenLogFile();
        FT_LOG_DRV("[module] Init()", 0);

        provider_       = context.provider;
        driver_context_ = context.driverContext;

        // Create the shmem ring through the reader so we map the segment
        // exactly once. The driver owns the lifecycle; the C# host opens
        // the existing segment via MemoryMappedFile.OpenExisting. The prior
        // form did shmem_.Create() AND reader_.Open() on the same name,
        // producing two independent mappings of the same backing store and
        // leaking a HANDLE pair on shutdown.
        if (!reader_.Create(OPENVR_PAIRDRIVER_FACETRACKING_SHMEM_NAME)) {
            FT_LOG_DRV("[module] failed to create shmem segment '%s'",
                OPENVR_PAIRDRIVER_FACETRACKING_SHMEM_NAME);
            return false;
        }
        FT_LOG_DRV("[module] shmem segment created", 0);

        // Register the tracking device.
        device_ = std::make_unique<FaceTrackingDevice>();
        vr::VRServerDriverHost()->TrackedDeviceAdded(
            "OpenVRPair-FaceTracking-Sink",
            vr::TrackedDeviceClass_GenericTracker,
            device_.get());

        // Build host path and start supervisor.
        std::string host_path = ResolveHostExePath(driver_context_);
        FT_LOG_DRV("[module] host exe: %s", host_path.c_str());
        supervisor_ = std::make_unique<HostSupervisor>(host_path);
        supervisor_->Start();

        // Start the frame worker thread.
        worker_stop_.store(false, std::memory_order_release);
        worker_ = std::thread([this]{ WorkerLoop(); });

        FT_LOG_DRV("[module] Init complete", 0);
        return true;
    }

    void Shutdown() override
    {
        FT_LOG_DRV("[module] Shutdown()", 0);

        worker_stop_.store(true, std::memory_order_release);
        if (worker_.joinable()) worker_.join();

        if (supervisor_) supervisor_->Stop();
        supervisor_.reset();

        calib_.Save();
        reader_.Close();
        device_.reset();

        provider_       = nullptr;
        driver_context_ = nullptr;

        FT_LOG_DRV("[module] shutdown complete", 0);
    }

    bool HandleRequest(const protocol::Request &req, protocol::Response &resp) override
    {
        switch (req.type) {
        case protocol::RequestSetFaceTrackingConfig: {
            std::lock_guard<std::mutex> lk(config_mutex_);
            config_ = req.setFaceTrackingConfig;
            // Forward active module selection to supervisor.
            if (supervisor_ && config_.active_module_uuid[0] != '\0') {
                supervisor_->SetActiveModuleUuid(config_.active_module_uuid);
            }
            resp.type = protocol::ResponseSuccess;
            return true;
        }
        case protocol::RequestSetFaceCalibrationCommand: {
            const protocol::FaceCalibrationOp op =
                (protocol::FaceCalibrationOp)req.setFaceCalibrationCommand.op;
            std::lock_guard<std::mutex> lk(config_mutex_);
            calib_.Reset(op);
            resp.type = protocol::ResponseSuccess;
            return true;
        }
        case protocol::RequestSetFaceActiveModule: {
            if (supervisor_) {
                supervisor_->SetActiveModuleUuid(req.setFaceActiveModule.uuid);
            }
            resp.type = protocol::ResponseSuccess;
            return true;
        }
        default:
            return false;
        }
    }

private:
    ServerTrackedDeviceProvider *provider_       = nullptr;
    vr::IVRDriverContext        *driver_context_ = nullptr;

    FaceFrameReader                  reader_;

    std::unique_ptr<FaceTrackingDevice> device_;
    std::unique_ptr<HostSupervisor>     supervisor_;

    CalibrationEngine calib_;
    VergenceLock      vergence_;
    EyelidSync        eyelid_;

    // Config cache -- written by HandleRequest, read by WorkerLoop.
    protocol::FaceTrackingConfig config_{};
    mutable std::mutex           config_mutex_;

    std::atomic<bool> worker_stop_{ false };
    std::thread       worker_;

    // -----------------------------------------------------------------------
    // Worker thread: polls shmem, runs the filter pipeline, publishes.
    // -----------------------------------------------------------------------
    void WorkerLoop()
    {
        FT_LOG_DRV("[worker] started", 0);

        uint64_t last_idx = 0;

        while (!worker_stop_.load(std::memory_order_acquire)) {
            uint64_t idx = reader_.LastPublishIndex();
            if (idx == last_idx) {
                // No new frame; sleep briefly so we don't busy-spin.
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            last_idx = idx;

            protocol::FaceTrackingFrameBody frame{};
            if (!reader_.TryRead(frame)) continue;

            // Snapshot config under lock.
            protocol::FaceTrackingConfig cfg;
            {
                std::lock_guard<std::mutex> lk(config_mutex_);
                cfg = config_;
            }

            if (!cfg.master_enabled) continue;

            // Continuous calibration ingestion + normalization.
            if (cfg.continuous_calib_mode > 0) {
                calib_.IngestFrame(frame);
                calib_.Normalize(frame);
            }

            // Vergence lock.
            if (cfg.vergence_lock_enabled) {
                vergence_.Apply(frame, cfg.vergence_lock_strength);
            }

            // Eyelid sync.
            if (cfg.eyelid_sync_enabled) {
                eyelid_.Apply(frame,
                    cfg.eyelid_sync_strength,
                    cfg.eyelid_sync_preserve_winks != 0);
            }

            // Publish to SteamVR inputs.
            if (cfg.output_native_enabled && device_) {
                device_->PublishFrame(frame);
            }
        }

        FT_LOG_DRV("[worker] stopped", 0);
    }
};

} // namespace

std::unique_ptr<DriverModule> CreateDriverModule()
{
    return std::make_unique<FacetrackingDriverModule>();
}

} // namespace facetracking
