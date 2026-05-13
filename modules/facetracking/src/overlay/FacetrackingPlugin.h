#pragma once

#include "FeaturePlugin.h"
#include "HostStatusPoller.h"
#include "IPCClient.h"
#include "Profiles.h"
#include "Protocol.h"

#include <chrono>
#include <cstdint>
#include <string>

class FacetrackingPlugin;

namespace facetracking::ui {
void DrawSettingsTab(FacetrackingPlugin &plugin);
void DrawCalibrationTab(FacetrackingPlugin &plugin);
void DrawModulesTab(FacetrackingPlugin &plugin);
void DrawAdvancedTab(FacetrackingPlugin &plugin);
void DrawLogsSection(FacetrackingPlugin &plugin);
} // namespace facetracking::ui

class FacetrackingPlugin final : public openvr_pair::overlay::FeaturePlugin
{
public:
    FacetrackingPlugin();

    const char *Name()         const override { return "Face Tracking"; }
    const char *FlagFileName() const override { return "enable_facetracking.flag"; }
    const char *PipeName()     const override { return OPENVR_PAIRDRIVER_FACETRACKING_PIPE_NAME; }

    void OnStart(openvr_pair::overlay::ShellContext &ctx) override;
    void OnShutdown(openvr_pair::overlay::ShellContext &ctx) override;
    void Tick(openvr_pair::overlay::ShellContext &ctx) override;
    void DrawTab(openvr_pair::overlay::ShellContext &ctx) override;
    void DrawLogsSection(openvr_pair::overlay::ShellContext &ctx) override;

    // Build and push the current profile settings to the driver over IPC.
    // Quiet on success; sets last_error_ and logs on failure.
    void PushConfigToDriver();

    // Send a FaceCalibrationCommand to the driver.
    void SendCalibrationCommand(protocol::FaceCalibrationOp op);

    // Tell the driver / host to load a specific hardware module by UUID.
    void SendActiveModule(const std::string &uuid);

    // Called by Tick() and by the tab functions to keep the pipe alive
    // across SteamVR restarts. Not normally called directly from UI code.
    void MaintainDriverConnection();

private:
    friend void facetracking::ui::DrawSettingsTab(FacetrackingPlugin &plugin);
    friend void facetracking::ui::DrawCalibrationTab(FacetrackingPlugin &plugin);
    friend void facetracking::ui::DrawModulesTab(FacetrackingPlugin &plugin);
    friend void facetracking::ui::DrawAdvancedTab(FacetrackingPlugin &plugin);
    friend void facetracking::ui::DrawLogsSection(FacetrackingPlugin &plugin);

    FtIPCClient                   ipc_;
    FacetrackingProfileStore      profile_;
    facetracking::HostStatusPoller host_status_;

    std::string last_error_;
    uint64_t    observed_ipc_generation_ = 0;

    std::chrono::steady_clock::time_point last_connection_check_{};
    std::chrono::steady_clock::time_point last_save_{};

    void DrawStatusBanner();
};
