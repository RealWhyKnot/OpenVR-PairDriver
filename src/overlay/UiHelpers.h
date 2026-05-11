#pragma once

#include <imgui.h>
#include <string>

// Shared UI helpers for all overlay feature modules.
// Keeps control styling and interaction patterns consistent across Input Health,
// Smoothing, and the shell module table.
namespace openvr_pair::overlay::ui {

inline void ApplyOverlayStyle()
{
	ImGuiStyle &style = ImGui::GetStyle();

	// Keep the look coherent and compact without over-styling.
	style.WindowPadding = ImVec2(12.0f, 10.0f);
	style.FramePadding = ImVec2(8.0f, 4.0f);
	style.ItemSpacing = ImVec2(10.0f, 6.0f);
	style.ItemInnerSpacing = ImVec2(8.0f, 4.0f);
	style.IndentSpacing = 20.0f;
	style.ScrollbarSize = 14.0f;
	style.TabRounding = 4.0f;
	style.GrabRounding = 4.0f;
}

inline void TooltipForLastItem(const char *tooltip)
{
	if (tooltip && tooltip[0] != '\0' && ImGui::IsItemHovered()) {
		ImGui::SetTooltip("%s", tooltip);
	}
}

inline bool CheckboxWithTooltip(const char *label, bool *value, const char *tooltip)
{
	const bool changed = ImGui::Checkbox(label, value);
	TooltipForLastItem(tooltip);
	return changed;
}

inline bool SliderIntWithTooltip(
	const char *label, int *value, int min, int max, const char *format, const char *tooltip)
{
	const bool changed = ImGui::SliderInt(label, value, min, max, format);
	TooltipForLastItem(tooltip);
	return changed;
}

inline void DrawHelpMarker(const char *tooltip)
{
	if (!tooltip || tooltip[0] == '\0') return;
	ImGui::TextDisabled("(?)");
	TooltipForLastItem(tooltip);
}

inline void DrawTextWrapped(const char *text)
{
	if (!text) return;
	ImGui::TextWrapped("%s", text);
}

inline void DrawTextWrapped(const std::string &text)
{
	DrawTextWrapped(text.c_str());
}

// Section heading: spacing + SeparatorText. Lets feature tabs stop hand-rolling
// the "Spacing(); Spacing(); SeparatorText(name);" idiom so headings look the
// same everywhere.
inline void DrawSectionHeading(const char *label)
{
	if (!label) return;
	ImGui::Spacing();
	ImGui::Spacing();
	ImGui::SeparatorText(label);
}

} // namespace openvr_pair::overlay::ui
