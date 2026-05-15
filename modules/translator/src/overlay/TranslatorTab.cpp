#include "TranslatorTab.h"
#include "TranslatorPlugin.h"
#include "HostStatusPoller.h"
#include "UiHelpers.h"
#include "Win32Paths.h"

// Win32Paths.h pulls in windows.h with WIN32_LEAN_AND_MEAN already set.
// imgui comes after to avoid any Windows header order conflicts.
#include <imgui/imgui.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Consent dialog state (per-session; persisted in the plugin's profile).
// ---------------------------------------------------------------------------

static bool s_consent_pending = false;

// ---------------------------------------------------------------------------
// Diagnostics: read last N lines from the newest translator log file.
// Rate-limited to at most once per 2 s to avoid constant directory scans.
// ---------------------------------------------------------------------------

namespace {

struct DiagState
{
    bool   expanded     = false;
    std::string log_tail;           // last 20 lines, joined with \n
    std::chrono::steady_clock::time_point last_refresh{};
    static constexpr int kLines = 20;
    static constexpr auto kRefreshInterval = std::chrono::seconds(2);
};

static DiagState s_diag;

// Find the newest file in dir matching prefix (wide strings).
static std::wstring FindNewestLogFile(const std::wstring &dir,
                                      const std::wstring &prefix)
{
    std::wstring pat = dir + L"\\" + prefix + L"*";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};

    std::wstring best_name;
    FILETIME     best_ft{};
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        // Compare by last-write time.
        if (best_name.empty() ||
            CompareFileTime(&fd.ftLastWriteTime, &best_ft) > 0) {
            best_name = fd.cFileName;
            best_ft   = fd.ftLastWriteTime;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    if (best_name.empty()) return {};
    return dir + L"\\" + best_name;
}

static void RefreshDiagnostics()
{
    auto now = std::chrono::steady_clock::now();
    if (now - s_diag.last_refresh < DiagState::kRefreshInterval) return;
    s_diag.last_refresh = now;

    std::wstring logs_dir = openvr_pair::common::WkOpenVrLogsPath(false);
    if (logs_dir.empty()) {
        s_diag.log_tail = "(could not resolve log directory)";
        return;
    }

    // Prefer the host log; also look for crash dumps.
    std::wstring host_log = FindNewestLogFile(logs_dir, L"translator_host_log.");
    std::wstring crash_log = FindNewestLogFile(logs_dir, L"translator_host_crash_");

    std::string tail;

    // Crash dump takes priority if newer than the host log.
    if (!crash_log.empty()) {
        std::ifstream cf(crash_log);
        if (cf) {
            std::ostringstream ss;
            ss << cf.rdbuf();
            tail = "--- crash dump ---\n" + ss.str();
        }
    }

    if (!host_log.empty()) {
        std::ifstream lf(host_log);
        if (lf) {
            std::vector<std::string> lines;
            std::string ln;
            while (std::getline(lf, ln)) lines.push_back(ln);
            int start = std::max(0, (int)lines.size() - DiagState::kLines);
            std::ostringstream ss;
            for (int i = start; i < (int)lines.size(); ++i) {
                ss << lines[i] << "\n";
            }
            if (!tail.empty()) tail += "\n--- host log (last 20 lines) ---\n";
            tail += ss.str();
        }
    }

    if (tail.empty()) tail = "(no log files found yet)";
    s_diag.log_tail = std::move(tail);
}

} // namespace

