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

inline void DrawBanner(
	const char *title,
	const char *detail,
	ImVec4 background,
	ImVec4 titleColor,
	ImVec4 detailColor)
{
	const ImGuiStyle &style = ImGui::GetStyle();
	const float width = ImGui::GetContentRegionAvail().x;
	if (width <= 1.0f) return;

	const ImVec2 padding(12.0f, 8.0f);
	const float wrapWidth = width - padding.x * 2.0f;
	const bool hasTitle = title && title[0] != '\0';
	const bool hasDetail = detail && detail[0] != '\0';
	const float titleHeight = hasTitle
		? ImGui::CalcTextSize(title, nullptr, false, wrapWidth).y
		: 0.0f;
	const float detailHeight = hasDetail
		? ImGui::CalcTextSize(detail, nullptr, false, wrapWidth).y
		: 0.0f;
	const float gap = (hasTitle && hasDetail) ? style.ItemSpacing.y * 0.5f : 0.0f;
	const float height = padding.y * 2.0f + titleHeight + gap + detailHeight;

	const ImVec2 rectMin = ImGui::GetCursorScreenPos();
	const ImVec2 rectMax(rectMin.x + width, rectMin.y + height);
	ImDrawList *dl = ImGui::GetWindowDrawList();
	dl->AddRectFilled(rectMin, rectMax, ImGui::GetColorU32(background), 6.0f);
	dl->AddRect(rectMin, rectMax, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.12f)), 6.0f);

	ImGui::SetCursorScreenPos(ImVec2(rectMin.x + padding.x, rectMin.y + padding.y));
	ImGui::PushTextWrapPos(rectMax.x - padding.x);
	if (hasTitle) {
		ImGui::PushStyleColor(ImGuiCol_Text, titleColor);
		ImGui::TextWrapped("%s", title);
		ImGui::PopStyleColor();
	}
	if (hasDetail) {
		ImGui::PushStyleColor(ImGuiCol_Text, detailColor);
		ImGui::TextWrapped("%s", detail);
		ImGui::PopStyleColor();
	}
	ImGui::PopTextWrapPos();
	ImGui::SetCursorScreenPos(ImVec2(rectMin.x, rectMax.y));
	ImGui::Dummy(ImVec2(width, 0.0f));
}

inline void DrawErrorBanner(const char *title, const char *detail)
{
	DrawBanner(
		title,
		detail,
		ImVec4(0.42f, 0.06f, 0.06f, 1.0f),
		ImVec4(1.0f, 0.88f, 0.88f, 1.0f),
		ImVec4(1.0f, 0.96f, 0.96f, 1.0f));
}

inline void DrawWaitingBanner(const char *message)
{
	DrawBanner(
		"Waiting",
		message,
		ImVec4(0.45f, 0.33f, 0.08f, 1.0f),
		ImVec4(1.0f, 0.92f, 0.72f, 1.0f),
		ImVec4(1.0f, 0.96f, 0.86f, 1.0f));
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
