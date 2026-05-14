#include "RouterTab.h"
#include "ShellContext.h"
#include "UiHelpers.h"

#include <imgui.h>
#include <cstring>
#include <cstdio>

// ShellContext is forward-declared in FeaturePlugin.h; include the header
// so the definition is visible for field access.
#include "ShellContext.h"

void RouterTab::EnsureIpc()
{
    if (!ipc_.IsConnected() && !ipcConnectAttempted_) {
        ipcConnectAttempted_ = true;
        try { ipc_.Connect(); } catch (...) {}
    }
}

void RouterTab::Tick(openvr_pair::overlay::ShellContext &)
{
    statsReader_.TryOpen();
    if (statsReader_.IsOpen()) {
        statsReader_.ReadGlobal(lastStats_);
    }
}

void RouterTab::Draw(openvr_pair::overlay::ShellContext &ctx)
{
    (void)ctx;

    if (!statsReader_.IsOpen()) {
        openvr_pair::overlay::ui::DrawErrorBanner(
            "OSC Router not active",
            "The OSC Router feature is not enabled. "
            "Add enable_oscrouter.flag to the driver's resources folder "
            "and restart SteamVR.");
        return;
    }

    ImGui::Text("Send target: 127.0.0.1:9000");
    openvr_pair::overlay::ui::TooltipOnHover(
        "Default outbound OSC target. Configurable via RequestOscRouterSetConfig.");
    ImGui::Separator();

    ImGui::Text("Packets sent: %llu  Bytes: %llu  Dropped: %llu  Routes: %u",
        (unsigned long long)lastStats_.packets_sent,
        (unsigned long long)lastStats_.bytes_sent,
        (unsigned long long)lastStats_.packets_dropped,
        (unsigned)lastStats_.active_routes);

    ImGui::Separator();
    DrawRouteTable();
    ImGui::Separator();
    DrawTestPublish();
}

void RouterTab::DrawRouteTable()
{
    ImGui::Text("Active routes:");
    if (ImGui::BeginTable("routes", 3,
            ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Pattern",    ImGuiTableColumnFlags_WidthStretch, 5.0f);
        ImGui::TableSetupColumn("Matched",    ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("Dropped",    ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableHeadersRow();

        for (uint32_t i = 0; i < OscRouterStatsReader::RouteSlotCount(); ++i) {
            protocol::OscRouterRouteSlot slot;
            if (!statsReader_.ReadRoute(i, slot)) continue;
            if (!slot.active) continue;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(slot.address_pattern);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%llu", (unsigned long long)slot.match_count.load(std::memory_order_relaxed));
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%llu", (unsigned long long)slot.drop_count.load(std::memory_order_relaxed));
        }
        ImGui::EndTable();
    }
}

void RouterTab::DrawTestPublish()
{
    ImGui::Text("Test publish:");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    ImGui::InputText("Address##testaddr", testAddress_, sizeof(testAddress_));
    openvr_pair::overlay::ui::TooltipOnHover("OSC address to send (e.g. /avatar/parameters/JawOpen)");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.4f);
    ImGui::InputText("Value##testval", testValue_, sizeof(testValue_));
    openvr_pair::overlay::ui::TooltipOnHover("Float value to send as a ,f argument");
    ImGui::SameLine();
    if (ImGui::Button("Send##testsend")) {
        TrySendTestPublish();
    }
    if (testStatus_[0] != '\0') {
        ImGui::TextUnformatted(testStatus_);
    }
}

void RouterTab::TrySendTestPublish()
{
    testStatus_[0] = '\0';
    EnsureIpc();

    if (!ipc_.IsConnected()) {
        snprintf(testStatus_, sizeof(testStatus_),
            "Not connected to driver. Is OSC Router enabled?");
        return;
    }

    float fval = 0.0f;
    bool parsed = (sscanf(testValue_, "%f", &fval) == 1);
    if (!parsed) {
        snprintf(testStatus_, sizeof(testStatus_), "Could not parse value as float.");
        return;
    }

    // Encode as big-endian float.
    uint32_t bits;
    memcpy(&bits, &fval, 4);
    uint8_t argBytes[4];
    argBytes[0] = (uint8_t)(bits >> 24);
    argBytes[1] = (uint8_t)(bits >> 16);
    argBytes[2] = (uint8_t)(bits >> 8);
    argBytes[3] = (uint8_t)(bits);

    protocol::Request req;
    req.type = protocol::RequestOscPublish;
    memset(&req.oscPublish, 0, sizeof(req.oscPublish));
    {
        size_t n = 0;
        const char *src = testAddress_;
        for (; n < sizeof(req.oscPublish.address) - 1 && src[n]; ++n)
            req.oscPublish.address[n] = src[n];
        req.oscPublish.address[n] = '\0';
    }
    req.oscPublish.typetag[0] = ','; req.oscPublish.typetag[1] = 'f'; req.oscPublish.typetag[2] = '\0';
    req.oscPublish.arg_len = 4;
    memcpy(req.oscPublish.arg_bytes, argBytes, 4);

    try {
        protocol::Response resp = ipc_.SendBlocking(req);
        if (resp.type == protocol::ResponseSuccess) {
            snprintf(testStatus_, sizeof(testStatus_), "Sent %s = %.4f", testAddress_, fval);
        } else {
            snprintf(testStatus_, sizeof(testStatus_), "Driver rejected publish (queue full?)");
        }
    } catch (const std::exception &e) {
        snprintf(testStatus_, sizeof(testStatus_), "IPC error: %s", e.what());
        ipc_.Close();
        ipcConnectAttempted_ = false;
    }
}
