#include "TranslatorPlugin.h"
#include "DiscordPresenceComposer.h"
#include "ShellContext.h"
#include "TranslatorIpcClient.h"
#include "TranslatorTab.h"
#include "UiHelpers.h"

#include <imgui/imgui.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <sstream>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;

namespace {

std::wstring WidenAscii(const std::string &value)
{
    return std::wstring(value.begin(), value.end());
}

std::wstring QuoteArg(const std::wstring &value)
{
    std::wstring out;
    out.reserve(value.size() + 2);
    out.push_back(L'"');
    unsigned backslashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(ch);
            backslashes = 0;
            continue;
        }
        if (backslashes) {
            out.append(backslashes, L'\\');
            backslashes = 0;
        }
        out.push_back(ch);
    }
    if (backslashes) {
        out.append(backslashes * 2, L'\\');
    }
    out.push_back(L'"');
    return out;
}

bool FileExists(const std::wstring &path)
{
    if (path.empty()) return false;
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES &&
           (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

} // namespace

TranslatorPlugin::TranslatorPlugin()
{
    observed_ipc_generation_ = ipc_.ConnectionGeneration();
}

void TranslatorPlugin::OnStart(openvr_pair::overlay::ShellContext &ctx)
{
    RefreshPackResourcePaths(ctx);

    try {
        ipc_.Connect();
        PushConfigToDriver();
    } catch (const std::exception &e) {
        last_error_ = std::string("Translator IPC: ") + e.what();
    }

    last_connection_check_ = Clock::now();
}

void TranslatorPlugin::OnShutdown(openvr_pair::overlay::ShellContext &)
{
    if (pack_process_) {
        CloseHandle(static_cast<HANDLE>(pack_process_));
        pack_process_ = nullptr;
    }
    ipc_.Close();
}

void TranslatorPlugin::Tick(openvr_pair::overlay::ShellContext &)
{
    PollPackAction();

    const auto now = Clock::now();
    if (now - last_connection_check_ >= std::chrono::seconds(1)) {
        MaintainDriverConnection();
        last_connection_check_ = now;
        PollSupervisorStatus();
    }
    host_status_.Tick();
}

void TranslatorPlugin::DrawTab(openvr_pair::overlay::ShellContext &ctx)
{
    RefreshPackResourcePaths(ctx);

    if (!ctx.IsFlagPresent("enable_oscrouter.flag")) {
        openvr_pair::overlay::ui::DrawErrorBanner(
            "OSC Router required",
            "This feature sends OSC to VRChat through the OSC Router. "
            "Enable OSC Router on the Modules tab so chatbox text reaches VRChat.");
    }
    DrawStatusBanner();
    translator::ui::DrawTranslatorTab(*this);
}

void TranslatorPlugin::PushConfigToDriver()
{
    if (!ipc_.IsConnected()) {
        last_error_ = "Not connected to the Translator driver.";
        return;
    }
    try {
        protocol::Request req(protocol::RequestSetTranslatorConfig);
        auto &cfg = req.setTranslatorConfig;
        memset(&cfg, 0, sizeof(cfg));

        cfg.master_enabled = 1;
        cfg.mode           = static_cast<uint8_t>(mode_);
        cfg.notify_sound   = notify_sound_ ? 1 : 0;
        cfg.chatbox_port   = 9000;

        std::snprintf(cfg.source_lang, sizeof(cfg.source_lang), "%s", source_lang_.c_str());
        std::snprintf(cfg.target_lang, sizeof(cfg.target_lang), "%s", target_lang_.c_str());
        std::snprintf(cfg.chatbox_address, sizeof(cfg.chatbox_address), "%s", chatbox_address_.c_str());

        auto resp = ipc_.SendBlocking(req);
        if (resp.type != protocol::ResponseSuccess) {
            last_error_ = "Driver rejected Translator config (type=" +
                          std::to_string(resp.type) + ")";
            return;
        }
        last_error_.clear();
    } catch (const std::exception &e) {
        last_error_ = std::string("IPC error: ") + e.what();
        ipc_.Close();
    }
}

void TranslatorPlugin::SendRestartHost()
{
    if (!ipc_.IsConnected()) return;
    try {
        protocol::Request req(protocol::RequestTranslatorRestartHost);
        ipc_.SendBlocking(req);
    } catch (...) {
        ipc_.Close();
    }
}

void TranslatorPlugin::InstallSpeechPack()
{
    StartPackAction("speech-base", false);
}

void TranslatorPlugin::UninstallSpeechPack()
{
    StartPackAction("speech-base", true);
}

void TranslatorPlugin::InstallTranslationPack()
{
    const std::string pack = CurrentTranslationPackId();
    if (pack.empty()) {
        pack_status_ = "No managed translation pack for the selected pair.";
        return;
    }
    StartPackAction(pack, false);
}

void TranslatorPlugin::UninstallTranslationPack()
{
    const std::string pack = CurrentTranslationPackId();
    if (pack.empty()) {
        pack_status_ = "No managed translation pack for the selected pair.";
        return;
    }
    StartPackAction(pack, true);
}

std::string TranslatorPlugin::CurrentTranslationPackId() const
{
    if (target_lang_.empty()) return {};

    const std::string src = (source_lang_.empty() || source_lang_ == "auto")
        ? "en"
        : source_lang_;
    if (src != "en" || target_lang_ == "en") return {};

    static const char *kManagedTargets[] = { "de", "es", "fr", "ru", "zh" };
    for (const char *target : kManagedTargets) {
        if (target_lang_ == target) {
            return std::string("translation-en-") + target_lang_;
        }
    }
    return {};
}

bool TranslatorPlugin::HasManagedTranslationPack() const
{
    return !CurrentTranslationPackId().empty();
}

void TranslatorPlugin::RefreshPackResourcePaths(const openvr_pair::overlay::ShellContext &ctx)
{
    if (ctx.driverResourceDirs.empty()) return;
    const std::wstring root = ctx.driverResourceDirs.front() +
        L"\\translator\\host\\resources";
    pack_script_path_ = root + L"\\install-translator-pack.ps1";
    pack_manifest_path_ = root + L"\\translator-packs.json";
}

void TranslatorPlugin::StartPackAction(const std::string &pack_id, bool uninstall)
{
    if (pack_process_) return;
    if (pack_id.empty()) {
        pack_status_ = "No translator pack selected.";
        return;
    }
    if (!FileExists(pack_script_path_)) {
        pack_status_ = "Translator pack installer is missing from the host resources.";
        return;
    }
    if (!FileExists(pack_manifest_path_)) {
        pack_status_ = "Translator pack manifest is missing from the host resources.";
        return;
    }

    std::wstring command = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File ";
    command += QuoteArg(pack_script_path_);
    command += L" -PackId ";
    command += QuoteArg(WidenAscii(pack_id));
    command += L" -Manifest ";
    command += QuoteArg(pack_manifest_path_);
    if (uninstall) command += L" -Uninstall";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmd(command.begin(), command.end());
    cmd.push_back(L'\0');

    BOOL ok = CreateProcessW(
        nullptr,
        cmd.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi);

    if (!ok) {
        std::ostringstream ss;
        ss << "Could not start translator pack installer (Win32 " << GetLastError() << ").";
        pack_status_ = ss.str();
        return;
    }

    CloseHandle(pi.hThread);
    pack_process_ = pi.hProcess;
    pack_status_ = uninstall ? "Uninstalling translator pack..." : "Installing translator pack...";
}

void TranslatorPlugin::PollPackAction()
{
    if (!pack_process_) return;

    HANDLE h = static_cast<HANDLE>(pack_process_);
    DWORD code = STILL_ACTIVE;
    if (!GetExitCodeProcess(h, &code) || code == STILL_ACTIVE) return;

    CloseHandle(h);
    pack_process_ = nullptr;

    if (code == 0) {
        pack_status_ = "Translator pack action completed.";
        SendRestartHost();
    } else {
        std::ostringstream ss;
        ss << "Translator pack action failed (exit " << code
           << "). See translator_pack_install.log.";
        pack_status_ = ss.str();
    }
}

void TranslatorPlugin::PollSupervisorStatus()
{
    if (!ipc_.IsConnected()) return;
    try {
        auto resp = ipc_.SendBlocking(
            protocol::Request(protocol::RequestTranslatorGetSupervisorStatus));
        if (resp.type == protocol::ResponseTranslatorSupervisorStatus) {
            host_status_.SetSupervisorStatus(
                resp.translatorSupervisorStatus.host_halted != 0,
                resp.translatorSupervisorStatus.last_exit_code,
                resp.translatorSupervisorStatus.last_exit_description);
        }
    } catch (...) {
        // Non-fatal; host_halted remains at its last known value.
    }
}

void TranslatorPlugin::MaintainDriverConnection()
{
    try {
        if (!ipc_.IsConnected()) {
            ipc_.Connect();
        }

        auto resp = ipc_.SendBlocking(
            protocol::Request(protocol::RequestHandshake));
        if (resp.type != protocol::ResponseHandshake ||
            resp.protocol.version != protocol::Version) {
            last_error_ = "Translator driver protocol mismatch";
            return;
        }

        const uint64_t gen = ipc_.ConnectionGeneration();
        if (gen != observed_ipc_generation_) {
            observed_ipc_generation_ = gen;
            PushConfigToDriver();
        }

        if (last_error_.find("Translator IPC") == 0 ||
            last_error_.find("Not connected")  == 0 ||
            last_error_.find("Driver connection:") == 0) {
            last_error_.clear();
        }
    } catch (const std::exception &e) {
        last_error_ = std::string("Driver connection: ") + e.what();
        ipc_.Close();
    }
}

void TranslatorPlugin::DrawStatusBanner()
{
    if (!last_error_.empty()) {
        openvr_pair::overlay::ui::DrawErrorBanner(
            "Translator driver problem", last_error_.c_str());
    }
}

void TranslatorPlugin::ProvidePresence(WKOpenVR::PresenceComposer &composer)
{
    const auto &snap = host_status_.Snapshot();

    // Map HostStatus::State int to a short display label.
    // 0=Idle 1=Listening 2=Transcribing 3=Translating 4=Sending
    static const char *const kStateLabels[] = {
        "idle", "listening", "transcribing", "translating", "sending"
    };
    const int stateIdx = snap.state;
    const char *stateLabel = (stateIdx >= 0 && stateIdx < 5)
        ? kStateLabels[stateIdx]
        : "idle";

    std::string state = std::string(stateLabel) +
                        " | " + std::to_string(snap.packets_sent) + " sent";

    WKOpenVR::PresenceUpdate u;
    u.priority = 50;
    u.details  = "Local speech pipeline";
    u.state    = std::move(state);

    composer.Submit("Translator", std::move(u));
}

namespace openvr_pair::overlay {

std::unique_ptr<FeaturePlugin> CreateTranslatorPlugin()
{
    return std::make_unique<TranslatorPlugin>();
}

} // namespace openvr_pair::overlay
