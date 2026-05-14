#define _CRT_SECURE_NO_DEPRECATE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>

#include "ActionBindings.h"
#include "ChatboxPacer.h"
#include "HostStatus.h"
#include "Logging.h"
#include "ModelDownloader.h"
#include "RouterPublisher.h"
#include "SileroVad.h"
#include "Translator.h"
#include "WasapiCapture.h"
#include "WhisperEngine.h"

#include <openvr.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Config (updated at runtime via the host control pipe)
// ---------------------------------------------------------------------------

struct HostConfig
{
    std::string source_lang       = "auto";
    std::string target_lang       = "";           // empty = transcribe only
    std::string chatbox_address   = "/chatbox/input";
    uint16_t    chatbox_port      = 9000;
    bool        transcript_logging = false;
    int         mode              = 0;            // 0=PTT, 1=always-on

    // Paths: resolved once at startup. Overrideable via command-line.
    std::string whisper_model_path;
    std::string silero_model_path;
};

static std::mutex  g_config_mutex;
static HostConfig  g_config;

// ---------------------------------------------------------------------------
// Control pipe listener (receives config updates from the driver)
// ---------------------------------------------------------------------------

#define HOST_CONTROL_PIPE_NAME "\\\\.\\pipe\\WKOpenVR-Translator.host"

static std::atomic<bool> g_shutdown{ false };

