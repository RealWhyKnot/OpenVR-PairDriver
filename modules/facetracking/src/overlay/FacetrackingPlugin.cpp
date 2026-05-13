#include "FacetrackingPlugin.h"

#include "AdvancedTab.h"
#include "CalibrationTab.h"
#include "IPCClient.h"
#include "Logging.h"
#include "LogsSection.h"
#include "ModuleSources.h"
#include "ModulesTab.h"
#include "Profiles.h"
#include "SettingsTab.h"
#include "ShellFooter.h"
#include "UiHelpers.h"
#include "BuildStamp.h"

#include <imgui/imgui.h>

#include "picojson.h"

#include <chrono>
#include <cstring>
#include <ctime>
#include <exception>
#include <memory>
#include <string>

using Clock = std::chrono::steady_clock;

FacetrackingPlugin::FacetrackingPlugin()
{
    observed_ipc_generation_ = ipc_.ConnectionGeneration();
}

void FacetrackingPlugin::OnStart(openvr_pair::overlay::ShellContext &)
{
    FtOpenLogFile();
    FT_LOG_OVL("FaceTracking overlay plugin starting (build %s channel=%s)",
        FACETRACKING_BUILD_STAMP, FACETRACKING_BUILD_CHANNEL);

    profile_.Load();

    // Seed sources.json on first run; load the catalogue for Tick() to use.
    sources_catalogue_ = facetracking::EnsureSourcesCatalogue();

    // Enqueue auto-updates for GitHub sources not checked in the last 6 hours.
    {
        const auto now = std::chrono::system_clock::now();
        constexpr auto kAutoUpdateInterval = std::chrono::hours(6);

        for (auto &src : sources_catalogue_.sources) {
            if (src.kind != facetracking::SourceKind::GitHub || !src.auto_update)
                continue;

            bool needsCheck = src.last_checked_at.empty();
            if (!needsCheck) {
                // Parse "YYYY-MM-DDTHH:MM:SSZ".
                struct tm t{};
                if (sscanf(src.last_checked_at.c_str(),
                           "%d-%d-%dT%d:%d:%dZ",
                           &t.tm_year, &t.tm_mon, &t.tm_mday,
                           &t.tm_hour, &t.tm_min, &t.tm_sec) == 6) {
                    t.tm_year -= 1900;
                    t.tm_mon  -= 1;
                    time_t last = _mkgmtime(&t);
                    if (last != (time_t)-1) {
                        auto lastTp = std::chrono::system_clock::from_time_t(last);
                        needsCheck = (now - lastTp) >= kAutoUpdateInterval;
                    } else {
                        needsCheck = true;
                    }
                } else {
                    needsCheck = true;
                }
            }

            if (needsCheck) {
                FT_LOG_OVL("[modules] queuing auto-update for github source '%s'",
                            src.label.c_str());
                picojson::object o;
                o["id"]               = picojson::value(src.id);
                o["kind"]             = picojson::value(std::string("github"));
                o["owner_repo"]       = picojson::value(src.owner_repo);
                o["label"]            = picojson::value(src.label);
                o["auto_update"]      = picojson::value(src.auto_update);
                o["last_release_tag"] = picojson::value(src.last_release_tag);
                std::string data = picojson::value(o).serialize();
                sync_runner_.StartUpdate(src.id, data);
            }
        }
    }

    try {
        ipc_.Connect();
        FT_LOG_OVL("[ipc] connected on startup");
        PushConfigToDriver();
    } catch (const std::exception &e) {
        FT_LOG_OVL("[ipc] initial connect failed: %s", e.what());
        last_error_ = std::string("FaceTracking IPC: ") + e.what();
    }

    const auto now = Clock::now();
    last_connection_check_ = now;
    last_save_             = now;
}

void FacetrackingPlugin::OnShutdown(openvr_pair::overlay::ShellContext &)
{
    profile_.Save();
    ipc_.Close();
    FT_LOG_OVL("FaceTracking overlay plugin shutting down");
    FtLogFlush();
}

void FacetrackingPlugin::Tick(openvr_pair::overlay::ShellContext &)
{
    const auto now = Clock::now();

    if (now - last_connection_check_ >= std::chrono::seconds(1)) {
        MaintainDriverConnection();
        last_connection_check_ = now;
    }

    // Pull the latest host_status.json snapshot. The poller throttles itself
    // to a stat() every 500 ms and only re-reads on mtime change, so calling
    // this every frame is cheap.
    host_status_.Tick();

    // Pull the latest driver_telemetry.json snapshot (same cadence).
    driver_telemetry_.Tick();

    // Reap any completed background sync.  On success, stamp last_checked_at.
    if (auto res = sync_runner_.Poll()) {
        if (res->ok && !res->installed_uuid.empty()) {
            FT_LOG_OVL("[modules] background sync completed: uuid='%s' ver='%s'",
                res->installed_uuid.c_str(), res->installed_version.c_str());
        }
        // Refresh catalogue and timestamp all github sources that were running.
        sources_catalogue_ = facetracking::EnsureSourcesCatalogue();
    }

    // Periodic auto-save (every 60 s).
    if (now - last_save_ >= std::chrono::seconds(60)) {
        profile_.Save();
        last_save_ = now;
    }
}

