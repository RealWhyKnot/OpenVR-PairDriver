#include "Widgets.h"

#include "Theme.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace openvr_pair::ui {
namespace {

ImVec4 StatusBase(Status status)
{
	const Palette &p = Colors();
	switch (status) {
		case Status::Learning: return p.accent;
		case Status::Ready: return p.success;
		case Status::Warning: return p.warning;
		case Status::Danger: return p.danger;
		case Status::Disabled: return p.text_muted;
		case Status::Idle:
		default: return p.text_subtle;
	}
}

ImVec4 StatusFill(Status status)
{
	ImVec4 c = StatusBase(status);
	c.w = status == Status::Disabled ? 0.0f : 0.16f;
	return c;
}

} // namespace

bool StatusPill(const char *label, Status s, bool clickable)
{
	const Palette &p = Colors();
	const ImVec2 text_size = ImGui::CalcTextSize(label);
	const ImVec2 size(std::max(54.0f, text_size.x + 22.0f), 26.0f);
	const ImVec2 pos = ImGui::GetCursorScreenPos();

	bool clicked = false;
	if (clickable) {
		clicked = ImGui::InvisibleButton(label, size);
	} else {
		ImGui::Dummy(size);
	}

	const bool hovered = clickable && ImGui::IsItemHovered();
	ImDrawList *draw = ImGui::GetWindowDrawList();
	const ImVec4 fill = hovered ? WithAlpha(StatusBase(s), 0.24f) : StatusFill(s);
	const ImVec4 border = s == Status::Disabled ? p.border : WithAlpha(StatusBase(s), 0.62f);
	const ImVec4 text = s == Status::Disabled ? p.text_muted : p.text;
	draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ColorU32(fill), size.y * 0.5f);
	draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), ColorU32(border), size.y * 0.5f);
	draw->AddText(ImVec2(pos.x + (size.x - text_size.x) * 0.5f, pos.y + (size.y - text_size.y) * 0.5f),
		ColorU32(text), label);
	return clicked;
}

void Card(const char *title, const char *subtitle, const std::function<void()> &body_fn)
{
	const Palette &p = Colors();
	ImGui::PushID(title ? title : "card");
	ImGui::PushStyleColor(ImGuiCol_ChildBg, p.surface);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);

	ImGui::BeginChild("##card", ImVec2(0.0f, 0.0f),
		ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	if (title && title[0]) {
		ImGui::TextUnformatted(title);
		if (subtitle && subtitle[0]) {
			ImGui::PushStyleColor(ImGuiCol_Text, p.text_muted);
			ImGui::TextWrapped("%s", subtitle);
			ImGui::PopStyleColor();
		}
		ImGui::Spacing();
	}
	if (body_fn) body_fn();
	ImGui::EndChild();

	const ImVec2 min = ImGui::GetItemRectMin();
	const ImVec2 max = ImGui::GetItemRectMax();
	ImGui::GetWindowDrawList()->AddRect(min, max, ColorU32(p.border), 8.0f);

	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();
	ImGui::PopID();
}

void SectionHeader(const char *label)
{
	const Palette &p = Colors();
	ImGui::Spacing();
	ImGui::PushStyleColor(ImGuiCol_Text, p.text_muted);
	ImGui::TextUnformatted(label);
	ImGui::PopStyleColor();
	const ImVec2 start = ImGui::GetCursorScreenPos();
	const float width = ImGui::GetContentRegionAvail().x;
	ImGui::GetWindowDrawList()->AddLine(start, ImVec2(start.x + width, start.y), ColorU32(p.border), 1.0f);
	ImGui::Dummy(ImVec2(width, 6.0f));
}

void Stat(const char *label, const char *value, Status accent)
{
	const Palette &p = Colors();
	const ImVec2 size(188.0f, 86.0f);
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton(label, size);

	ImDrawList *draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ColorU32(p.surface), 8.0f);
	draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), ColorU32(p.border), 8.0f);
	draw->AddCircleFilled(ImVec2(pos.x + 18.0f, pos.y + 22.0f), 4.0f, ColorU32(StatusBase(accent)));
	draw->AddText(ImVec2(pos.x + 30.0f, pos.y + 13.0f), ColorU32(p.text_muted), label);
	draw->AddText(ImVec2(pos.x + 18.0f, pos.y + 48.0f), ColorU32(p.text), value);
}

