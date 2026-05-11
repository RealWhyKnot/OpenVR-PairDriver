#include "Theme.h"

namespace openvr_pair::ui {
namespace {

constexpr float C(int value)
{
	return static_cast<float>(value) / 255.0f;
}

Palette MakePalette()
{
	return {
		ImVec4(C(0x1a), C(0x1d), C(0x23), 1.0f),
		ImVec4(C(0x23), C(0x27), C(0x30), 1.0f),
		ImVec4(C(0x1f), C(0x22), C(0x29), 1.0f),
		ImVec4(C(0x27), C(0x2b), C(0x34), 1.0f),
		ImVec4(C(0xf0), C(0xf4), C(0xf8), 1.0f),
		ImVec4(C(0x9a), C(0xa3), C(0xaf), 1.0f),
		ImVec4(C(0x6b), C(0x72), C(0x80), 1.0f),
		ImVec4(C(0x2d), C(0x33), C(0x40), 1.0f),
		ImVec4(C(0x2d), C(0x5c), C(0xd6), 1.0f),
		ImVec4(C(0x4a), C(0x73), C(0xe8), 1.0f),
		ImVec4(C(0x4a), C(0xde), C(0x80), 1.0f),
		ImVec4(C(0xfb), C(0xbf), C(0x24), 1.0f),
		ImVec4(C(0xf8), C(0x71), C(0x71), 1.0f),
	};
}

} // namespace

const Palette &Colors()
{
	static const Palette palette = MakePalette();
	return palette;
}

ImVec4 WithAlpha(ImVec4 color, float alpha)
{
	color.w = alpha;
	return color;
}

ImU32 ColorU32(const ImVec4 &color, float alpha_scale)
{
	ImVec4 c = color;
	c.w *= alpha_scale;
	return ImGui::ColorConvertFloat4ToU32(c);
}

ImGuiStyle &Apply(ImGuiIO &)
{
	const Palette &p = Colors();
	ImGuiStyle &style = ImGui::GetStyle();

	style.WindowPadding = ImVec2(0.0f, 0.0f);
	style.FramePadding = ImVec2(12.0f, 8.0f);
	style.ItemSpacing = ImVec2(10.0f, 10.0f);
	style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
	style.IndentSpacing = 18.0f;
	style.ScrollbarSize = 12.0f;
	style.GrabMinSize = 14.0f;
	style.WindowRounding = 0.0f;
	style.ChildRounding = 8.0f;
	style.FrameRounding = 6.0f;
	style.GrabRounding = 6.0f;
	style.TabRounding = 4.0f;
	style.ScrollbarRounding = 6.0f;
	style.WindowBorderSize = 0.0f;
	style.ChildBorderSize = 0.0f;
	style.PopupRounding = 8.0f;
	style.PopupBorderSize = 0.0f;
	style.FrameBorderSize = 0.0f;
	style.TabBorderSize = 0.0f;
	style.TabBarBorderSize = 0.0f;
	style.WindowMenuButtonPosition = ImGuiDir_None;
	style.CellPadding = ImVec2(10.0f, 8.0f);

	for (int i = 0; i < ImGuiCol_COUNT; ++i) {
		style.Colors[i] = p.bg;
	}

	style.Colors[ImGuiCol_Text] = p.text;
	style.Colors[ImGuiCol_TextDisabled] = p.text_muted;
	style.Colors[ImGuiCol_WindowBg] = p.bg;
	style.Colors[ImGuiCol_ChildBg] = p.surface;
	style.Colors[ImGuiCol_PopupBg] = p.surface;
	style.Colors[ImGuiCol_Border] = p.border;
	style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	style.Colors[ImGuiCol_FrameBg] = p.surface_alt;
	style.Colors[ImGuiCol_FrameBgHovered] = p.surface_hover;
	style.Colors[ImGuiCol_FrameBgActive] = WithAlpha(p.accent, 0.4f);
	style.Colors[ImGuiCol_TitleBg] = p.surface_alt;
	style.Colors[ImGuiCol_TitleBgActive] = p.surface_alt;
	style.Colors[ImGuiCol_TitleBgCollapsed] = p.surface_alt;
	style.Colors[ImGuiCol_MenuBarBg] = p.surface_alt;
	style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	style.Colors[ImGuiCol_ScrollbarGrab] = p.border;
	style.Colors[ImGuiCol_ScrollbarGrabHovered] = p.text_muted;
	style.Colors[ImGuiCol_ScrollbarGrabActive] = p.text_subtle;
	style.Colors[ImGuiCol_CheckMark] = p.accent;
	style.Colors[ImGuiCol_SliderGrab] = p.accent;
	style.Colors[ImGuiCol_SliderGrabActive] = p.accent_hover;
	style.Colors[ImGuiCol_Button] = p.surface_alt;
	style.Colors[ImGuiCol_ButtonHovered] = p.surface_hover;
	style.Colors[ImGuiCol_ButtonActive] = p.accent;
	style.Colors[ImGuiCol_Header] = p.surface_alt;
	style.Colors[ImGuiCol_HeaderHovered] = p.surface_hover;
	style.Colors[ImGuiCol_HeaderActive] = p.accent;
	style.Colors[ImGuiCol_Separator] = p.border;
	style.Colors[ImGuiCol_SeparatorHovered] = p.border;
	style.Colors[ImGuiCol_SeparatorActive] = p.accent;
	style.Colors[ImGuiCol_ResizeGrip] = p.border;
	style.Colors[ImGuiCol_ResizeGripHovered] = p.text_muted;
	style.Colors[ImGuiCol_ResizeGripActive] = p.accent;
	style.Colors[ImGuiCol_Tab] = p.surface_alt;
	style.Colors[ImGuiCol_TabHovered] = p.surface_hover;
	style.Colors[ImGuiCol_TabSelected] = p.surface_hover;
	style.Colors[ImGuiCol_TabSelectedOverline] = p.accent;
	style.Colors[ImGuiCol_TabDimmed] = p.surface_alt;
	style.Colors[ImGuiCol_TabDimmedSelected] = p.surface_hover;
	style.Colors[ImGuiCol_TabDimmedSelectedOverline] = p.accent;
	style.Colors[ImGuiCol_PlotLines] = p.accent;
	style.Colors[ImGuiCol_PlotLinesHovered] = p.accent_hover;
	style.Colors[ImGuiCol_PlotHistogram] = p.accent;
	style.Colors[ImGuiCol_PlotHistogramHovered] = p.accent_hover;
	style.Colors[ImGuiCol_TableHeaderBg] = p.surface_alt;
	style.Colors[ImGuiCol_TableBorderStrong] = p.border;
	style.Colors[ImGuiCol_TableBorderLight] = p.border;
	style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	style.Colors[ImGuiCol_TableRowBgAlt] = WithAlpha(p.surface, 0.3f);
	style.Colors[ImGuiCol_TextLink] = p.accent_hover;
	style.Colors[ImGuiCol_TextSelectedBg] = WithAlpha(p.accent, 0.5f);
	style.Colors[ImGuiCol_DragDropTarget] = p.accent;
	style.Colors[ImGuiCol_NavCursor] = p.accent;
	style.Colors[ImGuiCol_NavWindowingHighlight] = p.accent;
	style.Colors[ImGuiCol_NavWindowingDimBg] = WithAlpha(p.bg, 0.7f);
	style.Colors[ImGuiCol_ModalWindowDimBg] = WithAlpha(p.bg, 0.7f);

	return style;
}

} // namespace openvr_pair::ui
