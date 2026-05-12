#include "CalibrationTab.h"

#include "FacetrackingPlugin.h"
#include "Protocol.h"
#include "UiHelpers.h"

#include <imgui/imgui.h>

#include <array>
#include <cstring>
#include <string>

namespace facetracking::ui {

using namespace openvr_pair::overlay::ui;

// Unified Expressions v2 shape names (63 shapes, index-matched to
// FaceTrackingFrameBody::expressions[]). Kept as a static table so the
// per-frame draw function allocates nothing.
static const char *const kShapeNames[63] = {
    // Eyes (0-11)
    "EyeLookUp_L",    "EyeLookDown_L",  "EyeLookIn_L",    "EyeLookOut_L",
    "EyeLookUp_R",    "EyeLookDown_R",  "EyeLookIn_R",    "EyeLookOut_R",
    "EyeClosed_L",    "EyeClosed_R",    "EyeSquint_L",    "EyeSquint_R",
    // Brow (12-19)
    "BrowDown_L",     "BrowDown_R",     "BrowInnerUp_L",  "BrowInnerUp_R",
    "BrowOuterUp_L",  "BrowOuterUp_R",  "BrowPinch_L",    "BrowPinch_R",
    // Nose (20-21)
    "NoseSneer_L",    "NoseSneer_R",
    // Cheek (22-25)
    "CheekSquint_L",  "CheekSquint_R",  "CheekPuff_L",    "CheekPuff_R",
    // Jaw / mouth (26-62)
    "JawOpen",        "JawLeft",        "JawRight",       "JawForward",
    "MouthClose",     "MouthFunnel",    "MouthPucker",     "MouthLeft",
    "MouthRight",     "MouthSmile_L",   "MouthSmile_R",    "MouthFrown_L",
    "MouthFrown_R",   "MouthDimple_L",  "MouthDimple_R",   "MouthStretch_L",
    "MouthStretch_R", "MouthRollLower", "MouthRollUpper",  "MouthShrugLower",
    "MouthShrugUpper","MouthPress_L",   "MouthPress_R",    "MouthLowerDown_L",
    "MouthLowerDown_R","MouthUpperUp_L","MouthUpperUp_R",  "MouthLeft2",
    "MouthRight2",    "TongueOut",      "TongueCurl",      "TongueSquish",
    "TongueFlat",     "TongueTwistL",   "TongueTwistR",    "TongueRoll",
    "NeckTension",
};

// V1: the driver publishes shape warmth via telemetry once that channel is
// wired. For now every shape reports "cold" so the readiness dots show the
// correct initial state and the UI structure is in place.
static bool ShapeIsWarm(int /*index*/)
{
    return false; // TODO(V2): read from shmem telemetry
}

void DrawCalibrationTab(FacetrackingPlugin &plugin)
{
    FacetrackingProfile &p = plugin.profile_.current;

    // ---- Continuous calibration master toggle ----
    DrawSectionHeading("Continuous calibration");

    static const char *const kModeNames[] = { "Off", "Conservative", "Aggressive" };
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::Combo("Mode##calib", &p.continuous_calib_mode, kModeNames, 3)) {
        plugin.PushConfigToDriver();
    }
    TooltipForLastItem(
        "Off: raw values forwarded without normalisation.\n"
        "Conservative: slow EMA decay, tight outlier gates -- good default.\n"
        "Aggressive: faster adaptation, looser gates -- use when\n"
        "  switching hardware modules frequently.");

    ImGui::Spacing();

    const bool calibRunning = (p.continuous_calib_mode != 0);

    if (ImGui::Button(calibRunning ? "Pause" : "Resume")) {
        if (calibRunning) {
            plugin.SendCalibrationCommand(protocol::FaceCalibBegin); // repurpose: end/pause
        } else {
            plugin.SendCalibrationCommand(protocol::FaceCalibBegin);
        }
    }
    TooltipForLastItem("Temporarily pause or resume the running calibration\n"
                       "without discarding learned data.");

    ImGui::SameLine();

    // Reset with confirmation.
    static bool confirmResetAll = false;
    if (ImGui::Button("Reset learned data")) {
        confirmResetAll = true;
    }
    TooltipForLastItem("Discard all per-shape learned bounds.\n"
                       "A confirmation dialog will appear.");

    if (confirmResetAll) {
        ImGui::OpenPopup("ft_confirm_reset");
        confirmResetAll = false;
    }
    if (ImGui::BeginPopupModal("ft_confirm_reset", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Reset all learned calibration data for this module?");
        ImGui::Text("This cannot be undone.");
        ImGui::Spacing();
        if (ImGui::Button("Reset", ImVec2(120, 0))) {
            plugin.SendCalibrationCommand(protocol::FaceCalibResetAll);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ---- Vergence / focus-distance readout ----
    DrawSectionHeading("Vergence readout");

    ImGui::TextDisabled("Focus distance: n/a (driver telemetry not wired in V1)");
    ImGui::TextDisabled("IPD estimate:   n/a");

    // ---- Per-shape readiness grid ----
    DrawSectionHeading("Shape readiness (63 expressions + 2 eyes)");

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));

    // Two additional eye-open rows not in the expression array.
    const int kEyeCount  = 2;
    const int kTotalDots = 63 + kEyeCount;

    // Compute how many columns fit.
    const float cellW  = 14.0f;
    const float avail  = ImGui::GetContentRegionAvail().x;
    const int   cols   = (int)(avail / (cellW + 4.0f));
    const int   safeCols = (cols > 0) ? cols : 1;

    for (int i = 0; i < kTotalDots; ++i) {
        if (i > 0 && (i % safeCols) != 0) ImGui::SameLine();

        const bool warm = (i < 63) ? ShapeIsWarm(i) : false;
        const char *label = (i < 63)
            ? kShapeNames[i]
            : (i == 63 ? "EyeOpen_L" : "EyeOpen_R");

        ImVec4 col = warm
            ? ImVec4(0.2f, 0.8f, 0.25f, 1.0f)   // green
            : ImVec4(0.35f, 0.35f, 0.35f, 1.0f); // grey

        ImGui::PushStyleColor(ImGuiCol_Button, col);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  col);

        char btnId[32];
        snprintf(btnId, sizeof(btnId), "##dot%d", i);
        ImGui::Button(btnId, ImVec2(cellW, cellW));
        ImGui::PopStyleColor(3);

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s: %s", label, warm ? "warm" : "cold (< 200 samples)");
    }

    ImGui::PopStyleVar();
}

} // namespace facetracking::ui
