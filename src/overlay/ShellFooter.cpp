#include "ShellFooter.h"

#include <imgui.h>

#ifndef OPENVR_PAIR_VERSION_STRING
#define OPENVR_PAIR_VERSION_STRING "0.0.0.0-dev"
#endif

namespace openvr_pair::overlay {
namespace {

// Small filled circle aligned with the current text baseline. Lifted from
// SC's DrawStatusDot so the shared footer reads the same way SC's own
// version line does. Reserves a Dummy of the dot's width so SameLine()
// after it lines up with the text.
void DrawStatusDot(ImU32 color)
{
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const float h = ImGui::GetTextLineHeight();
	const float r = h * 0.32f;
	ImVec2 cursor = ImGui::GetCursorScreenPos();
	ImVec2 center(cursor.x + r + 2.0f, cursor.y + h * 0.5f);
	dl->AddCircleFilled(center, r, color);
	ImGui::Dummy(ImVec2(r * 2.0f + 4.0f, h));
	ImGui::SameLine();
}

} // namespace

void DrawShellFooter(const ShellFooterStatus &status)
{
	const float lineH = ImGui::GetTextLineHeight();
	const float footerH = lineH + 8.0f;
	const float available = ImGui::GetContentRegionAvail().y;
	if (available > footerH) {
		ImGui::Dummy(ImVec2(0.0f, available - footerH));
	}
	ImGui::Separator();

	const char *label = status.driverLabel ? status.driverLabel : "Driver";
	const char *stamp = status.buildStamp ? status.buildStamp : OPENVR_PAIR_VERSION_STRING;

	if (status.driverConnected) {
		DrawStatusDot(IM_COL32(80, 200, 120, 255));
		ImGui::TextColored(ImVec4(0.5f, 0.85f, 0.55f, 1.0f), "%s: connected", label);
	} else {
		DrawStatusDot(IM_COL32(220, 170, 60, 255));
		ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.40f, 1.0f), "%s: waiting", label);
	}

	ImGui::SameLine();
	ImGui::Text("  |  OpenVR-Pair %s", stamp);
}

} // namespace openvr_pair::overlay
