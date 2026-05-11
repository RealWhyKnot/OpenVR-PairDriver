#include "FeaturePlugin.h"
#include "ShellContext.h"

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

void DrawStatus(openvr_pair::overlay::ShellContext &context)
{
	ImGui::TextUnformatted("OpenVR-Pair");
	ImGui::SameLine();
	ImGui::TextDisabled("%s", OPENVR_PAIR_VERSION_STRING);
	ImGui::Separator();
	if (!context.status.empty()) {
		ImGui::TextWrapped("%s", context.status.c_str());
	}
}

void DrawModules(openvr_pair::overlay::ShellContext &context,
	std::vector<std::unique_ptr<openvr_pair::overlay::FeaturePlugin>> &plugins)
{
	ImGui::TextUnformatted("Installed modules");
	ImGui::Spacing();
	if (ImGui::BeginTable("modules", 4, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
		ImGui::TableSetupColumn("Module");
		ImGui::TableSetupColumn("Enabled");
		ImGui::TableSetupColumn("Flag file");
		ImGui::TableSetupColumn("Pipe");
		ImGui::TableHeadersRow();

		for (auto &plugin : plugins) {
			bool installed = plugin->IsInstalled(context);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(plugin->Name());
			ImGui::TableNextColumn();
			ImGui::PushID(plugin->FlagFileName());
			bool wanted = installed;
			if (ImGui::Checkbox("##enabled", &wanted) && wanted != installed) {
				context.SetFlagPresent(plugin->FlagFileName(), wanted);
			}
			ImGui::PopID();
			ImGui::TableNextColumn();
			std::string flagPath = openvr_pair::overlay::Narrow(context.FlagPath(plugin->FlagFileName()));
			ImGui::TextWrapped("%s", flagPath.c_str());
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(plugin->PipeName());
		}
		ImGui::EndTable();
	}
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

	GLFWwindow *window = glfwCreateWindow(1200, 780, "OpenVR-Pair", nullptr, nullptr);
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
	ImGui::StyleColorsDark();
	ImGui::GetIO().IniFilename = nullptr;

	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 130");

	ShellContext context = CreateShellContext();
	auto plugins = CreatePlugins();
	for (auto &plugin : plugins) {
		plugin->OnStart(context);
	}

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		for (auto &plugin : plugins) {
			if (plugin->IsInstalled(context)) plugin->Tick(context);
		}

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		const ImGuiViewport *vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(vp->WorkPos);
		ImGui::SetNextWindowSize(vp->WorkSize);
		ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoBringToFrontOnFocus;
		ImGui::Begin("OpenVR-Pair", nullptr, flags);

		DrawStatus(context);
		if (ImGui::BeginTabBar("tabs")) {
			for (auto &plugin : plugins) {
				if (!plugin->IsInstalled(context)) continue;
				if (ImGui::BeginTabItem(plugin->Name())) {
					plugin->DrawTab(context);
					ImGui::EndTabItem();
				}
			}
			if (ImGui::BeginTabItem("Modules")) {
				DrawModules(context, plugins);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		ImGui::End();
		ImGui::Render();

		int fbw = 0;
		int fbh = 0;
		glfwGetFramebufferSize(window, &fbw, &fbh);
		glViewport(0, 0, fbw, fbh);
		glClearColor(0.07f, 0.07f, 0.08f, 1.0f);
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
