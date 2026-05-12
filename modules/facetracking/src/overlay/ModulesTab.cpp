#include "ModulesTab.h"

#include "FacetrackingPlugin.h"
#include "Logging.h"
#include "UiHelpers.h"

#include <imgui/imgui.h>

#include <cstring>
#include <string>

namespace facetracking::ui {

using namespace openvr_pair::overlay::ui;

void DrawModulesTab(FacetrackingPlugin &plugin)
{
    // ---- Registry table ----
    DrawSectionHeading("Installed modules");

    // V1: the registry list arrives from the C# host over a channel that is
    // not wired yet. Show a clear placeholder instead of an empty table so
    // the user is not confused by blank space.
    DrawWaitingBanner("Modules list not yet wired (V1 placeholder).\n"
                      "Use the UUID input below to select an installed module.");

    // ---- Manual UUID override ----
    DrawSectionHeading("Select active module by UUID");

    static char uuidBuf[40] = {};
    // Pre-fill from the stored profile on first draw.
    if (uuidBuf[0] == '\0' && !plugin.profile_.current.active_module_uuid.empty()) {
        std::strncpy(uuidBuf, plugin.profile_.current.active_module_uuid.c_str(),
                     sizeof(uuidBuf) - 1);
    }

    ImGui::SetNextItemWidth(300.0f);
    ImGui::InputText("Module UUID##ft_uuid", uuidBuf, sizeof(uuidBuf));
    TooltipForLastItem(
        "UUID of the hardware module to activate, e.g.\n"
        "  quest-pro-module-v1 (check the module's manifest.json).\n"
        "Leave blank for automatic selection (first available).");

    ImGui::SameLine();
    if (ImGui::Button("Apply##ft_uuid_apply")) {
        plugin.SendActiveModule(std::string(uuidBuf));
    }
    TooltipForLastItem("Tell the host process to load this module UUID.\n"
                       "Takes effect on the next host restart if the module\n"
                       "is already loaded.");

    // ---- Trust controls ----
    DrawSectionHeading("Trust and security");

    static bool confirmUnsigned = false;
    static bool unsignedEnabled = false;

    if (CheckboxWithTooltip("Enable unsigned modules (developer mode)", &unsignedEnabled,
            "Allows modules that have no Ed25519 signature from a\n"
            "trusted publisher. A banner is shown on every host launch\n"
            "while this is active. Only enable if you are testing your\n"
            "own module locally.")) {
        if (unsignedEnabled) {
            confirmUnsigned = true;
            unsignedEnabled = false; // revert until confirmed
        } else {
            // Turning off -- no confirmation needed.
            FT_LOG_OVL("[modules] unsigned module dev mode disabled");
            // TODO(V2): write to trust.json via host control message
        }
    }

    if (confirmUnsigned) {
        ImGui::OpenPopup("ft_confirm_unsigned");
        confirmUnsigned = false;
    }
    if (ImGui::BeginPopupModal("ft_confirm_unsigned", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enable unsigned module loading?");
        ImGui::TextWrapped(
            "Unsigned modules have not been verified by any trusted\n"
            "publisher and could behave unexpectedly. Only enable this\n"
            "if you are testing your own module locally.");
        ImGui::Spacing();
        if (ImGui::Button("Enable", ImVec2(120, 0))) {
            unsignedEnabled = true;
            FT_LOG_OVL("[modules] unsigned module dev mode ENABLED by user");
            // TODO(V2): write to trust.json via host control message
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    static bool confirmPublisher = false;
    if (ImGui::Button("Add trusted publisher")) {
        confirmPublisher = true;
    }
    TooltipForLastItem("Add an Ed25519 public key to the trust list.\n"
                       "Modules signed by this key will be loadable\n"
                       "without the 'unsigned' override.");

    if (confirmPublisher) {
        ImGui::OpenPopup("ft_add_publisher");
        confirmPublisher = false;
    }
    if (ImGui::BeginPopupModal("ft_add_publisher", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        static char pubKeyBuf[128] = {};
        ImGui::Text("Paste the publisher's Ed25519 public key (base64):");
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputText("##ft_pubkey", pubKeyBuf, sizeof(pubKeyBuf));
        ImGui::Spacing();
        if (ImGui::Button("Add", ImVec2(120, 0))) {
            FT_LOG_OVL("[modules] add trusted publisher requested (len=%zu)", strlen(pubKeyBuf));
            // TODO(V2): forward to trust.json via host control message
            pubKeyBuf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace facetracking::ui
