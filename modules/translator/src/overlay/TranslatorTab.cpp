#include "TranslatorTab.h"
#include "TranslatorPlugin.h"
#include "HostStatusPoller.h"
#include "UiHelpers.h"

#include <imgui/imgui.h>

#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// Consent dialog state (per-session; persisted in the plugin's profile).
// ---------------------------------------------------------------------------

static bool s_consent_pending = false;

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
            strncpy(addr_buf, addr.c_str(), sizeof(addr_buf) - 1);
            addr_buf[sizeof(addr_buf) - 1] = '\0';
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
