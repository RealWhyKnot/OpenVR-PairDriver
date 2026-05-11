#include "FeaturePlugin.h"
#include "ShellContext.h"
#include "UiHelpers.h"

#include <GL/gl3w.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>

#include <cstdio>
#include <exception>
#include <map>
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

void DrawTransientStatus(openvr_pair::overlay::ShellContext &context)
{
	// Transient feedback (elevated module toggles, IPC heartbeat hiccups)
	// drawn as a thin coloured line just above the bottom edge. The version
	// stamp and driver-status dot live on each plugin's own footer (SC,
	// InputHealth, Smoothing) so the shell doesn't duplicate them.
	if (context.status.empty()) return;
	const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
	const float windowHeight = ImGui::GetWindowHeight();
	const float padding = ImGui::GetStyle().WindowPadding.y;
	ImGui::SetCursorPosY(windowHeight - lineHeight * 3.0f - padding);
	ImGui::Separator();
	openvr_pair::overlay::ui::DrawTextWrapped(context.status.c_str());
}

void DrawModules(openvr_pair::overlay::ShellContext &context,
	std::vector<std::unique_ptr<openvr_pair::overlay::FeaturePlugin>> &plugins)
{
	// Visual intent: the value the checkbox should display while the
	// elevated helper is in flight. Cleared as soon as ShellContext is no
	// longer tracking a pending toggle for this flag (process exited, with
	// or without writing the file).
	static std::map<std::string, bool> wanted;

	ImGui::TextUnformatted("Modules");
	openvr_pair::overlay::ui::DrawTextWrapped(
		"Toggle features on or off. Each change pops a UAC prompt. "
		"Changes take effect the next time SteamVR loads the driver.");
	ImGui::Spacing();
	if (ImGui::BeginTable("modules", 3,
		ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupColumn("Module", ImGuiTableColumnFlags_WidthStretch, 0.40f);
		ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 80.0f);
		ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 0.60f);
		ImGui::TableHeadersRow();

		for (auto &plugin : plugins) {
			const bool installed = plugin->IsInstalled(context);
			const std::string key = plugin->FlagFileName();
			const bool isPending = context.IsTogglePending(key.c_str());

			auto it = wanted.find(key);
			if (!isPending && it != wanted.end()) {
				wanted.erase(it);
				it = wanted.end();
			}
			const bool displayState = (it != wanted.end()) ? it->second : installed;

			ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(plugin->Name());

			ImGui::TableNextColumn();
			ImGui::PushID(key.c_str());
			ImGui::BeginDisabled(isPending);
			bool checkbox = displayState;
			const std::string tooltip = std::string("Enable or disable ") + plugin->Name() +
			                            " for this profile. Takes effect next SteamVR launch.";
			if (openvr_pair::overlay::ui::CheckboxWithTooltip(
					"##enabled", &checkbox, tooltip.c_str())) {
				wanted[key] = checkbox;
				context.SetFlagPresent(plugin->FlagFileName(), checkbox);
			}
			ImGui::EndDisabled();
			ImGui::PopID();

			ImGui::TableNextColumn();
			if (isPending) {
				ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.4f, 1.0f),
					"%s -- takes effect on next SteamVR launch",
					(it != wanted.end() && it->second) ? "Enabling" : "Disabling");
			} else if (installed) {
				ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "Enabled");
			} else {
				ImGui::TextDisabled("Disabled");
			}
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
	openvr_pair::overlay::ui::ApplyOverlayStyle();
	auto plugins = CreatePlugins();
	for (auto &plugin : plugins) {
		plugin->OnStart(context);
	}

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		context.TickToggles();

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

		DrawTransientStatus(context);

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
