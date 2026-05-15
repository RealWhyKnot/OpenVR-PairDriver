#include "OscRouterPlugin.h"
#include "DiscordPresenceComposer.h"
#include "ShellContext.h"

#include <memory>
#include <string>

void OscRouterPlugin::Tick(openvr_pair::overlay::ShellContext &ctx)
{
    tab_.Tick(ctx);
}

void OscRouterPlugin::DrawTab(openvr_pair::overlay::ShellContext &ctx)
{
    tab_.Draw(ctx);
}

void OscRouterPlugin::ProvidePresence(WKOpenVR::PresenceComposer &composer)
{
    const auto &s = tab_.LastStats();

    // Format packet counts: use "k" suffix when >= 1000 for readability.
    std::string pkts;
    if (s.packets_sent >= 1000) {
        pkts = std::to_string(s.packets_sent / 1000) + "k";
    } else {
        pkts = std::to_string(s.packets_sent);
    }

    std::string state = std::to_string(s.active_routes) + " routes | " + pkts + " packets";
    if (s.packets_dropped > 0) {
        state += " | " + std::to_string(s.packets_dropped) + " dropped";
    }

    WKOpenVR::PresenceUpdate u;
    u.priority = 40;
    u.details  = "Routing OSC";
    u.state    = std::move(state);

    composer.Submit("OSC Router", std::move(u));
}

namespace openvr_pair::overlay {

std::unique_ptr<FeaturePlugin> CreateOscRouterPlugin()
{
    return std::make_unique<OscRouterPlugin>();
}

} // namespace openvr_pair::overlay
