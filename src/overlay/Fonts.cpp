#include "Fonts.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace openvr_pair::overlay {
namespace {

std::filesystem::path ExeDir()
{
	char path[MAX_PATH] = {};
	const DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
	if (len == 0 || len >= MAX_PATH) return std::filesystem::current_path();
	return std::filesystem::path(path).parent_path();
}

std::filesystem::path FindFont(const char *name)
{
	const std::filesystem::path exe_dir = ExeDir();
	const std::vector<std::filesystem::path> candidates = {
		exe_dir / name,
		exe_dir / "lib" / "fonts" / name,
		std::filesystem::current_path() / "lib" / "fonts" / name,
	};
	for (const auto &candidate : candidates) {
		if (std::filesystem::exists(candidate)) return candidate;
	}
	return {};
}

void AppendMissing(std::string *warning, const char *name)
{
	if (!warning) return;
	if (!warning->empty()) warning->append(", ");
	warning->append(name);
}

void MergeIcons(ImGuiIO &io, const std::filesystem::path &icon_path, float size)
{
	if (icon_path.empty()) return;
	static const ImWchar ranges[] = { 0xE000, 0xF8FF, 0 };
	ImFontConfig cfg;
	cfg.MergeMode = true;
	cfg.PixelSnapH = true;
	cfg.GlyphMinAdvanceX = size;
	cfg.GlyphOffset.y = 1.0f;
	io.Fonts->AddFontFromFileTTF(icon_path.string().c_str(), size, &cfg, ranges);
}

ImFont *AddFont(ImGuiIO &io,
	const std::filesystem::path &font_path,
	const std::filesystem::path &icon_path,
	float size,
	const char *missing_name,
	std::string *warning)
{
	if (!font_path.empty()) {
		ImFont *font = io.Fonts->AddFontFromFileTTF(font_path.string().c_str(), size);
		if (font) {
			MergeIcons(io, icon_path, size);
			return font;
		}
	}

	AppendMissing(warning, missing_name);
	ImFontConfig cfg;
	cfg.SizePixels = size;
	return io.Fonts->AddFontDefault(&cfg);
}

} // namespace

FontSet LoadFonts(ImGuiIO &io, std::string *warning)
{
	if (warning) warning->clear();

	const std::filesystem::path inter_regular = FindFont("Inter-Regular.ttf");
	const std::filesystem::path inter_medium = FindFont("Inter-Medium.ttf");
	const std::filesystem::path inter_semibold = FindFont("Inter-SemiBold.ttf");
	const std::filesystem::path jetbrains_mono = FindFont("JetBrainsMono-Regular.ttf");
	const std::filesystem::path lucide = FindFont("lucide.ttf");
	if (lucide.empty()) AppendMissing(warning, "lucide.ttf");

	FontSet fonts;
	fonts.body = AddFont(io, inter_regular, lucide, 16.0f, "Inter-Regular.ttf", warning);
	fonts.body_medium = AddFont(io, inter_medium, lucide, 16.0f, "Inter-Medium.ttf", warning);
	fonts.heading = AddFont(io, inter_semibold, lucide, 22.0f, "Inter-SemiBold.ttf", warning);
	fonts.display = AddFont(io, inter_semibold, lucide, 28.0f, "Inter-SemiBold.ttf", warning);

	if (!jetbrains_mono.empty()) {
		fonts.mono = io.Fonts->AddFontFromFileTTF(jetbrains_mono.string().c_str(), 13.0f);
	}
	if (!fonts.mono) {
		AppendMissing(warning, "JetBrainsMono-Regular.ttf");
		ImFontConfig cfg;
		cfg.SizePixels = 13.0f;
		fonts.mono = io.Fonts->AddFontDefault(&cfg);
	}

	io.FontDefault = fonts.body;

	if (warning && !warning->empty()) {
		*warning = "Font fallback active; missing " + *warning;
	}
	return fonts;
}

} // namespace openvr_pair::overlay
