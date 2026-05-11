#pragma once

#include <imgui.h>

#include <string>

namespace openvr_pair::overlay {

struct FontSet
{
	ImFont *body = nullptr;
	ImFont *body_medium = nullptr;
	ImFont *heading = nullptr;
	ImFont *display = nullptr;
	ImFont *mono = nullptr;
};

FontSet LoadFonts(ImGuiIO &io, std::string *warning);

} // namespace openvr_pair::overlay
