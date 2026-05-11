#include "ModulesTab.h"

#include "FeaturePlugin.h"
#include "ShellContext.h"
#include "Widgets.h"

#include <imgui.h>

#include <chrono>
#include <string>
#include <unordered_map>

namespace openvr_pair::overlay {
namespace {

using Clock = std::chrono::steady_clock;

std::unordered_map<std::string, Clock::time_point> g_pending_until;

bool IsPending(const char *flag)
{
	const auto it = g_pending_until.find(flag ? flag : "");
	if (it == g_pending_until.end()) return false;
	if (Clock::now() < it->second) return true;
	g_pending_until.erase(it);
	return false;
}

} // namespace

void DrawModulesTab(ShellContext &context, std::vector<std::unique_ptr<FeaturePlugin>> &plugins)
{
	for (auto &plugin_ptr : plugins) {
		FeaturePlugin &plugin = *plugin_ptr;
		ImGui::PushID(plugin.FlagFileName());
		ui::Card(plugin.Name(), plugin.Subtitle(), [&]() {
			const bool pending = IsPending(plugin.FlagFileName());
			bool enabled = plugin.IsInstalled(context);
			ImGui::BeginDisabled(pending);
			if (ui::ToggleSwitch("Enabled", &enabled)) {
				if (context.SetFlagPresent(plugin.FlagFileName(), enabled)) {
					g_pending_until[plugin.FlagFileName()] = Clock::now() + std::chrono::seconds(4);
				}
			}
			ImGui::EndDisabled();

			ImGui::SameLine();
			if (pending) {
				ImGui::TextDisabled("waiting for elevation");
			} else {
				ImGui::TextDisabled(enabled ? "flag present" : "flag missing");
			}

			std::string path = Narrow(context.FlagPath(plugin.FlagFileName()));
			ImGui::Spacing();
			ImGui::TextWrapped("%s", path.c_str());
		});
		ImGui::PopID();
	}
}

} // namespace openvr_pair::overlay
