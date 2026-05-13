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
	// Reserve two text lines so the footer still has room when the composed
	// status string wraps on a narrow window. Single-line case leaves the
	// second line as a small gap below the text.
	const float footerH = lineH * 2.0f + 8.0f;
	const float available = ImGui::GetContentRegionAvail().y;
	if (available > footerH) {
		ImGui::Dummy(ImVec2(0.0f, available - footerH));
	}
	ImGui::Separator();

	const char *label = status.driverLabel ? status.driverLabel : "Driver";
	const char *stamp = status.buildStamp ? status.buildStamp : OPENVR_PAIR_VERSION_STRING;

	const ImU32  dotColor  = status.driverConnected ? IM_COL32(80, 200, 120, 255) : IM_COL32(220, 170, 60, 255);
	const ImVec4 textColor = status.driverConnected ? ImVec4(0.5f, 0.85f, 0.55f, 1.0f)
	                                                : ImVec4(0.95f, 0.80f, 0.40f, 1.0f);
	const char  *state     = status.driverConnected ? "connected" : "waiting";

	DrawStatusDot(dotColor);
	ImGui::PushStyleColor(ImGuiCol_Text, textColor);
	// TextWrapped wraps at the right edge of the current content region, so
	// the "Driver: connected  |  WKOpenVR <stamp>" line reflows instead of
	// overflowing on a desktop-mode window that has been shrunk.
	ImGui::TextWrapped("%s: %s  |  WKOpenVR %s", label, state, stamp);
	ImGui::PopStyleColor();
}

} // namespace openvr_pair::overlay
