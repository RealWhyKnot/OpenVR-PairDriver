#include "LogsSection.h"

#include "BuildStamp.h"
#include "FacetrackingPlugin.h"
#include "Logging.h"
#include "UiHelpers.h"

#include <imgui/imgui.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>

#include <string>
#include <vector>

namespace facetracking::ui {

using namespace openvr_pair::overlay::ui;

namespace {

std::wstring LogsDir()
{
    PWSTR raw = nullptr;
    if (S_OK != SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, nullptr, &raw)) {
        if (raw) CoTaskMemFree(raw);
        return {};
    }
    std::wstring root(raw);
    CoTaskMemFree(raw);
    return root + L"\\OpenVR-Pair\\Logs";
}

void OpenInExplorer(const std::wstring &dir)
{
    ShellExecuteW(nullptr, L"explore", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

struct LogEntry {
    std::wstring path;
    std::string  nameUtf8;
};

std::vector<LogEntry> EnumerateLogs(const std::wstring &dir)
{
    std::vector<LogEntry> result;
    if (dir.empty()) return result;

    std::wstring search = dir + L"\\facetracking_log.*.txt";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(search.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return result;
    do {
        int needed = WideCharToMultiByte(
            CP_UTF8, 0, fd.cFileName, -1, nullptr, 0, nullptr, nullptr);
        std::string name(needed > 0 ? needed - 1 : 0, '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, fd.cFileName, -1, name.data(), needed, nullptr, nullptr);
        result.push_back({ dir + L"\\" + fd.cFileName, std::move(name) });
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return result;
}

} // namespace

void DrawLogsSection(FacetrackingPlugin &plugin)
{
    // ---- IPC state ----
    ImGui::Text("IPC: %s", plugin.ipc_.IsConnected() ? "connected" : "disconnected");

    // ---- Verbose logging toggle ----
    DrawSectionHeading("Logging");

    const bool isDev = (std::string(FACETRACKING_BUILD_CHANNEL) == "dev");

    if (isDev) {
        FtOverlayVerbose.store(true, std::memory_order_relaxed);
        ImGui::BeginDisabled();
    }

    bool verboseOverlay = FtOverlayVerbose.load(std::memory_order_relaxed);
    if (CheckboxWithTooltip("Verbose overlay logging", &verboseOverlay,
            "Write extra trace lines from the overlay side to the\n"
            "facetracking_log.* file. Enabled and locked in dev builds.")) {
        FtOverlayVerbose.store(verboseOverlay, std::memory_order_relaxed);
    }

    if (isDev) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("(forced on -- dev channel)");
    }

    // ---- Log file list ----
    DrawSectionHeading("Log files");

    std::wstring dir    = LogsDir();
    auto         entries = EnumerateLogs(dir);

    if (entries.empty()) {
        ImGui::TextDisabled("No facetracking_log.*.txt files found.");
    } else {
        for (const auto &e : entries) {
            ImGui::TextUnformatted(e.nameUtf8.c_str());
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("Open log folder in Explorer")) {
        OpenInExplorer(dir);
    }
    TooltipForLastItem(
        "%LocalAppDataLow%\\OpenVR-Pair\\Logs\\\n"
        "Contains overlay, driver, and host log files for all features.");

    // ---- File paths (reference) ----
    DrawSectionHeading("Paths");
    ImGui::TextWrapped("Overlay:   %%LocalAppDataLow%%\\OpenVR-Pair\\Logs\\facetracking_log.<ts>.txt");
    ImGui::TextWrapped("Driver:    %%LocalAppDataLow%%\\OpenVR-Pair\\Logs\\driver_log.<ts>.txt");
    ImGui::TextWrapped("Profiles:  %%LocalAppDataLow%%\\OpenVR-Pair\\profiles\\facetracking.json");
    ImGui::TextWrapped("Calib:     %%LocalAppDataLow%%\\OpenVR-Pair\\profiles\\facetracking_calib_<uuid>.json");
    ImGui::TextWrapped("Trust:     %%LocalAppDataLow%%\\OpenVR-Pair\\facetracking\\trust.json");

    (void)plugin; // profile_.current is not needed here at present
}

} // namespace facetracking::ui
