#include "OscRouterPlugin.h"
#include "ShellContext.h"

#include <memory>

void OscRouterPlugin::Tick(openvr_pair::overlay::ShellContext &ctx)
{
    tab_.Tick(ctx);
}

void OscRouterPlugin::DrawTab(openvr_pair::overlay::ShellContext &ctx)
{
    tab_.Draw(ctx);
}

namespace openvr_pair::overlay {

std::unique_ptr<FeaturePlugin> CreateOscRouterPlugin()
{
    return std::make_unique<OscRouterPlugin>();
}

} // namespace openvr_pair::overlay
