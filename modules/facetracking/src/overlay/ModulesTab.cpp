#include "ModulesTab.h"

#include "FacetrackingPlugin.h"
#include "Logging.h"
#include "ModuleSources.h"
#include "UiHelpers.h"

#include "picojson.h"

#include <imgui/imgui.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>

#include <string>
#include <vector>

namespace facetracking::ui {

using namespace openvr_pair::overlay::ui;

// ---- state shared across draws (per-overlay-instance, reset on tab show) -

struct ModulesTabState
{
    // Installed-modules list (refreshed each frame from disk scan,
    // throttled so we don't hammer the FS every 60 Hz frame).
    std::vector<InstalledModule>  installed;
    SourcesCatalogue              catalogue;
    int64_t                       last_scan_tick = 0;   // ::GetTickCount64()

    // Dropdown selection index for active-module picker.
    int  selected_idx = 0;  // 0 = (none/auto), 1..N = installed[idx-1]

    // Last sync status string shown near the sources table.
    // Populated by polling plugin.sync_runner_ each draw.
    std::string sync_status;
    bool        sync_status_ok = true;

    // GitHub "add source" popup input.
    char github_input[256] = {};

    bool initialised = false;
};

static ModulesTabState g_state;

static void RefreshIfStale()
{
    uint64_t now = GetTickCount64();
    // Rescan at most once per second.
    if (now - static_cast<uint64_t>(g_state.last_scan_tick) < 1000 && g_state.initialised)
        return;

    g_state.last_scan_tick = static_cast<int64_t>(now);
    g_state.installed      = ScanInstalledModules();
    g_state.catalogue      = EnsureSourcesCatalogue();
    g_state.initialised    = true;
}

// Open a Win32 folder-picker dialog (IFileOpenDialog + FOS_PICKFOLDERS).
// Returns empty string if the user cancels or COM fails.
static std::string PickFolder()
{
    std::string result;
    IFileOpenDialog *pDlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&pDlg))))
        return {};

    DWORD opts = 0;
    pDlg->GetOptions(&opts);
    pDlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    pDlg->SetTitle(L"Select face-tracking module folder");

    if (SUCCEEDED(pDlg->Show(nullptr))) {
        IShellItem *pItem = nullptr;
        if (SUCCEEDED(pDlg->GetResult(&pItem))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
                if (n > 1) {
                    result.resize(static_cast<size_t>(n - 1));
                    WideCharToMultiByte(CP_UTF8, 0, path, -1, result.data(), n, nullptr, nullptr);
                }
                CoTaskMemFree(path);
            }
            pItem->Release();
        }
    }
    pDlg->Release();
    return result;
}

// Parse "https://github.com/owner/repo" or "owner/repo" -> "owner/repo".
static std::string ParseOwnerRepo(const std::string &input)
{
    static const char prefix[] = "https://github.com/";
    if (input.rfind(prefix, 0) == 0)
        return input.substr(sizeof(prefix) - 1);
    // Rudimentary validation: must contain exactly one '/'.
    size_t slash = input.find('/');
    if (slash == std::string::npos || slash == 0 || slash == input.size() - 1)
        return {};
    return input;
}

// Build source_data JSON for the PowerShell helper.
static std::string BuildSourceDataJson(const ModuleSource &src)
{
    picojson::object o;
    o["id"]          = picojson::value(src.id);
    o["kind"]        = picojson::value(
        src.kind == SourceKind::Folder  ? "folder" :
        src.kind == SourceKind::GitHub  ? "github" : "registry");
    o["label"]       = picojson::value(src.label);
    if (!src.path.empty())       o["path"]       = picojson::value(src.path);
    if (!src.owner_repo.empty()) o["owner_repo"] = picojson::value(src.owner_repo);
    return picojson::value(o).serialize();
}

// ---- section helpers ----------------------------------------------------

