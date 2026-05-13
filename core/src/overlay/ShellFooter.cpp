#include "ShellFooter.h"

#include "Theme.h"
#include "UiHelpers.h"

#include <imgui.h>

#ifndef OPENVR_PAIR_VERSION_STRING
#define OPENVR_PAIR_VERSION_STRING "0.0.0.0-dev"
#endif

namespace openvr_pair::overlay {

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

	const ui::SemanticPalette &pal = ui::GetPalette();
	const ImU32  dotColor  = status.driverConnected ? pal.dotOk      : pal.dotPending;
	const ImVec4 textColor = status.driverConnected ? pal.statusOk   : pal.statusPending;
	const char  *state     = status.driverConnected ? "connected"    : "waiting";

	ui::DrawStatusDot(dotColor);
	ImGui::PushStyleColor(ImGuiCol_Text, textColor);
	// TextWrapped wraps at the right edge of the current content region, so
	// the "Driver: connected  |  WKOpenVR <stamp>" line reflows instead of
	// overflowing on a desktop-mode window that has been shrunk.
	ImGui::TextWrapped("%s: %s  |  WKOpenVR %s", label, state, stamp);
	ImGui::PopStyleColor();
}

} // namespace openvr_pair::overlay
