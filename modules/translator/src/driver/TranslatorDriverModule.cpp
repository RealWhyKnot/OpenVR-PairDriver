#define _CRT_SECURE_NO_DEPRECATE
#include "HostSupervisor.h"
#include "Logging.h"

#include "DriverModule.h"
#include "FeatureFlags.h"
#include "Protocol.h"
#include "ServerTrackedDeviceProvider.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#include <openvr_driver.h>

#include <cstring>
#include <memory>
#include <mutex>
#include <string>

// Named pipe for translator host control messages (driver -> host).
// Wire format: NUL-terminated ASCII command strings, one per write.
#define TRANSLATOR_HOST_CONTROL_PIPE_NAME "\\\\.\\pipe\\WKOpenVR-Translator.host"

namespace translator {
namespace {

// Resolve the translator host exe path relative to the driver DLL.
// driver_wkopenvr.dll lives at <root>\bin\win64\; the host is at
// <root>\resources\translator\host\WKOpenVR.TranslatorHost.exe.
std::string ResolveHostExePath()
{
    HMODULE hSelf = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&ResolveHostExePath),
        &hSelf);

    if (!hSelf) return {};

    char dllPath[MAX_PATH] = {};
    GetModuleFileNameA(hSelf, dllPath, MAX_PATH);
    std::string path(dllPath);
    // DLL lives at <root>/bin/win64/driver_wkopenvr.dll. Pop the filename,
    // then "win64", then "bin" -- three pops -- to reach <root>, then
    // append resources/translator/host/. The original two-pop landed at
    // <root>/bin and produced a phantom <root>/bin/resources/... path
    // that does not exist; CreateProcessW returned err=3 PATH_NOT_FOUND.
    // Same bug class the facetracking driver had; fixed there in 68fd11d.
    for (int up = 0; up < 3; ++up) {
        auto sep = path.find_last_of("/\\");
        if (sep == std::string::npos) break;
        path = path.substr(0, sep);
    }
    path += "\\resources\\translator\\host\\WKOpenVR.TranslatorHost.exe";
    return path;
}

// Push a config command over the host control pipe. Returns true on success.
bool SendHostCommand(const std::string &cmd)
{
    HANDLE h = CreateFileA(
        TRANSLATOR_HOST_CONTROL_PIPE_NAME,
        GENERIC_WRITE, 0, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    BOOL ok = WriteFile(h, cmd.data(), (DWORD)cmd.size(), &written, nullptr);
    CloseHandle(h);
    return ok && written == (DWORD)cmd.size();
}

class TranslatorDriverModule final : public DriverModule
{
public:
    const char *Name()        const override { return "Translator"; }
    uint32_t    FeatureMask() const override { return pairdriver::kFeatureTranslator; }
    const char *PipeName()    const override { return OPENVR_PAIRDRIVER_TRANSLATOR_PIPE_NAME; }

    bool Init(DriverModuleContext &) override
    {
        TrDrvOpenLogFile();
        TR_LOG_DRV("[module] Init()");

        std::string host_path = ResolveHostExePath();
        TR_LOG_DRV("[module] host exe: %s", host_path.c_str());

        supervisor_ = std::make_unique<HostSupervisor>(host_path);
        supervisor_->Start();

        TR_LOG_DRV("[module] Init complete");
        return true;
    }

    void Shutdown() override
    {
        TR_LOG_DRV("[module] Shutdown()");
        if (supervisor_) supervisor_->Stop();
        supervisor_.reset();
        TR_LOG_DRV("[module] shutdown complete");
    }

    bool HandleRequest(const protocol::Request &req, protocol::Response &resp) override
    {
        switch (req.type) {
        case protocol::RequestSetTranslatorConfig: {
            std::lock_guard<std::mutex> lk(config_mutex_);
            config_ = req.setTranslatorConfig;
            // Forward relevant settings to the host via the control pipe.
            // The host re-reads its config via a simple tagged line protocol.
            std::string cmd = std::string("config:") +
                "src=" + config_.source_lang +
                ",tgt=" + config_.target_lang +
                ",mode=" + std::to_string((int)config_.mode) +
                ",addr=" + config_.chatbox_address +
                ",port=" + std::to_string((int)config_.chatbox_port) +
                ",log=" + std::to_string((int)config_.transcript_logging) +
                "\n";
            if (!SendHostCommand(cmd)) {
                TR_LOG_DRV("[module] failed to forward config to host (pipe not up yet)");
            }
            resp.type = protocol::ResponseSuccess;
            return true;
        }
        case protocol::RequestTranslatorRestartHost: {
            TR_LOG_DRV("[module] host restart requested by overlay");
            if (supervisor_) supervisor_->Restart();
            resp.type = protocol::ResponseSuccess;
            return true;
        }
        default:
            return false;
        }
    }

private:
    std::unique_ptr<HostSupervisor> supervisor_;

    protocol::TranslatorConfig config_{};
    mutable std::mutex         config_mutex_;
};

} // namespace

std::unique_ptr<DriverModule> CreateDriverModule()
{
    return std::make_unique<TranslatorDriverModule>();
}

} // namespace translator