static void ControlPipeThread()
{
    while (!g_shutdown.load(std::memory_order_acquire)) {
        HANDLE pipe = CreateNamedPipeA(
            HOST_CONTROL_PIPE_NAME,
            PIPE_ACCESS_INBOUND,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            0, 4096, 1000, nullptr);

        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(500);
            continue;
        }

        if (!ConnectNamedPipe(pipe, nullptr) &&
            GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(pipe);
            continue;
        }

        char buf[4096] = {};
        DWORD got = 0;
        if (ReadFile(pipe, buf, sizeof(buf) - 1, &got, nullptr) && got > 0) {
            buf[got] = '\0';
            std::string msg(buf, got);
            TH_LOG("[control] received: %s", msg.c_str());
            // Parse "config:src=XX,tgt=YY,mode=N,addr=ZZ,port=N,log=N"
            if (msg.rfind("config:", 0) == 0) {
                std::lock_guard<std::mutex> lk(g_config_mutex);
                // Simple key=value parser.
                std::string body = msg.substr(7);
                size_t pos = 0;
                while (pos < body.size()) {
                    size_t eq = body.find('=', pos);
                    if (eq == std::string::npos) break;
                    size_t comma = body.find(',', eq);
                    std::string key = body.substr(pos, eq - pos);
                    std::string val = body.substr(eq + 1,
                        comma == std::string::npos ? std::string::npos : comma - eq - 1);
                    // Trim trailing newline.
                    while (!val.empty() && (val.back() == '\n' || val.back() == '\r'))
                        val.pop_back();

                    if (key == "src") g_config.source_lang = val;
                    else if (key == "tgt") g_config.target_lang = val;
                    else if (key == "mode") g_config.mode = std::stoi(val);
                    else if (key == "addr") g_config.chatbox_address = val;
                    else if (key == "port") g_config.chatbox_port = (uint16_t)std::stoi(val);
                    else if (key == "log")  g_config.transcript_logging = (val != "0");

                    if (comma == std::string::npos) break;
                    pos = comma + 1;
                }
            }
        }

        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

// ---------------------------------------------------------------------------
// Default model paths
// ---------------------------------------------------------------------------

static std::string DefaultWhisperModelPath()
{
    std::string dir = ModelDownloader::DefaultModelDir();
    if (dir.empty()) return {};
    return dir + "\\ggml-base.bin";
}

static std::string DefaultSileroModelPath()
{
    // silero_vad.onnx lives in resources/ next to the host exe.
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string path(buf);
    auto sep = path.find_last_of("/\\");
    if (sep != std::string::npos) path = path.substr(0, sep);
    return path + "\\resources\\silero_vad.onnx";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char **argv)
try
{
    TranslatorHostOpenLogFile();
    TH_LOG("[startup] phase=logger-open");
    TH_LOG("[main] WKOpenVR.TranslatorHost starting");

    // Parse optional command-line overrides: --model <path> --silero <path>
    {
        std::lock_guard<std::mutex> lk(g_config_mutex);
        g_config.whisper_model_path = DefaultWhisperModelPath();
        g_config.silero_model_path  = DefaultSileroModelPath();
        for (int i = 1; i + 1 < argc; ++i) {
            if (strcmp(argv[i], "--model")  == 0) { g_config.whisper_model_path = argv[i+1]; ++i; }
            if (strcmp(argv[i], "--silero") == 0) { g_config.silero_model_path  = argv[i+1]; ++i; }
        }
    }

    TH_LOG("[startup] phase=opening-control-pipe");
    // Start control pipe thread.
    std::thread ctrl_thread(ControlPipeThread);

    // Initialise OpenVR (try; non-fatal if SteamVR isn't up yet).
    bool vr_ok = false;
    {
        vr::EVRInitError vr_err = vr::VRInitError_None;
        vr::VR_Init(&vr_err, vr::VRApplication_Background);
        vr_ok = (vr_err == vr::VRInitError_None);
        if (!vr_ok) TH_LOG("[main] VR_Init failed (%d); PTT will be unavailable", (int)vr_err);
    }

    TH_LOG("[startup] phase=initializing-vad");
    // Load Silero VAD.
    SileroVad vad;
    {
        std::string silero_path;
        {
            std::lock_guard<std::mutex> lk(g_config_mutex);
            silero_path = g_config.silero_model_path;
        }
        if (!vad.Load(silero_path)) {
            TH_LOG("[main] Silero VAD load failed; path='%s'", silero_path.c_str());
        }
    }

    TH_LOG("[startup] phase=initializing-translation");
    // Load Whisper.
    WhisperEngine whisper;
    {
        std::string model_path;
        {
            std::lock_guard<std::mutex> lk(g_config_mutex);
            model_path = g_config.whisper_model_path;
        }
        if (!whisper.Load(model_path)) {
            TH_LOG("[main] Whisper model load failed; path='%s'", model_path.c_str());
        }
    }

    // Resolve the translation model directory from the default model dir.
    // Convention: models are stored as ct2-opus-mt-<src>-<tgt>/ subdirectories
    // under the WKOpenVR models directory.
    auto ResolveTranslatorModelDir = [](const std::string &src, const std::string &tgt) -> std::string {
        std::string dir = ModelDownloader::DefaultModelDir();
        if (dir.empty() || tgt.empty()) return {};
        return dir + "\\ct2-opus-mt-" + src + "-" + tgt;
    };

    // Translation model loaded on demand when target_lang changes.
    Translator translator;
    std::string loaded_tgt_lang;

    // Action bindings for PTT.
    ActionBindings actions;
    if (vr_ok) {
        std::string manifest = ActionBindings::ResolveManifestPath();
        if (!actions.Register(manifest)) {
            TH_LOG("[main] PTT action binding failed; push-to-talk unavailable");
        }
    }

    // Router publisher.
    RouterPublisher publisher;

    // Chatbox pacer (1.2 s minimum gap).
    ChatboxPacer pacer(1.2);

    // Host status.
    HostStatus status;

    // VAD state machine.
    constexpr float kSpeechThreshold    = 0.5f;
    constexpr float kSilenceThreshold   = 0.35f;
    constexpr int   kSilenceFrames      = 20; // ~600 ms of silence
    int   silence_count = 0;
    bool  in_speech = false;
    bool  ptt_was_held = false;
    std::vector<float> speech_buf;

    TH_LOG("[startup] phase=initializing-audio-capture");
    // WASAPI capture: 30 ms chunks fed through a thread-safe queue.
    std::mutex              audio_mutex;
    std::vector<std::vector<float>> audio_queue;

    WasapiCapture capture;
    bool cap_ok = capture.Start([&](const float *pcm, size_t n) {
        std::lock_guard<std::mutex> lk(audio_mutex);
        audio_queue.push_back(std::vector<float>(pcm, pcm + n));
    });

    if (!cap_ok) TH_LOG("[main] WASAPI capture start failed");

    status.SetMicName(capture.DeviceName());
    status.SetState(HostStatus::State::Idle);

    TH_LOG("[startup] phase=running");

    // ---------------------------------------------------------------------------
    // Main loop
    // ---------------------------------------------------------------------------

    while (!g_shutdown.load(std::memory_order_acquire)) {
        // Poll PTT action.
        bool ptt_held = actions.Poll();

        // Drain captured audio frames.
        std::vector<std::vector<float>> frames;
        {
            std::lock_guard<std::mutex> lk(audio_mutex);
            frames.swap(audio_queue);
        }

        HostConfig cfg;
        {
            std::lock_guard<std::mutex> lk(g_config_mutex);
            cfg = g_config;
        }

        // Update whisper language hint.
        whisper.SetLanguage(cfg.source_lang == "auto" ? "" : cfg.source_lang);

        // Load translation model if target changed.
        if (cfg.target_lang != loaded_tgt_lang) {
            if (!cfg.target_lang.empty()) {
                std::string model_dir = ResolveTranslatorModelDir(
                    cfg.source_lang == "auto" ? "en" : cfg.source_lang,
                    cfg.target_lang);
                if (!model_dir.empty()) translator.Load(model_dir);
            } else {
                translator.Unload();
            }
            loaded_tgt_lang = cfg.target_lang;
        }

        const bool always_on = (cfg.mode == 1);

        for (const auto &frame : frames) {
            // VAD gate (PTT mode skips it).
            if (always_on && vad.IsLoaded()) {
                float prob = vad.Feed(frame.data(), frame.size());
                if (prob >= kSpeechThreshold) {
                    if (!in_speech) {
                        in_speech = true;
                        vad.Reset();
                        speech_buf.clear();
                        status.SetState(HostStatus::State::Listening);
                    }
                    silence_count = 0;
                } else if (in_speech) {
                    ++silence_count;
                    if (silence_count >= kSilenceFrames) {
                        in_speech = false;
                        status.SetState(HostStatus::State::Transcribing);
                        goto transcribe;
                    }
                }
                if (in_speech) {
                    speech_buf.insert(speech_buf.end(), frame.begin(), frame.end());
                }
                continue;
            }

            // PTT mode: collect while held, flush on release.
            if (!always_on) {
                if (ptt_held) {
                    if (!ptt_was_held) {
                        speech_buf.clear();
                        status.SetState(HostStatus::State::Listening);
                    }
                    speech_buf.insert(speech_buf.end(), frame.begin(), frame.end());
                    ptt_was_held = true;
                } else if (ptt_was_held) {
                    ptt_was_held = false;
                    status.SetState(HostStatus::State::Transcribing);
                    goto transcribe;
                }
            }
            continue;

            transcribe:
            if (speech_buf.size() < 1600) {
                // Less than 100 ms of audio -- too short to transcribe.
                speech_buf.clear();
                status.SetState(HostStatus::State::Idle);
                continue;
            }

            {
                std::string detected_lang;
                std::string transcript = whisper.Transcribe(speech_buf, &detected_lang);
                speech_buf.clear();
                status.SetLastTranscript(transcript);
                TH_LOG("[main] transcript (%s): %s", detected_lang.c_str(), transcript.c_str());

                // Translation step.
                std::string output = transcript;
                if (!cfg.target_lang.empty() && translator.IsLoaded()) {
                    status.SetState(HostStatus::State::Translating);
                    output = translator.Translate(transcript,
                        detected_lang.empty() ? cfg.source_lang : detected_lang,
                        cfg.target_lang);
                    status.SetLastTranslation(output);
                    TH_LOG("[main] translation: %s", output.c_str());
                }

                if (!output.empty()) {
                    pacer.Enqueue(output, true, cfg.chatbox_port != 0);
                }
                status.SetState(HostStatus::State::Idle);
            }
        }

        // Drain pacer and publish.
        ChatboxPacer::Entry entry;
        while (pacer.Dequeue(entry)) {
            status.SetState(HostStatus::State::Sending);
            bool sent = publisher.PublishChatbox(entry.text, entry.send_immediate, entry.notify);
            if (sent) {
                status.IncrementPacketsSent();
                TH_LOG("[main] published: '%s'", entry.text.c_str());
            }
            status.SetState(HostStatus::State::Idle);
        }

        status.SetMicName(capture.DeviceName());
        status.MaybeFlush();

        Sleep(10); // 10 ms main-loop cadence
    }

    TH_LOG("[main] shutting down");
    capture.Stop();
    status.SetState(HostStatus::State::Idle);
    status.Flush();

    if (vr_ok) vr::VR_Shutdown();

    g_shutdown.store(true, std::memory_order_release);
    if (ctrl_thread.joinable()) ctrl_thread.join();

    TranslatorHostFlushLog();
    return 0;
}
catch (const std::exception &e)
{
    TH_LOG("[crash] main threw std::exception: %s", e.what());
    TranslatorHostFlushLog();
    return 1;
}
catch (...)
{
    TH_LOG("[crash] main threw unknown exception");
    TranslatorHostFlushLog();
    return 1;
}
