#pragma once

#include <memory>
#include <vector>

namespace openvr_pair::overlay {

class FeaturePlugin;
struct ShellContext;

void DrawModulesTab(ShellContext &context, std::vector<std::unique_ptr<FeaturePlugin>> &plugins);

} // namespace openvr_pair::overlay
