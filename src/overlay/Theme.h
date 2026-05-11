#pragma once

#include <imgui.h>

namespace openvr_pair::ui {

struct Palette
{
	ImVec4 bg;
	ImVec4 surface;
	ImVec4 surface_alt;
	ImVec4 surface_hover;
	ImVec4 text;
	ImVec4 text_muted;
	ImVec4 text_subtle;
	ImVec4 border;
	ImVec4 accent;
	ImVec4 accent_hover;
	ImVec4 success;
	ImVec4 warning;
	ImVec4 danger;
};

ImGuiStyle &Apply(ImGuiIO &io);
const Palette &Colors();

ImVec4 WithAlpha(ImVec4 color, float alpha);
ImU32 ColorU32(const ImVec4 &color, float alpha_scale = 1.0f);

} // namespace openvr_pair::ui