void FacetrackingPlugin::PushConfigToDriver()
{
    if (!ipc_.IsConnected()) {
        last_error_ = "Not connected to the FaceTracking driver. Is SteamVR running?";
        return;
    }
    try {
        protocol::Request req(protocol::RequestSetFaceTrackingConfig);
        auto &cfg = req.setFaceTrackingConfig;
        const auto &p = profile_.current;

        cfg.master_enabled             = 1;
        cfg.eyelid_sync_enabled        = p.eyelid_sync_enabled        ? 1 : 0;
        cfg.eyelid_sync_preserve_winks = p.eyelid_sync_preserve_winks ? 1 : 0;
        cfg.vergence_lock_enabled      = p.vergence_lock_enabled       ? 1 : 0;
        cfg.continuous_calib_mode      = static_cast<uint8_t>(p.continuous_calib_mode);
        cfg.output_osc_enabled         = p.output_osc_enabled          ? 1 : 0;
        cfg._reserved_native           = 0;
        cfg._reserved1                 = 0;
        cfg.eyelid_sync_strength       = static_cast<uint8_t>(p.eyelid_sync_strength);
        cfg.vergence_lock_strength     = static_cast<uint8_t>(p.vergence_lock_strength);
        cfg.gaze_smoothing             = static_cast<uint8_t>(p.gaze_smoothing);
        cfg.openness_smoothing         = static_cast<uint8_t>(p.openness_smoothing);
        cfg.osc_port                   = static_cast<uint16_t>(p.osc_port);
        cfg._reserved2                 = 0;

        std::strncpy(cfg.osc_host, p.osc_host.c_str(),
            sizeof(cfg.osc_host) - 1);
        cfg.osc_host[sizeof(cfg.osc_host) - 1] = '\0';

        std::strncpy(cfg.active_module_uuid, p.active_module_uuid.c_str(),
            sizeof(cfg.active_module_uuid) - 1);
        cfg.active_module_uuid[sizeof(cfg.active_module_uuid) - 1] = '\0';

        auto resp = ipc_.SendBlocking(req);
        if (resp.type != protocol::ResponseSuccess) {
            last_error_ = "Driver rejected SetFaceTrackingConfig (type=" +
                          std::to_string(resp.type) + ")";
            FT_LOG_OVL("[ipc] driver rejected config push: type=%d", (int)resp.type);
            return;
        }
        last_error_.clear();
        FT_LOG_OVL("[ipc] config pushed: osc=%d eyelid=%d vergence=%d calib_mode=%d",
            (int)cfg.output_osc_enabled, (int)cfg.eyelid_sync_enabled,
            (int)cfg.vergence_lock_enabled, (int)cfg.continuous_calib_mode);
    } catch (const std::exception &e) {
        last_error_ = std::string("IPC error: ") + e.what();
        FT_LOG_OVL("[ipc] PushConfigToDriver failed: %s", e.what());
    }
}

void FacetrackingPlugin::SendCalibrationCommand(protocol::FaceCalibrationOp op)
{
    if (!ipc_.IsConnected()) {
        last_error_ = "Not connected to the FaceTracking driver.";
        return;
    }
    try {
        protocol::Request req(protocol::RequestSetFaceCalibrationCommand);
        req.setFaceCalibrationCommand.op = static_cast<uint8_t>(op);
        std::memset(req.setFaceCalibrationCommand._reserved, 0,
            sizeof(req.setFaceCalibrationCommand._reserved));
        auto resp = ipc_.SendBlocking(req);
        if (resp.type != protocol::ResponseSuccess) {
            last_error_ = "Driver rejected calibration command (op=" +
                          std::to_string((int)op) + " type=" +
                          std::to_string(resp.type) + ")";
            FT_LOG_OVL("[ipc] driver rejected calib command op=%d: type=%d",
                (int)op, (int)resp.type);
            return;
        }
        last_error_.clear();
        FT_LOG_OVL("[ipc] calibration command sent: op=%d", (int)op);
    } catch (const std::exception &e) {
        last_error_ = std::string("IPC error: ") + e.what();
        FT_LOG_OVL("[ipc] SendCalibrationCommand(op=%d) failed: %s", (int)op, e.what());
    }
}

