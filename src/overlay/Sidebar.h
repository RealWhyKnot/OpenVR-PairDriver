#pragma once

#include <memory>
#include <vector>

namespace openvr_pair::overlay {

class FeaturePlugin;
struct ShellContext;

void DrawSidebar(ShellContext &context,
	std::vector<std::unique_ptr<FeaturePlugin>> &plugins,
	int &selected_plugin,
	bool &modules_selected);

} // namespace openvr_pair::overlay
