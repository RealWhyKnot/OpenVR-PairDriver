#include "Sidebar.h"

#include "FeaturePlugin.h"
#include "Icons.h"
#include "ShellContext.h"
#include "Theme.h"
#include "Widgets.h"

#include <imgui.h>

namespace openvr_pair::overlay {

void DrawSidebar(ShellContext &context,
	std::vector<std::unique_ptr<FeaturePlugin>> &plugins,
	int &selected_plugin,
	bool &modules_selected)
{
	const ui::Palette &p = ui::Colors();
	ImGui::PushStyleColor(ImGuiCol_ChildBg, p.surface_alt);
	ImGui::BeginChild("##sidebar", ImVec2(240.0f, 0.0f),
		ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	ImGui::SetCursorPos(ImVec2(20.0f, 24.0f));
	if (context.fonts().heading) ImGui::PushFont(context.fonts().heading);
	ImGui::TextUnformatted("OpenVR-Pair");
	if (context.fonts().heading) ImGui::PopFont();
	ImGui::SetCursorPosX(20.0f);
	ImGui::PushStyleColor(ImGuiCol_Text, p.text_muted);
	ImGui::TextUnformatted("vr companion");
	ImGui::PopStyleColor();

	ImGui::SetCursorPosY(96.0f);
	if (context.fonts().body_medium) ImGui::PushFont(context.fonts().body_medium);
	for (int i = 0; i < static_cast<int>(plugins.size()); ++i) {
		FeaturePlugin &plugin = *plugins[static_cast<size_t>(i)];
		if (!plugin.IsInstalled(context)) continue;
		ImGui::SetCursorPosX(8.0f);
		ImGui::PushID(plugin.FlagFileName());
		if (ui::SidebarItem(plugin.IconGlyph(), plugin.Name(), !modules_selected && selected_plugin == i)) {
			selected_plugin = i;
			modules_selected = false;
		}
		ImGui::PopID();
	}

	const float bottom_y = ImGui::GetWindowHeight() - 64.0f;
	if (ImGui::GetCursorPosY() < bottom_y) ImGui::SetCursorPosY(bottom_y);
	ImGui::SetCursorPosX(16.0f);
	ImGui::Separator();
	ImGui::SetCursorPosX(8.0f);
	if (ui::SidebarItem(ICON_PAIR_MODULES, "Modules", modules_selected)) {
		modules_selected = true;
	}
	if (context.fonts().body_medium) ImGui::PopFont();

	ImGui::EndChild();
	ImGui::PopStyleColor();
}

} // namespace openvr_pair::overlay