void FacetrackingPlugin::SendActiveModule(const std::string &uuid)
{
    profile_.current.active_module_uuid = uuid;
    profile_.Save();

    if (!ipc_.IsConnected()) {
        // Config will be pushed on the next successful heartbeat reconnect.
        return;
    }
    try {
        protocol::Request req(protocol::RequestSetFaceActiveModule);
        std::strncpy(req.setFaceActiveModule.uuid, uuid.c_str(),
            sizeof(req.setFaceActiveModule.uuid) - 1);
        req.setFaceActiveModule.uuid[sizeof(req.setFaceActiveModule.uuid) - 1] = '\0';
        std::memset(req.setFaceActiveModule._reserved, 0,
            sizeof(req.setFaceActiveModule._reserved));
        auto resp = ipc_.SendBlocking(req);
        if (resp.type != protocol::ResponseSuccess) {
            last_error_ = "Driver rejected SetFaceActiveModule (type=" +
                          std::to_string(resp.type) + ")";
            FT_LOG_OVL("[ipc] driver rejected active-module set: type=%d", (int)resp.type);
            return;
        }
        last_error_.clear();
        FT_LOG_OVL("[ipc] active module set: uuid='%s'", uuid.c_str());
    } catch (const std::exception &e) {
        last_error_ = std::string("IPC error: ") + e.what();
        FT_LOG_OVL("[ipc] SendActiveModule failed: %s", e.what());
    }
}

void FacetrackingPlugin::MaintainDriverConnection()
{
    try {
        if (!ipc_.IsConnected()) {
            ipc_.Connect();
            FT_LOG_OVL("[ipc] connected from heartbeat");
        }

        auto resp = ipc_.SendBlocking(protocol::Request(protocol::RequestHandshake));
        if (resp.type != protocol::ResponseHandshake ||
            resp.protocol.version != protocol::Version) {
            last_error_ = "FaceTracking driver protocol mismatch during heartbeat";
            FT_LOG_OVL("[ipc] heartbeat mismatch: type=%d driverVer=%u overlayVer=%u",
                (int)resp.type, resp.protocol.version, protocol::Version);
            return;
        }

        const uint64_t gen = ipc_.ConnectionGeneration();
        if (gen != observed_ipc_generation_) {
            observed_ipc_generation_ = gen;
            FT_LOG_OVL("[ipc] generation changed to %llu; re-sending config",
                (unsigned long long)gen);
            PushConfigToDriver();
        }

        // Clear stale connection-error banners.
        if (last_error_.find("FaceTracking IPC") == 0 ||
            last_error_.find("Not connected")    == 0 ||
            last_error_.find("Driver connection:") == 0) {
            last_error_.clear();
        }
    } catch (const std::exception &e) {
        last_error_ = std::string("Driver connection: ") + e.what();
        FT_LOG_OVL("[ipc] heartbeat failed: %s", e.what());
        ipc_.Close();
    }
}

void FacetrackingPlugin::DrawStatusBanner()
{
    if (!last_error_.empty()) {
        openvr_pair::overlay::ui::DrawErrorBanner(
            "Face Tracking driver problem", last_error_.c_str());
    }
}

void FacetrackingPlugin::DrawTab(openvr_pair::overlay::ShellContext &)
{
    DrawStatusBanner();

    if (ImGui::BeginTabBar("ft_tabs")) {
        if (ImGui::BeginTabItem("Settings"))    {
            facetracking::ui::DrawSettingsTab(*this);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Calibration")) {
            facetracking::ui::DrawCalibrationTab(*this);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Modules"))     {
            facetracking::ui::DrawModulesTab(*this);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Advanced"))    {
            facetracking::ui::DrawAdvancedTab(*this);
            ImGui::EndTabItem();
        }
        // Logs appear in the umbrella's global Logs tab via DrawLogsSection.
        ImGui::EndTabBar();
    }

    openvr_pair::overlay::ShellFooterStatus footer;
    footer.driverConnected = ipc_.IsConnected();
    footer.driverLabel     = "FaceTracking driver";
    footer.buildStamp      = FACETRACKING_BUILD_STAMP;
    openvr_pair::overlay::DrawShellFooter(footer);
}

void FacetrackingPlugin::DrawLogsSection(openvr_pair::overlay::ShellContext &)
{
    facetracking::ui::DrawLogsSection(*this);
}

namespace openvr_pair::overlay {

std::unique_ptr<FeaturePlugin> CreateFaceTrackingPlugin()
{
    return std::make_unique<FacetrackingPlugin>();
}

} // namespace openvr_pair::overlay
