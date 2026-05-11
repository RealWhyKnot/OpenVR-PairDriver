#include "FeaturePlugin.h"
#include "ShellContext.h"

namespace openvr_pair::overlay {

bool FeaturePlugin::IsInstalled(ShellContext &context) const
{
	return context.IsFlagPresent(FlagFileName());
}

bool FeaturePlugin::DriverStatusOk(ShellContext &context) const
{
	return IsInstalled(context);
}

} // namespace openvr_pair::overlay