static void DrawActiveModuleSection(FacetrackingPlugin &plugin)
{
    DrawSectionHeading("Active module");

    const auto &hs = plugin.HostStatus().Snapshot();
    const std::string *hostActiveUuid =
        hs.valid && hs.active_module.has_value() ? &hs.active_module->uuid : nullptr;

    // Build combo items: first entry is auto-select.
    std::vector<std::string> items;
    items.push_back("(none -- host auto-selects)");
    for (const auto &m : g_state.installed)
        items.push_back(m.name + " v" + m.version + " -- " + m.vendor);

    // Sync selected_idx with the persisted profile on first use.
    static bool idx_synced = false;
    if (!idx_synced) {
        idx_synced = true;
        const std::string &saved = plugin.Profile().current.active_module_uuid;
        if (!saved.empty()) {
            for (size_t i = 0; i < g_state.installed.size(); ++i) {
                if (g_state.installed[i].uuid == saved) {
                    g_state.selected_idx = static_cast<int>(i + 1);
                    break;
                }
            }
        }
    }

    // Clamp in case the installed list shrank.
    if (g_state.selected_idx > static_cast<int>(g_state.installed.size()))
        g_state.selected_idx = 0;

    // Current combo label.
    const char *preview = items[static_cast<size_t>(g_state.selected_idx)].c_str();

    // Show "[active]" badge when host confirms it.
    std::string activeLabel;
    if (hostActiveUuid) {
        const std::string *selUuid = g_state.selected_idx > 0
            ? &g_state.installed[static_cast<size_t>(g_state.selected_idx - 1)].uuid
            : nullptr;
        if (selUuid && *selUuid == *hostActiveUuid)
            activeLabel = "  [active]";
    }

    ImGui::SetNextItemWidth(340.0f);
    if (ImGui::BeginCombo("##ft_active_mod", preview)) {
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            bool sel = (g_state.selected_idx == i);
            if (ImGui::Selectable(items[static_cast<size_t>(i)].c_str(), sel))
                g_state.selected_idx = i;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    TooltipForLastItem("Choose which hardware module the host should load.\n"
                       "Leave on auto to let the host pick the first available.");

    if (!activeLabel.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "%s", activeLabel.c_str());
    }

    ImGui::SameLine();
    if (ImGui::Button("Apply##ft_mod_apply")) {
        std::string uuid;
        if (g_state.selected_idx > 0)
            uuid = g_state.installed[static_cast<size_t>(g_state.selected_idx - 1)].uuid;
        FT_LOG_OVL("[modules] user set active module: '%s'", uuid.c_str());
        plugin.SendActiveModule(uuid);
        idx_synced = false;  // re-sync next frame
    }
    TooltipForLastItem("Persist the selection and tell the host (if running) to switch modules.");
}

static void DrawInstalledModulesSection()
{
    DrawSectionHeading("Installed modules");

    if (g_state.installed.empty()) {
        DrawWaitingBanner("No modules installed. Add a source below to fetch modules.");
        return;
    }

    ImGuiTableFlags tf = ImGuiTableFlags_Borders
                       | ImGuiTableFlags_RowBg
                       | ImGuiTableFlags_Resizable
                       | ImGuiTableFlags_SizingStretchProp;

    if (!ImGui::BeginTable("ft_installed_v2", 5, tf)) return;
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Version",  ImGuiTableColumnFlags_WidthFixed,  80.0f);
    ImGui::TableSetupColumn("Vendor",   ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("Source",   ImGuiTableColumnFlags_WidthFixed, 160.0f);
    ImGui::TableSetupColumn("Notes");
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < g_state.installed.size(); ++i) {
        const auto &m = g_state.installed[i];
        bool isSelected = g_state.selected_idx == static_cast<int>(i + 1);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (isSelected)
            ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "%s", m.name.c_str());
        else
            ImGui::TextUnformatted(m.name.c_str());

        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(m.version.c_str());

        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(m.vendor.c_str());

        ImGui::TableSetColumnIndex(3);
        std::string srcLabel = SourceLabel(g_state.catalogue, m.source_id);
        ImGui::TextDisabled("%s", srcLabel.c_str());

        ImGui::TableSetColumnIndex(4);
        if (m.source_kind_str == "github" && !m.sha_verified) {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Unverified");
            TooltipForLastItem("This module was installed without SHA-256 verification.\n"
                               "The release notes did not contain a recognisable SHA-256 hash.\n"
                               "Confirm the developer publishes verifiable hashes before trusting this source.");
        } else if (m.source_kind_str == "folder") {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "Local");
            TooltipForLastItem("This module was installed from a local folder.\n"
                               "Local modules are not signature-verified.");
        }
    }
    ImGui::EndTable();
}

