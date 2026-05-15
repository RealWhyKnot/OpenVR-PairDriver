#pragma once

#include "FeaturePlugin.h"
#include "HostStatusPoller.h"
#include "Protocol.h"
#include "TranslatorIpcClient.h"

#include <chrono>
#include <string>

class TranslatorPlugin;

namespace translator::ui {
void DrawTranslatorTab(TranslatorPlugin &plugin);
} // namespace translator::ui

class TranslatorPlugin final : public openvr_pair::overlay::FeaturePlugin
{
public:
    TranslatorPlugin();

    const char *Name()         const override { return "Translator"; }
    const char *FlagFileName() const override { return "enable_translator.flag"; }
    const char *PipeName()     const override { return OPENVR_PAIRDRIVER_TRANSLATOR_PIPE_NAME; }

    void OnStart(openvr_pair::overlay::ShellContext &ctx) override;
    void OnShutdown(openvr_pair::overlay::ShellContext &ctx) override;
    void Tick(openvr_pair::overlay::ShellContext &ctx) override;
    void DrawTab(openvr_pair::overlay::ShellContext &ctx) override;
    void ProvidePresence(WKOpenVR::PresenceComposer &composer) override;

    // Config push to driver.
    void PushConfigToDriver();

    // Send a restart-host request to the driver.
    void SendRestartHost();

    void InstallSpeechPack();
    void UninstallSpeechPack();
    void InstallTranslationPack();
    void UninstallTranslationPack();
    bool IsPackActionRunning() const { return pack_process_ != nullptr; }
    const std::string &PackActionStatus() const { return pack_status_; }
    std::string CurrentTranslationPackId() const;
    bool HasManagedTranslationPack() const;

    // Query the driver supervisor state and propagate host_halted to the
    // host status snapshot so the tab can show appropriate guidance.
    void PollSupervisorStatus();

    // Accessors for the UI helpers.
    translator::HostStatusPoller &HostStatus() { return host_status_; }

    int         GetMode()            const { return mode_; }
    void        SetMode(int m)             { mode_ = m; }
    bool        HasAlwaysOnConsent() const { return always_on_consented_; }
    void        SetAlwaysOnConsented(bool v) { always_on_consented_ = v; }

    const std::string &GetSourceLang()      const { return source_lang_; }
    const std::string &GetTargetLang()      const { return target_lang_; }
    const std::string &GetChatboxAddress()  const { return chatbox_address_; }
    bool               GetNotifySound()     const { return notify_sound_; }

    void SetSourceLang(const std::string &s)     { source_lang_    = s; }
    void SetTargetLang(const std::string &s)     { target_lang_    = s; }
    void SetChatboxAddress(const std::string &s) { chatbox_address_= s; }
    void SetNotifySound(bool v)                  { notify_sound_   = v; }

private:
    friend void translator::ui::DrawTranslatorTab(TranslatorPlugin &plugin);

    TranslatorIpcClient                  ipc_;
    translator::HostStatusPoller         host_status_;

    // Settings cached here and pushed to the driver.
    int         mode_                = 0;   // 0=PTT, 1=always-on
    bool        always_on_consented_ = false;
    std::string source_lang_         = "auto";
    std::string target_lang_         = "";
    std::string chatbox_address_     = "/chatbox/input";
    bool        notify_sound_        = false;

    std::string last_error_;
    std::string pack_status_;
    void       *pack_process_ = nullptr;
    std::wstring pack_script_path_;
    std::wstring pack_manifest_path_;
    uint64_t    observed_ipc_generation_ = 0;

    std::chrono::steady_clock::time_point last_connection_check_{};

    void MaintainDriverConnection();
    void DrawStatusBanner();
    void RefreshPackResourcePaths(const openvr_pair::overlay::ShellContext &ctx);
    void StartPackAction(const std::string &pack_id, bool uninstall);
    void PollPackAction();
};
