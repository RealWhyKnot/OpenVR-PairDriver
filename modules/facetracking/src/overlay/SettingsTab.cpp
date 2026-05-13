#include "SettingsTab.h"

#include "FacetrackingPlugin.h"
#include "UiHelpers.h"

#include <imgui/imgui.h>

#include <cstring>

namespace facetracking::ui {

using namespace openvr_pair::overlay::ui;

void DrawSettingsTab(FacetrackingPlugin &plugin)
{
    FacetrackingProfile &p = plugin.profile_.current;

    // ---- Master toggle ----
    DrawSectionHeading("Master");

    if (CheckboxWithTooltip("Enable Face Tracking", &p.master_enabled,
            "Master on/off for the entire face-tracking pipeline.\n"
            "Off = driver publishes no face/eye inputs and does not\n"
            "start the hardware module host.")) {
        plugin.PushConfigToDriver();
    }

    // ---- Eyelid Sync ----
    DrawSectionHeading("Eyelid Sync");

    if (CheckboxWithTooltip("Eyelid Sync", &p.eyelid_sync_enabled,
            "Blends both eye openness values toward a weighted average\n"
            "to reduce asymmetric flicker from tracking noise.\n"
            "Strength 0 = no effect even when enabled.")) {
        plugin.PushConfigToDriver();
    }

    if (!p.eyelid_sync_enabled) ImGui::BeginDisabled();

    if (SliderIntWithTooltip("Sync Strength##eyelid", &p.eyelid_sync_strength,
            0, 100, "%d%%",
            "How aggressively both eyes are pulled toward the average.\n"
            "100 = fully forced equal; 0 = no correction applied.\n"
            "70 is a safe default -- covers sensor noise without\n"
            "flattening deliberate asymmetry.")) {
        plugin.PushConfigToDriver();
    }

    if (CheckboxWithTooltip("Preserve intentional winks", &p.eyelid_sync_preserve_winks,
            "Detects a deliberate one-eye wink (asymmetry > 45%% sustained\n"
            "for 120 ms with good confidence) and bypasses sync during\n"
            "it so winks survive even at high strength settings.")) {
        plugin.PushConfigToDriver();
    }

    if (!p.eyelid_sync_enabled) ImGui::EndDisabled();

    // ---- Vergence Lock ----
    DrawSectionHeading("Vergence Lock");

    if (CheckboxWithTooltip("Vergence Lock", &p.vergence_lock_enabled,
            "Reconstructs a shared focus point from both gaze rays each\n"
            "frame (skew-line midpoint) and nudges both eyes toward it.\n"
            "Reduces the jitter and crossed-eye artefacts that appear\n"
            "when each eye's tracker drifts independently.")) {
        plugin.PushConfigToDriver();
    }

    if (!p.vergence_lock_enabled) ImGui::BeginDisabled();

    if (SliderIntWithTooltip("Lock Strength##vergence", &p.vergence_lock_strength,
            0, 100, "%d%%",
            "How much each eye's gaze is pulled toward the computed\n"
            "focus point. Start at 50 and increase if you still see\n"
            "crossed-eye artefacts; lower if the focus point feels\n"
            "sluggish on fast saccades.")) {
        plugin.PushConfigToDriver();
    }

    if (!p.vergence_lock_enabled) ImGui::EndDisabled();

    // ---- Output ----
    DrawSectionHeading("Output");

    if (CheckboxWithTooltip("OSC (VRChat)", &p.output_osc_enabled,
            "Sends /avatar/parameters/* over UDP to the host and port\n"
            "below. Default 127.0.0.1:9000 (VRChat on the same PC).")) {
        plugin.PushConfigToDriver();
    }

    if (p.output_osc_enabled) {
        static char oscHostBuf[40] = {};
        if (oscHostBuf[0] == '\0')
            std::strncpy(oscHostBuf, p.osc_host.c_str(), sizeof(oscHostBuf) - 1);

        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::InputText("OSC host##ft", oscHostBuf, sizeof(oscHostBuf))) {
            p.osc_host = oscHostBuf;
            plugin.PushConfigToDriver();
        }
        TooltipForLastItem("Dotted-quad IP or hostname of the OSC receiver.\n"
                           "Restart required for host changes to take effect.");

        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::InputInt("Port##ftosc", &p.osc_port, 0, 0)) {
            if (p.osc_port < 1)     p.osc_port = 1;
            if (p.osc_port > 65535) p.osc_port = 65535;
            plugin.PushConfigToDriver();
        }
        TooltipForLastItem("UDP port for the OSC receiver (default 9000 for VRChat).");

        // Status text for OSC -- driven by the host_status.json sidecar the
        // C# host writes once per second.
        const auto &host = plugin.host_status_.Snapshot();
        if (!host.valid) {
            ImGui::TextDisabled("OSC status: host not started yet.");
        } else if (host.stale || host.host_shutting_down) {
            ImGui::TextDisabled("OSC status: host stopped (last seen pid=%d).", host.host_pid);
        } else if (!host.osc.enabled) {
            ImGui::TextDisabled("OSC status: sender not running.");
        } else if (!host.osc.last_error.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.25f, 1.0f),
                "OSC error: %s", host.osc.last_error.c_str());
            ImGui::Text("Sending to %s:%d -- %lld packet(s), %lld error(s)",
                host.osc.target_host.c_str(), host.osc.target_port,
                host.osc.packets_sent, host.osc.packets_errored);
        } else {
            ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f),
                "OSC OK -> %s:%d @ %.0f pkt/s",
                host.osc.target_host.c_str(), host.osc.target_port,
                host.osc.packets_per_second);
            ImGui::Text("Total sent: %lld   errored: %lld",
                host.osc.packets_sent, host.osc.packets_errored);
        }
    }

    if (CheckboxWithTooltip("Native (OpenXR eye-gaze)", &p.output_native_enabled,
            "Publishes gaze pose and eye-openness scalar components via\n"
            "the SteamVR driver interface so XR_EXT_eye_gaze_interaction\n"
            "consumers (Resonite, future VRChat) receive live data\n"
            "without OSC.")) {
        plugin.PushConfigToDriver();
    }

    // Native eye gaze path: the host has no observable state for this beyond
    // the toggle. VRChat doesn't consume native eye-gaze on OpenVR anyway,
    // so this stays a stub until the pinned openvr SDK exposes
    // Prop_HasEyeTracking_Bool / CreatePoseComponent (see project memo
    // project_facetracking_v1_2026-05-12.md).
    ImGui::TextDisabled("Native status: stub -- not consumed by VRChat on OpenVR.");
}

} // namespace facetracking::ui
