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

    // Read the host's periodic sidecar file. valid=false means the host
    // hasn't started yet (no install scan has run). The list is what the
    // host found under %LocalAppDataLow%\OpenVR-Pair\facetracking\modules\
    // on its last scan.
    const auto &hs = plugin.host_status_.Snapshot();
    if (!hs.valid) {
        DrawWaitingBanner("Waiting for the host process to start.\n"
                          "Enable Face Tracking and launch SteamVR.");
    } else if (hs.installed_modules.empty()) {
        DrawWaitingBanner("No modules installed yet.\n"
                          "Modules land in %LocalAppDataLow%\\OpenVR-Pair\\facetracking\\modules\\\n"
                          "after the host fetches them from the registry.");
    } else {
        // Sortable table with the modules the host last saw on disk.
        ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders
                                   | ImGuiTableFlags_RowBg
                                   | ImGuiTableFlags_Resizable
                                   | ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("ft_installed_modules", 4, tableFlags)) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Vendor", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("UUID");
            ImGui::TableHeadersRow();

            const std::string *activeUuid =
                hs.active_module.has_value() ? &hs.active_module->uuid : nullptr;

            for (const auto &mod : hs.installed_modules) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                bool isActive = activeUuid && *activeUuid == mod.uuid;
                if (isActive) {
                    ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f),
                        "%s  [active]", mod.name.c_str());
                } else {
                    ImGui::TextUnformatted(mod.name.c_str());
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(mod.version.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(mod.vendor.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextDisabled("%s", mod.uuid.c_str());
            }
            ImGui::EndTable();
        }
    }

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
