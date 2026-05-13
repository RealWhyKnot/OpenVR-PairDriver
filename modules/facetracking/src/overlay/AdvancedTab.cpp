#include "AdvancedTab.h"

#include "FacetrackingPlugin.h"
#include "Logging.h"
#include "Protocol.h"
#include "UiHelpers.h"

#include <imgui/imgui.h>

namespace facetracking::ui {

using namespace openvr_pair::overlay::ui;

void DrawAdvancedTab(FacetrackingPlugin &plugin)
{
    FacetrackingProfile &p = plugin.profile_.current;

    // ---- Smoothing ----
    DrawSectionHeading("Signal smoothing");

    if (SliderIntWithTooltip("Gaze smoothing", &p.gaze_smoothing,
            0, 100, "%d%%",
            "EMA strength applied to eye-gaze direction after vergence\n"
            "lock reconstruction. Higher values reduce jitter at the\n"
            "cost of response lag on fast saccades. 20-40 is typical.")) {
        plugin.PushConfigToDriver();
    }

    if (SliderIntWithTooltip("Openness smoothing", &p.openness_smoothing,
            0, 100, "%d%%",
            "EMA strength applied to eye-openness values after eyelid\n"
            "sync. Smooths blink onset/offset ringing without delaying\n"
            "the closed position significantly. 15-30 is typical.")) {
        plugin.PushConfigToDriver();
    }

    // ---- Host process ----
    DrawSectionHeading("Host process");

    // Pull live status from the host_status.json sidecar the C# host writes
    // once per second. valid=false means the host hasn't started yet; stale
    // means the host stopped refreshing the file (crashed or stopped).
    const auto &hs = plugin.host_status_.Snapshot();
    if (!hs.valid) {
        ImGui::TextDisabled("Host status: not started.");
    } else if (hs.host_shutting_down) {
        ImGui::TextDisabled("Host status: shutting down (pid=%d).", hs.host_pid);
    } else if (hs.stale) {
        ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.25f, 1.0f),
            "Host status: stale (last pid=%d, no refresh in 10s+).", hs.host_pid);
    } else {
        int  s   = hs.host_uptime_seconds;
        int  h   = s / 3600;
        int  m   = (s % 3600) / 60;
        int  sec = s % 60;
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f),
            "Host running: pid=%d uptime=%02d:%02d:%02d",
            hs.host_pid, h, m, sec);
        if (hs.active_module.has_value()) {
            ImGui::Text("Active module: %s %s (%s)",
                hs.active_module->name.c_str(),
                hs.active_module->version.c_str(),
                hs.active_module->vendor.c_str());
        } else {
            ImGui::TextDisabled("Active module: (none)");
        }
    }

    if (ImGui::Button("Restart host process")) {
        plugin.SendCalibrationCommand(protocol::FaceCalibSave); // flush calib first
        // TODO(V2): host restart control message via driver pipe
        FT_LOG_OVL("[advanced] user requested host restart (stub)");
    }
    TooltipForLastItem(
        "Signal the driver to terminate and respawn the C# module host.\n"
        "Use this if the host has wedged or a module update requires\n"
        "a clean reload. Calibration data is flushed first.");

    // ---- OSC discovery override ----
    DrawSectionHeading("OSC discovery");

    static bool oscDiscoveryOverride = false;
    if (CheckboxWithTooltip("Disable mDNS VRChat discovery", &oscDiscoveryOverride,
            "The module host attempts mDNS-based VRChat port discovery\n"
            "and falls back to 127.0.0.1:9000 when it fails.\n"
            "Check this box to skip discovery and always use the\n"
            "host/port configured in Settings.")) {
        // TODO(V2): send override flag to host via config
    }

    // ---- Value preview ----
    DrawSectionHeading("Value preview");

    if (CheckboxWithTooltip("Show raw (un-calibrated) values", &p.show_raw_values,
            "When checked, the Calibration tab readiness dots show\n"
            "raw hardware values before normalisation so you can judge\n"
            "the calibration offset directly.")) {
        // Overlay-only preference; no driver push needed.
        plugin.profile_.Save();
    }
}

} // namespace facetracking::ui
