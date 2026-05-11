#include "FeaturePlugin.h"
#include "Fonts.h"
#include "ModulesTab.h"
#include "Sidebar.h"
#include "ShellContext.h"
#include "Theme.h"
#include "TopBar.h"
#include "Widgets.h"

#include <GL/gl3w.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>

#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#ifndef OPENVR_PAIR_VERSION_STRING
#define OPENVR_PAIR_VERSION_STRING "0.0.0.0-dev"
#endif

namespace openvr_pair::overlay {

std::unique_ptr<FeaturePlugin> CreateInputHealthPlugin();
std::unique_ptr<FeaturePlugin> CreateSmoothingPlugin();
std::unique_ptr<FeaturePlugin> CreateSpaceCalibratorPlugin();

} // namespace openvr_pair::overlay

namespace {

void GlfwErrorCallback(int code, const char *description)
{
	fprintf(stderr, "[glfw] error %d: %s\n", code, description ? description : "(null)");
}

std::vector<std::unique_ptr<openvr_pair::overlay::FeaturePlugin>> CreatePlugins()
{
	using namespace openvr_pair::overlay;
	std::vector<std::unique_ptr<FeaturePlugin>> plugins;
#if OPENVR_PAIR_HAS_INPUTHEALTH_OVERLAY
	plugins.push_back(CreateInputHealthPlugin());
#endif
#if OPENVR_PAIR_HAS_SMOOTHING_OVERLAY
	plugins.push_back(CreateSmoothingPlugin());
#endif
#if OPENVR_PAIR_HAS_CALIBRATION_OVERLAY
	plugins.push_back(CreateSpaceCalibratorPlugin());
#endif
	return plugins;
}

bool IsSelectedPluginValid(openvr_pair::overlay::ShellContext &context,
	const std::vector<std::unique_ptr<openvr_pair::overlay::FeaturePlugin>> &plugins,
	int selected_plugin)
{
	if (selected_plugin < 0 || selected_plugin >= static_cast<int>(plugins.size())) return false;
	return plugins[static_cast<size_t>(selected_plugin)]->IsInstalled(context);
}

int FirstInstalledPlugin(openvr_pair::overlay::ShellContext &context,
	const std::vector<std::unique_ptr<openvr_pair::overlay::FeaturePlugin>> &plugins)
{
	for (int i = 0; i < static_cast<int>(plugins.size()); ++i) {
		if (plugins[static_cast<size_t>(i)]->IsInstalled(context)) return i;
	}
	return -1;
}

} // namespace

int main(int, char **)
{
	using namespace openvr_pair::overlay;

	glfwSetErrorCallback(GlfwErrorCallback);
	if (!glfwInit()) return 1;

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

	GLFWwindow *window = glfwCreateWindow(1320, 820, "OpenVR-Pair", nullptr, nullptr);
	if (!window) {
		glfwTerminate();
		return 1;
	}

	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);
	if (gl3wInit() != 0) {
		glfwDestroyWindow(window);
		glfwTerminate();
		return 1;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImPlot::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = nullptr;
	std::string font_warning;
	FontSet fonts = LoadFonts(io, &font_warning);
	openvr_pair::ui::Apply(io);

	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 130");

	ShellContext context = CreateShellContext();
	context.fontSet = fonts;
	if (!font_warning.empty()) context.SetStatus(font_warning);
	auto plugins = CreatePlugins();
	for (auto &plugin : plugins) {
		plugin->OnStart(context);
	}
	int selected_plugin = FirstInstalledPlugin(context, plugins);
	bool modules_selected = selected_plugin < 0;

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		for (auto &plugin : plugins) {
			if (plugin->IsInstalled(context)) plugin->Tick(context);
		}

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		if (!modules_selected && !IsSelectedPluginValid(context, plugins, selected_plugin)) {
			selected_plugin = FirstInstalledPlugin(context, plugins);
			modules_selected = selected_plugin < 0;
		}

		const ImGuiViewport *vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(vp->WorkPos);
		ImGui::SetNextWindowSize(vp->WorkSize);
		ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoBringToFrontOnFocus;
		ImGui::PushStyleColor(ImGuiCol_WindowBg, openvr_pair::ui::Colors().bg);
		ImGui::Begin("OpenVR-Pair", nullptr, flags);

		DrawSidebar(context, plugins, selected_plugin, modules_selected);
		ImGui::SameLine(0.0f, 0.0f);

		openvr_pair::overlay::FeaturePlugin *selected = nullptr;
		if (!modules_selected && IsSelectedPluginValid(context, plugins, selected_plugin)) {
			selected = plugins[static_cast<size_t>(selected_plugin)].get();
		}

		const char *title = modules_selected || !selected ? "Modules" : selected->Name();
		const char *subtitle = modules_selected || !selected ? "Toggle features on/off" : selected->Subtitle();
		const bool driver_ok = selected ? selected->DriverStatusOk(context) : !context.driverResourceDirs.empty();
		const bool ipc_ok = selected ? selected->IpcStatusOk(context) : true;
		const bool shmem_ok = selected ? selected->SharedMemoryStatusOk(context) : true;

		ImGui::BeginGroup();
		DrawTopBar(context, title, subtitle, driver_ok, ipc_ok, shmem_ok);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, openvr_pair::ui::Colors().bg);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 24.0f));
		ImGui::BeginChild("##body", ImVec2(0.0f, 0.0f),
			ImGuiChildFlags_AlwaysUseWindowPadding,
			ImGuiWindowFlags_NoCollapse);
		if (!context.status.empty()) {
			openvr_pair::ui::StatusDot(context.status.c_str(), openvr_pair::ui::Status::Warning);
			ImGui::Spacing();
		}
		if (modules_selected || !selected) {
			DrawModulesTab(context, plugins);
		} else {
			selected->DrawTab(context);
		}
		ImGui::EndChild();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
		ImGui::EndGroup();

		ImGui::End();
		ImGui::PopStyleColor();
		ImGui::Render();

		int fbw = 0;
		int fbh = 0;
		glfwGetFramebufferSize(window, &fbw, &fbh);
		glViewport(0, 0, fbw, fbh);
		const ImVec4 bg = openvr_pair::ui::Colors().bg;
		glClearColor(bg.x, bg.y, bg.z, bg.w);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		glfwSwapBuffers(window);
	}

	for (auto it = plugins.rbegin(); it != plugins.rend(); ++it) {
		(*it)->OnShutdown(context);
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImPlot::DestroyContext();
	ImGui::DestroyContext();
	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}
