#include "TranslatorPlugin.h"
#include "ShellContext.h"
#include "TranslatorIpcClient.h"
#include "TranslatorTab.h"
#include "UiHelpers.h"

#include <imgui/imgui.h>

#include <chrono>
#include <cstring>
#include <exception>
#include <string>

using Clock = std::chrono::steady_clock;

TranslatorPlugin::TranslatorPlugin()
{
    observed_ipc_generation_ = ipc_.ConnectionGeneration();
}

void TranslatorPlugin::OnStart(openvr_pair::overlay::ShellContext &)
{
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
    ipc_.Close();
}

void TranslatorPlugin::Tick(openvr_pair::overlay::ShellContext &)
{
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

        strncpy(cfg.source_lang, source_lang_.c_str(),
            sizeof(cfg.source_lang) - 1);
        strncpy(cfg.target_lang, target_lang_.c_str(),
            sizeof(cfg.target_lang) - 1);
        strncpy(cfg.chatbox_address, chatbox_address_.c_str(),
            sizeof(cfg.chatbox_address) - 1);
        cfg.chatbox_address[sizeof(cfg.chatbox_address) - 1] = '\0';

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

void TranslatorPlugin::PollSupervisorStatus()
{
    if (!ipc_.IsConnected()) return;
    try {
        auto resp = ipc_.SendBlocking(
            protocol::Request(protocol::RequestTranslatorGetSupervisorStatus));
        if (resp.type == protocol::ResponseTranslatorSupervisorStatus) {
            host_status_.SetHostHalted(resp.translatorSupervisorStatus.host_halted != 0);
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

namespace openvr_pair::overlay {

std::unique_ptr<FeaturePlugin> CreateTranslatorPlugin()
{
    return std::make_unique<TranslatorPlugin>();
}

} // namespace openvr_pair::overlay