static void DrawSourcesSection(FacetrackingPlugin &plugin)
{
    DrawSectionHeading("Module sources");

    // ---- poll sync runner (shared with plugin startup auto-updates) ----
    auto result = plugin.SyncRunner().Poll();
    if (result.has_value()) {
        g_state.sync_status    = result->message;
        g_state.sync_status_ok = result->ok;
        if (result->ok) {
            // Refresh installed list and catalogue immediately.
            g_state.last_scan_tick = 0;
            g_state.catalogue = EnsureSourcesCatalogue();
        }
    }

    // ---- status line ----
    if (!g_state.sync_status.empty()) {
        if (g_state.sync_status_ok)
            ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f),
                               "Sync: %s", g_state.sync_status.c_str());
        else
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f),
                               "Sync error: %s", g_state.sync_status.c_str());
    } else if (plugin.SyncRunner().IsRunning()) {
        ImGui::TextDisabled("Syncing...");
    }

    // ---- sources table ----
    ImGuiTableFlags tf = ImGuiTableFlags_Borders
                       | ImGuiTableFlags_RowBg
                       | ImGuiTableFlags_SizingStretchProp;

    if (ImGui::BeginTable("ft_sources", 5, tf)) {
        ImGui::TableSetupColumn("Label");
        ImGui::TableSetupColumn("Kind",      ImGuiTableColumnFlags_WidthFixed,  70.0f);
        ImGui::TableSetupColumn("Auto-upd",  ImGuiTableColumnFlags_WidthFixed,  65.0f);
        ImGui::TableSetupColumn("Status",    ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Actions",   ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableHeadersRow();

        bool needSave = false;
        int  removeIdx = -1;
        std::string syncSourceId;
        std::string syncSourceData;

        for (int si = 0; si < static_cast<int>(g_state.catalogue.sources.size()); ++si) {
            auto &src = g_state.catalogue.sources[static_cast<size_t>(si)];
            bool isRegistry = src.kind == SourceKind::Registry;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(src.label.c_str());

            ImGui::TableSetColumnIndex(1);
            const char *kindStr =
                src.kind == SourceKind::Folder  ? "Folder"   :
                src.kind == SourceKind::GitHub  ? "GitHub"   : "Registry";
            ImGui::TextDisabled("%s", kindStr);

            ImGui::TableSetColumnIndex(2);
            if (isRegistry) {
                // Registry auto-update is always on but user can't toggle it.
                bool dummy = true;
                ImGui::BeginDisabled(true);
                ImGui::Checkbox(("##au_" + src.id).c_str(), &dummy);
                ImGui::EndDisabled();
            } else if (src.kind == SourceKind::GitHub) {
                if (ImGui::Checkbox(("##au_" + src.id).c_str(), &src.auto_update))
                    needSave = true;
            } else {
                // Folder: greyed out
                bool dummy = false;
                ImGui::BeginDisabled(true);
                ImGui::Checkbox(("##au_" + src.id).c_str(), &dummy);
                ImGui::EndDisabled();
            }

            ImGui::TableSetColumnIndex(3);
            if (!src.last_sync_error.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f),
                                   "%s", src.last_sync_error.c_str());
            else if (!src.last_checked_at.empty())
                ImGui::TextDisabled("%s", src.last_checked_at.c_str());
            else
                ImGui::TextDisabled("Never synced");

            ImGui::TableSetColumnIndex(4);
            bool syncing = plugin.SyncRunner().IsRunning();

            ImGui::BeginDisabled(syncing);
            if (ImGui::SmallButton(("Sync##" + src.id).c_str())) {
                syncSourceId   = src.id;
                syncSourceData = BuildSourceDataJson(src);
            }
            TooltipForLastItem("Check for updates and re-install if newer.");

            if (!isRegistry) {
                ImGui::SameLine();
                if (ImGui::SmallButton(("Remove##" + src.id).c_str()))
                    removeIdx = si;
                TooltipForLastItem("Remove this source and delete its installed modules.");
            }
            ImGui::EndDisabled();
        }

        if (needSave) SaveSourcesCatalogue(g_state.catalogue);

        // Trigger sync outside the table loop to avoid iterator invalidation.
        if (!syncSourceId.empty() && !plugin.SyncRunner().IsRunning())
            plugin.SyncRunner().StartUpdate(syncSourceId, syncSourceData);

        if (removeIdx >= 0) {
            const auto &src = g_state.catalogue.sources[static_cast<size_t>(removeIdx)];
            std::string id = src.id;
            plugin.SyncRunner().StartRemove(id);
            g_state.catalogue.sources.erase(
                g_state.catalogue.sources.begin() + removeIdx);
            SaveSourcesCatalogue(g_state.catalogue);
        }

        ImGui::EndTable();
    }

    // ---- add buttons ----
    ImGui::Spacing();
    bool syncing = plugin.SyncRunner().IsRunning();

    ImGui::BeginDisabled(syncing);
    if (ImGui::Button("Add folder source...##ft_add_folder")) {
        std::string chosen = PickFolder();
        if (!chosen.empty()) {
            ModuleSource src;
            src.id       = GenerateSourceId();
            src.kind     = SourceKind::Folder;
            src.path     = chosen;
            // Label = last path component.
            size_t slash = chosen.find_last_of("\\/");
            src.label    = "Folder: " + (slash != std::string::npos
                               ? chosen.substr(slash + 1) : chosen);
            src.added_at = NowIso8601();
            std::string data = BuildSourceDataJson(src);
            g_state.catalogue.sources.push_back(src);
            SaveSourcesCatalogue(g_state.catalogue);
            plugin.SyncRunner().StartAdd(SourceKind::Folder, data, src.id);
        }
    }
    TooltipForLastItem("Pick a local folder containing a packaged face-tracking module.");

    ImGui::SameLine();
    if (ImGui::Button("Add GitHub repo...##ft_add_github"))
        ImGui::OpenPopup("ft_add_github_popup");
    TooltipForLastItem("Fetch the latest release from a GitHub repository.");
    ImGui::EndDisabled();

    if (ImGui::BeginPopupModal("ft_add_github_popup", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("GitHub repository URL or owner/repo:");
        ImGui::SetNextItemWidth(360.0f);
        ImGui::InputText("##ft_github_url", g_state.github_input,
                         sizeof(g_state.github_input));
        TooltipForLastItem("e.g. https://github.com/owner/repo  or  owner/repo");
        ImGui::Spacing();
        if (ImGui::Button("Add##ft_github_add", ImVec2(110, 0))) {
            std::string ownerRepo = ParseOwnerRepo(std::string(g_state.github_input));
            if (!ownerRepo.empty()) {
                ModuleSource src;
                src.id          = GenerateSourceId();
                src.kind        = SourceKind::GitHub;
                src.owner_repo  = ownerRepo;
                src.label       = ownerRepo;
                src.auto_update = true;
                src.added_at    = NowIso8601();
                std::string data = BuildSourceDataJson(src);
                g_state.catalogue.sources.push_back(src);
                SaveSourcesCatalogue(g_state.catalogue);
                plugin.SyncRunner().StartAdd(SourceKind::GitHub, data, src.id);
                g_state.github_input[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
            // If ownerRepo empty: leave popup open, no-op (bad format).
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##ft_github_cancel", ImVec2(110, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

}

static void DrawTrustBanners()
{
    bool hasNonRegistry = false;
    bool hasUnverifiedGithub = false;
    for (const auto &src : g_state.catalogue.sources) {
        if (src.kind != SourceKind::Registry) hasNonRegistry = true;
    }
    for (const auto &m : g_state.installed) {
        if (m.source_kind_str == "github" && !m.sha_verified) {
            hasUnverifiedGithub = true;
            break;
        }
    }

    if (hasNonRegistry) {
        DrawBanner(
            "Untrusted sources",
            "Folder and GitHub sources are untrusted. They can execute arbitrary "
            "code in the face-tracking host process. Only add sources from "
            "developers you trust.",
            ImVec4(0.38f, 0.30f, 0.02f, 1.0f),
            ImVec4(1.0f, 0.92f, 0.45f, 1.0f),
            ImVec4(1.0f, 0.96f, 0.80f, 1.0f));
    }

    if (hasUnverifiedGithub) {
        DrawBanner(
            "Unverified SHA-256",
            "One or more GitHub modules were installed without SHA-256 verification "
            "because the release notes did not contain a SHA-256 hash. Confirm the "
            "developer publishes verifiable hashes before continuing to trust this source.",
            ImVec4(0.42f, 0.06f, 0.06f, 1.0f),
            ImVec4(1.0f, 0.88f, 0.88f, 1.0f),
            ImVec4(1.0f, 0.96f, 0.96f, 1.0f));
    }
}

// ---- main entry point ---------------------------------------------------

void DrawModulesTab(FacetrackingPlugin &plugin)
{
    RefreshIfStale();

    DrawActiveModuleSection(plugin);
    DrawInstalledModulesSection();
    DrawSourcesSection(plugin);
    DrawTrustBanners();
}

} // namespace facetracking::ui
