#pragma once

#include "Theme.h"

#include <imgui.h>
#include <string>

// Shared UI helpers for all overlay feature modules.
// Keeps control styling and interaction patterns consistent across Input Health,
// Smoothing, the calibration overlay, the face-tracking overlay, and the
// shell. Helpers that name a color route through GetPalette() so the active
// theme drives the actual hue.
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

// Same as TooltipForLastItem but reads better at call sites where the
// "last item" was a plain text element (TextUnformatted, TextWrapped) and
// the caller wants the tooltip to appear on hover of that text.
inline void TooltipOnHover(const char *tooltip)
{
	TooltipForLastItem(tooltip);
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

// Routes through ImGui::TextColored for now; existing as a stable call site
// so future passes can swap in semantic-palette routing without touching
// every feature module.
inline void DrawColoredText(const char *text, ImVec4 color)
{
	if (!text) return;
	ImGui::TextColored(color, "%s", text);
}

// Small filled circle aligned with the current text baseline. Reserves a
// Dummy(2r+4, lineH) so the next SameLine() call lines up with the dot.
// Promoted from ShellFooter.cpp so calibration's footer + future status
// rows can share the same dot dimensions.
inline void DrawStatusDot(ImU32 color)
{
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const float h = ImGui::GetTextLineHeight();
	const float r = h * 0.32f;
	const ImVec2 cursor = ImGui::GetCursorScreenPos();
	const ImVec2 center(cursor.x + r + 2.0f, cursor.y + h * 0.5f);
	dl->AddCircleFilled(center, r, color);
	ImGui::Dummy(ImVec2(r * 2.0f + 4.0f, h));
	ImGui::SameLine();
}

// Right-aligns `text` inside the current content region. Used by table
// cells that want a status word hugging the right edge regardless of the
// column's stretch width. If colored == false, falls back to
// TextDisabled to match the muted "Disabled" idiom main.cpp's Modules
// table used to hand-roll.
inline void RightAlignText(const char *text, ImVec4 color, bool colored = true)
{
	if (!text) return;
	const float colW = ImGui::GetContentRegionAvail().x;
	const float textW = ImGui::CalcTextSize(text).x;
	const float pad = (colW > textW) ? (colW - textW) : 0.0f;
	ImGui::Dummy(ImVec2(pad, 0));
	ImGui::SameLine(0, 0);
	if (colored) {
		ImGui::TextColored(color, "%s", text);
	} else {
		ImGui::TextDisabled("%s", text);
	}
}

// One-button "[Copy]" that writes `text` to the clipboard. `id` distinguishes
// multiple copy buttons on the same line (ImGui needs unique button ids).
inline bool CopyToClipboardButton(const char *id, const char *text)
{
	if (!id || !text) return false;
	ImGui::PushID(id);
	const bool clicked = ImGui::SmallButton("Copy");
	ImGui::PopID();
	if (clicked) {
		ImGui::SetClipboardText(text);
	}
	TooltipForLastItem("Copy this path to the clipboard");
	return clicked;
}

// Renders a file path that fits the current content region width, falling
// back to a middle-truncated form ("C:\Program Files\...\file.txt") when
// the full path is too long. Hovering the path shows the untruncated text
// in a tooltip so the user can still read it.
inline void DrawFilePath(const char *path)
{
	if (!path || !path[0]) return;
	const float avail = ImGui::GetContentRegionAvail().x;
	const float full = ImGui::CalcTextSize(path).x;
	if (avail <= 1.0f || full <= avail) {
		ImGui::TextUnformatted(path);
		TooltipOnHover(path);
		return;
	}
	// Middle-truncate: keep the leading drive/prefix and the trailing
	// filename so both are recognisable.
	std::string s(path);
	const std::string ellipsis = "...";
	while (s.size() > ellipsis.size() + 4) {
		const size_t mid = s.size() / 2;
		s.erase(mid - 1, 2);
		std::string candidate = s.substr(0, s.size() / 2) + ellipsis + s.substr(s.size() / 2);
		if (ImGui::CalcTextSize(candidate.c_str()).x <= avail) {
			ImGui::TextUnformatted(candidate.c_str());
			TooltipOnHover(path);
			return;
		}
	}
	ImGui::TextUnformatted(path);
	TooltipOnHover(path);
}

// RAII for a single push/pop pair. Replaces the 12+ scattered
// PushStyleColor/PopStyleColor pairs in calibration's tabs. Construct a
// local; the destructor pops on scope exit (including early return paths).
struct ScopedStyleColor
{
	ScopedStyleColor(ImGuiCol idx, ImVec4 color)
	{
		ImGui::PushStyleColor(idx, color);
	}
	~ScopedStyleColor() { ImGui::PopStyleColor(); }
	ScopedStyleColor(const ScopedStyleColor &) = delete;
	ScopedStyleColor &operator=(const ScopedStyleColor &) = delete;
};

// RAII for BeginDisabled/EndDisabled. When `whyTooltip` is non-null the
// tooltip is attached on hover of widgets drawn inside the disabled
// region, giving the user a reason instead of just an unresponsive
// control. Pass disabled=false to act as a no-op (still legal -- ImGui
// permits empty Begin/End pairs).
struct DisabledSection
{
	bool active;
	const char *why;
	DisabledSection(bool disabled, const char *whyTooltip = nullptr)
		: active(disabled), why(whyTooltip)
	{
		if (active) ImGui::BeginDisabled();
	}
	~DisabledSection()
	{
		if (active) ImGui::EndDisabled();
	}
	// Call after a widget inside the section to attach the "why" tooltip.
	void AttachReasonTooltip() const
	{
		if (active && why && why[0] && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
			ImGui::SetTooltip("%s", why);
		}
	}
	DisabledSection(const DisabledSection &) = delete;
	DisabledSection &operator=(const DisabledSection &) = delete;
};

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
	const SemanticPalette &pal = GetPalette();
	DrawBanner(title, detail, pal.bannerErrorBg, pal.bannerErrorTitle, pal.bannerErrorDetail);
}

inline void DrawWaitingBanner(const char *message)
{
	const SemanticPalette &pal = GetPalette();
	DrawBanner("Waiting", message, pal.bannerWarnBg, pal.bannerWarnTitle, pal.bannerWarnDetail);
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
