#include "TopBar.h"

#include "ShellContext.h"
#include "Theme.h"
#include "Widgets.h"

#include <imgui.h>

namespace openvr_pair::overlay {

void DrawTopBar(ShellContext &context,
	const char *title,
	const char *subtitle,
	bool driver_ok,
	bool ipc_ok,
	bool shmem_ok)
{
	const ui::Palette &p = ui::Colors();
	ImGui::PushStyleColor(ImGuiCol_ChildBg, p.bg);
	ImGui::BeginChild("##topbar", ImVec2(0.0f, 64.0f), 0,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	const ImVec2 win_pos = ImGui::GetWindowPos();
	const ImVec2 win_size = ImGui::GetWindowSize();
	ImDrawList *draw = ImGui::GetWindowDrawList();
	draw->AddLine(ImVec2(win_pos.x, win_pos.y + 63.0f),
		ImVec2(win_pos.x + win_size.x, win_pos.y + 63.0f),
		ui::ColorU32(p.border), 1.0f);

	ImGui::SetCursorPos(ImVec2(16.0f, subtitle && subtitle[0] ? 9.0f : 19.0f));
	if (context.fonts().heading) ImGui::PushFont(context.fonts().heading);
	ImGui::TextUnformatted(title ? title : "");
	if (context.fonts().heading) ImGui::PopFont();
	if (subtitle && subtitle[0]) {
		ImGui::SetCursorPos(ImVec2(16.0f, 38.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, p.text_muted);
		ImGui::TextUnformatted(subtitle);
		ImGui::PopStyleColor();
	}

	const float row_width = 66.0f * 3.0f + 8.0f * 2.0f;
	ImGui::SetCursorPos(ImVec2(win_size.x - row_width - 16.0f, 19.0f));
	ui::TopBarStatus("DRIVER", driver_ok);
	ImGui::SameLine(0.0f, 8.0f);
	ui::TopBarStatus("IPC", ipc_ok);
	ImGui::SameLine(0.0f, 8.0f);
	ui::TopBarStatus("SHMEM", shmem_ok);

	ImGui::EndChild();
	ImGui::PopStyleColor();
}

} // namespace openvr_pair::overlay