void ProgressBar(float frac, const char *text, Status state, float height)
{
	const Palette &p = Colors();
	frac = std::clamp(frac, 0.0f, 1.0f);
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const ImVec2 size(ImGui::GetContentRegionAvail().x, height);
	ImGui::Dummy(size);

	ImDrawList *draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ColorU32(p.surface_alt), height * 0.5f);
	if (frac > 0.0f) {
		draw->AddRectFilled(pos, ImVec2(pos.x + size.x * frac, pos.y + size.y), ColorU32(StatusBase(state)), height * 0.5f);
	}
	if (text && text[0]) {
		const ImVec2 text_size = ImGui::CalcTextSize(text);
		draw->AddText(ImVec2(pos.x + (size.x - text_size.x) * 0.5f, pos.y + (size.y - text_size.y) * 0.5f),
			ColorU32(p.text), text);
	}
}

void StatusDot(const char *label, Status s)
{
	const Palette &p = Colors();
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const ImVec2 text_size = ImGui::CalcTextSize(label);
	const float height = std::max(18.0f, text_size.y);
	ImGui::Dummy(ImVec2(14.0f + text_size.x, height));
	ImDrawList *draw = ImGui::GetWindowDrawList();
	draw->AddCircleFilled(ImVec2(pos.x + 5.0f, pos.y + height * 0.5f), 4.0f, ColorU32(StatusBase(s)));
	draw->AddText(ImVec2(pos.x + 14.0f, pos.y + (height - text_size.y) * 0.5f), ColorU32(p.text), label);
}

bool SidebarItem(const char *icon_glyph, const char *label, bool selected)
{
	const Palette &p = Colors();
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const ImVec2 size(ImGui::GetContentRegionAvail().x, 44.0f);
	const bool clicked = ImGui::InvisibleButton(label, size);
	const bool hovered = ImGui::IsItemHovered();

	ImVec4 bg = selected ? p.accent : (hovered ? p.surface_hover : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImVec4 fg = selected ? p.text : p.text_muted;
	ImDrawList *draw = ImGui::GetWindowDrawList();
	if (bg.w > 0.0f) {
		draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ColorU32(bg), 7.0f);
	}

	const char *icon = icon_glyph ? icon_glyph : "";
	const ImVec2 icon_size = ImGui::CalcTextSize(icon);
	draw->AddText(ImVec2(pos.x + 12.0f, pos.y + (size.y - icon_size.y) * 0.5f), ColorU32(fg), icon);
	draw->AddText(ImVec2(pos.x + 44.0f, pos.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f),
		ColorU32(selected ? p.text : p.text), label);
	return clicked;
}

void TopBarStatus(const char *label, bool ok)
{
	StatusPill(label, ok ? Status::Ready : Status::Danger, false);
}

bool ToggleSwitch(const char *label, bool *value)
{
	const Palette &p = Colors();
	const ImVec2 switch_size(42.0f, 24.0f);
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	bool changed = false;
	if (ImGui::InvisibleButton(label, switch_size)) {
		*value = !*value;
		changed = true;
	}
	const bool hovered = ImGui::IsItemHovered();
	const ImVec4 track = *value ? (hovered ? p.accent_hover : p.accent) : (hovered ? p.surface_hover : p.surface_alt);
	ImDrawList *draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled(pos, ImVec2(pos.x + switch_size.x, pos.y + switch_size.y), ColorU32(track), switch_size.y * 0.5f);
	draw->AddRect(pos, ImVec2(pos.x + switch_size.x, pos.y + switch_size.y), ColorU32(p.border), switch_size.y * 0.5f);
	const float knob_x = *value ? pos.x + switch_size.x - 12.0f : pos.x + 12.0f;
	draw->AddCircleFilled(ImVec2(knob_x, pos.y + switch_size.y * 0.5f), 8.0f, ColorU32(p.text));
	if (label && label[0] && label[0] != '#') {
		ImGui::SameLine(0.0f, 8.0f);
		ImGui::TextUnformatted(label);
	}
	return changed;
}

} // namespace openvr_pair::ui