namespace translator::ui {

static const char *StateLabel(int state)
{
    switch (state) {
    case 0: return "Idle";
    case 1: return "Listening";
    case 2: return "Transcribing";
    case 3: return "Translating";
    case 4: return "Sending";
    case 5: return "Error";
    default: return "Unknown";
    }
}

void DrawTranslatorTab(TranslatorPlugin &plugin)
{
    const auto &snap = plugin.HostStatus().Snapshot();

    // -----------------------------------------------------------------------
    // Always-on consent modal
    // -----------------------------------------------------------------------
    if (s_consent_pending) {
        ImGui::OpenPopup("##tr_aon_consent");
        s_consent_pending = false;
    }

    if (ImGui::BeginPopupModal("##tr_aon_consent", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Mic capture active while WKOpenVR is running");
        ImGui::Separator();
        ImGui::TextWrapped(
            "Always-on mode captures audio from your microphone continuously\n"
            "and runs speech recognition locally on this machine.\n"
            "No audio or transcripts leave your PC.\n\n"
            "  - Audio is processed in memory and discarded after each sentence.\n"
            "  - Nothing is written to disk unless you enable transcript logging.\n"
            "  - All inference runs locally: whisper.cpp, Silero VAD, translation model.\n"
            "  - To stop capture: disable always-on in this tab or close WKOpenVR.");
        ImGui::Spacing();
        if (ImGui::Button("OK, enable always-on")) {
            plugin.SetAlwaysOnConsented(true);
            plugin.SetMode(1);
            plugin.PushConfigToDriver();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // -----------------------------------------------------------------------
    // Live status strip
    // -----------------------------------------------------------------------
    if (snap.valid) {
        ImGui::Text("Mic: %s  |  State: %s  |  Sent: %lld",
            snap.mic_name.empty() ? "(unknown)" : snap.mic_name.c_str(),
            StateLabel(snap.state),
            snap.packets_sent);
        if (!snap.last_transcript.empty())
            ImGui::Text("Transcript: %s", snap.last_transcript.c_str());
        if (!snap.last_translation.empty())
            ImGui::Text("Translation: %s", snap.last_translation.c_str());
        if (!snap.last_error.empty())
            openvr_pair::overlay::ui::DrawErrorBanner("Host error", snap.last_error.c_str());
    } else if (snap.host_halted) {
        openvr_pair::overlay::ui::DrawErrorBanner(
            "Translator host failed to start",
            "The host crashed or exited immediately on each of 5 consecutive\n"
            "launch attempts. The most likely cause is missing native runtime DLLs.\n"
            "\n"
            "CUDA runtime (if built with CUDA support):\n"
            "  Install NVIDIA CUDA Toolkit v13.2 and ensure its bin\\x64 folder\n"
            "  is on the system PATH, or rebuild with -DWKOPENVR_TRANSLATOR_CUDA=OFF.\n"
            "\n"
            "Inference libraries (ONNX Runtime / CTranslate2):\n"
            "  These were not vendored in this build. Without them the host starts\n"
            "  but VAD and translation are inert. If the host itself will not launch,\n"
            "  check that all DLLs from lib/onnxruntime and lib/ctranslate2 are\n"
            "  alongside WKOpenVR.TranslatorHost.exe in the install directory.\n"
            "\n"
            "Use the Restart host button below to retry after fixing the missing files.");

        // Diagnostics collapsible: shows log tail + crash dump on demand.
        // The driver translator log contains the DLL probe lines from Init().
        // The host crash dump (if any) names the specific DLL that failed.
        if (ImGui::TreeNode("Show diagnostics")) {
            s_diag.expanded = true;
            RefreshDiagnostics();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.75f, 0.75f, 1.f));
            ImGui::TextWrapped("Logs: %%LocalAppDataLow%%\\WKOpenVR\\Logs\\");
            ImGui::PopStyleColor();
            ImGui::Spacing();

            if (!s_diag.log_tail.empty()) {
                // InputTextMultiline gives a scrollable read-only text area.
                ImGui::InputTextMultiline("##diag_log",
                    const_cast<char *>(s_diag.log_tail.c_str()),
                    s_diag.log_tail.size() + 1,
                    ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 12),
                    ImGuiInputTextFlags_ReadOnly);
            }

            if (ImGui::Button("Refresh")) {
                s_diag.last_refresh = {};  // force re-read on next frame
            }
            ImGui::TreePop();
        } else {
            s_diag.expanded = false;
        }
    } else {
        ImGui::TextDisabled("Host not running");
    }

    ImGui::Separator();

    // -----------------------------------------------------------------------
    // Mode selection
    // -----------------------------------------------------------------------
    {
        int mode = plugin.GetMode();
        if (ImGui::RadioButton("Push-to-Talk", mode == 0)) {
            plugin.SetMode(0);
            plugin.PushConfigToDriver();
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Always-on", mode == 1)) {
            if (!plugin.HasAlwaysOnConsent()) {
                s_consent_pending = true;
            } else {
                plugin.SetMode(1);
                plugin.PushConfigToDriver();
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Always-on continuously captures your microphone while WKOpenVR is open.\nAudio is processed locally; nothing is transmitted.");
    }

    ImGui::Spacing();

    // -----------------------------------------------------------------------
    // Language dropdowns
    // -----------------------------------------------------------------------
    {
        static const char *kSrcLangs[] = {
            "auto", "en", "zh", "ja", "ko", "ru", "de", "fr", "es", "pt"
        };
        static const char *kTgtLangs[] = {
            "(transcribe only)", "en", "zh", "ja", "ko", "ru", "de", "fr", "es", "pt"
        };

        // Source.
        const std::string &src = plugin.GetSourceLang();
        int src_idx = 0;
        for (int i = 0; i < (int)(sizeof(kSrcLangs)/sizeof(kSrcLangs[0])); ++i) {
            if (src == kSrcLangs[i]) { src_idx = i; break; }
        }
        if (ImGui::BeginCombo("Source language", kSrcLangs[src_idx])) {
            for (int i = 0; i < (int)(sizeof(kSrcLangs)/sizeof(kSrcLangs[0])); ++i) {
                bool sel = (i == src_idx);
                if (ImGui::Selectable(kSrcLangs[i], sel)) {
                    plugin.SetSourceLang(kSrcLangs[i]);
                    plugin.PushConfigToDriver();
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("\"auto\" lets whisper.cpp detect the language per utterance.\nSetting an explicit code skips detection and saves ~50-100 ms per chunk.");

        // Target.
        const std::string &tgt = plugin.GetTargetLang();
        int tgt_idx = 0;
        for (int i = 0; i < (int)(sizeof(kTgtLangs)/sizeof(kTgtLangs[0])); ++i) {
            if (tgt == kTgtLangs[i]) { tgt_idx = i; break; }
        }
        if (ImGui::BeginCombo("Target language", kTgtLangs[tgt_idx])) {
            for (int i = 0; i < (int)(sizeof(kTgtLangs)/sizeof(kTgtLangs[0])); ++i) {
                bool sel = (i == tgt_idx);
                if (ImGui::Selectable(kTgtLangs[i], sel)) {
                    plugin.SetTargetLang(i == 0 ? "" : kTgtLangs[i]);
                    plugin.PushConfigToDriver();
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("\"transcribe only\" sends speech text without translation.\nPick a target to download an OPUS-MT checkpoint for that pair (~75-120 MB).");
    }

    ImGui::Spacing();

    // -----------------------------------------------------------------------
    // Chatbox settings
    // -----------------------------------------------------------------------
    {
        static const char *kPresets[] = { "VRChat", "Custom" };
        int preset = 0; // default to VRChat
        const std::string &addr = plugin.GetChatboxAddress();
        if (addr != "/chatbox/input") preset = 1;

        if (ImGui::BeginCombo("Game preset", kPresets[preset])) {
            if (ImGui::Selectable("VRChat", preset == 0)) {
                plugin.SetChatboxAddress("/chatbox/input");
                plugin.PushConfigToDriver();
            }
            if (ImGui::Selectable("Custom", preset == 1)) {
                preset = 1;
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("ChilloutVR's OSC spec is not publicly machine-readable; use Custom\nand verify the address from the CVR docs.");

        if (preset == 1) {
            char addr_buf[64];
            std::snprintf(addr_buf, sizeof(addr_buf), "%s", addr.c_str());
            if (ImGui::InputText("OSC address", addr_buf, sizeof(addr_buf))) {
                plugin.SetChatboxAddress(addr_buf);
                plugin.PushConfigToDriver();
            }
        }

        bool notify = plugin.GetNotifySound();
        if (ImGui::Checkbox("Notify listeners", &notify)) {
            plugin.SetNotifySound(notify);
            plugin.PushConfigToDriver();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Play the VRChat chatbox notification sound for each message.\nDefault off to avoid spamming nearby players.");
    }

    ImGui::Spacing();

    // -----------------------------------------------------------------------
    // Host controls
    // -----------------------------------------------------------------------
    if (ImGui::Button("Restart host")) {
        plugin.SendRestartHost();
        // Optimistically clear the halted indicator; PollSupervisorStatus will
        // confirm the new state on the next tick.
        plugin.HostStatus().SetHostHalted(false);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Terminate and respawn the translator sidecar process.\nUse when the host appears stuck or crashed.");
}

} // namespace translator::ui
