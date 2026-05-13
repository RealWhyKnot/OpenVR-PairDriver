#include "FeaturePlugin.h"
#include "ManifestRegistration.h"
#include "Migration.h"
#include "ShellContext.h"
#include "UiHelpers.h"
#include "VrOverlayHost.h"

#include <GL/gl3w.h>
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <cstdio>
#include <exception>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#ifndef OPENVR_PAIR_VERSION_STRING
#define OPENVR_PAIR_VERSION_STRING "0.0.0.0-dev"
#endif

namespace openvr_pair::overlay {

std::unique_ptr<FeaturePlugin> CreateInputHealthPlugin();
std::unique_ptr<FeaturePlugin> CreateSmoothingPlugin();
std::unique_ptr<FeaturePlugin> CreateSpaceCalibratorPlugin();
std::unique_ptr<FeaturePlugin> CreateFaceTrackingPlugin();

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
#if OPENVR_PAIR_HAS_FACETRACKING_OVERLAY
	plugins.push_back(CreateFaceTrackingPlugin());
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

void DrawGlobalLogs(openvr_pair::overlay::ShellContext &context,
	std::vector<std::unique_ptr<openvr_pair::overlay::FeaturePlugin>> &plugins)
{
	// One tab to find every plugin's log surface. Replaces the per-feature
	// Logs sub-tab that used to live inside each plugin -- the user reported
	// having to remember which plugin owned which log file. Each plugin's
	// DrawLogsSection emits into a collapsing header so a long SC panel does
	// not push Smoothing / InputHealth off-screen.
	openvr_pair::overlay::ui::DrawTextWrapped(
		"Per-module logs. All overlay-side logs land in "
		"%LocalAppDataLow%\\WKOpenVR\\Logs\\; driver-side logs land in "
		"%LocalAppDataLow%\\WKOpenVR\\Logs\\.");
	ImGui::Spacing();

	bool anyDrawn = false;
	for (auto &plugin : plugins) {
		if (!plugin->IsInstalled(context)) continue;
		ImGui::PushID(plugin->Name());
		// SetNextItemOpen(true) on first frame so the user does not have to
		// click into every section to see content. Subsequent frames respect
		// whatever the user left the header at.
		ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
		if (ImGui::CollapsingHeader(plugin->Name())) {
			ImGui::Indent();
			plugin->DrawLogsSection(context);
			ImGui::Unindent();
		}
		ImGui::PopID();
		anyDrawn = true;
	}
	if (!anyDrawn) {
		ImGui::TextDisabled("No installed feature plugins.");
	}
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
		// Module name stretches left so Status + Enabled hug the right edge.
		// Status is wide enough to hold "Enabling -- takes effect on next
		// SteamVR launch" without wrapping; Enabled holds the checkbox plus
		// the "Enabled" header without ImGui ellipsizing it (the 70 px the
		// column originally had ate the header text).
		ImGui::TableSetupColumn("Module",  ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("Status",  ImGuiTableColumnFlags_WidthFixed,  340.0f);
		ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed,  100.0f);
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

			// Column 0: module name (left).
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(plugin->Name());

			// Column 1: status text, right-aligned within its fixed column.
			// During a pending toggle the row is the only place the user can
			// learn the change is in flight and won't take effect until the
			// next SteamVR launch -- that's the reason status isn't merged
			// into the checkbox.
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			const char *statusText = nullptr;
			ImVec4 statusColor{};
			bool statusColored = false;
			if (isPending) {
				statusText = (it != wanted.end() && it->second)
					? "Enabling -- takes effect on next SteamVR launch"
					: "Disabling -- takes effect on next SteamVR launch";
				statusColor = ImVec4(0.95f, 0.7f, 0.4f, 1.0f);
				statusColored = true;
			} else if (installed) {
				statusText = "Enabled";
				statusColor = ImVec4(0.45f, 0.85f, 0.45f, 1.0f);
				statusColored = true;
			} else {
				statusText = "Disabled";
			}
			// Right-align by padding from the right edge: avail width - text width.
			const float colW = ImGui::GetContentRegionAvail().x;
			const float textW = ImGui::CalcTextSize(statusText).x;
			const float pad = (colW > textW) ? (colW - textW) : 0.0f;
			ImGui::Dummy(ImVec2(pad, 0));
			ImGui::SameLine(0, 0);
			if (statusColored) {
				ImGui::TextColored(statusColor, "%s", statusText);
			} else {
				ImGui::TextDisabled("%s", statusText);
			}

			// Column 2: enabled checkbox, far right.
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
		}
		ImGui::EndTable();
	}
}

} // namespace

int main(int argc, char **argv)
{
	using namespace openvr_pair::overlay;

	// Headless command modes. The installer calls --register-only as a
	// post-install step, and the uninstaller calls --unregister-only before
	// deleting the exe so SteamVR does not end up holding an autolaunch
	// pointer at a deleted binary. Both exit before GLFW touches the screen.
	bool registerOnly = false;
	bool unregisterOnly = false;
	for (int i = 1; i < argc; ++i) {
		const std::string_view arg(argv[i]);
		if (arg == "--register-only")   registerOnly = true;
		if (arg == "--unregister-only") unregisterOnly = true;
	}

	if (unregisterOnly) {
		UnregisterApplicationManifest();
		return 0;
	}

	// First-launch migration: copy AppData tree and SC registry key from
	// the old OpenVR-Pair paths to WKOpenVR. Idempotent -- short-circuits
	// immediately once the new locations already exist.
	RunFirstLaunchMigration();

	// Register vrmanifest with SteamVR if not already installed. Idempotent;
	// no-ops on subsequent launches and when the runtime is unavailable.
	RegisterApplicationManifest();

	if (registerOnly) {
		return 0;
	}

	glfwSetErrorCallback(GlfwErrorCallback);
	if (!glfwInit()) return 1;

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

	GLFWwindow *window = glfwCreateWindow(1200, 780, "WKOpenVR", nullptr, nullptr);
	if (!window) {
		glfwTerminate();
		return 1;
	}

#ifdef _WIN32
	// Windows shell (taskbar, Start menu, alt-tab) picks the ICON resource
	// off the .exe directly, but the GLFW window's title-bar icon comes
	// from a separate per-window WM_SETICON message. Without this, the
	// title bar renders GLFW's default icon while the taskbar shows the
	// real one -- which is exactly the asymmetry the user reported.
	// LoadImageW with LR_SHARED lets Windows manage the HICON lifetime.
	{
		HWND hwnd = glfwGetWin32Window(window);
		HINSTANCE hinst = GetModuleHandleW(nullptr);
		HICON iconBig = (HICON)LoadImageW(hinst, MAKEINTRESOURCEW(1), IMAGE_ICON,
			0, 0, LR_DEFAULTSIZE | LR_SHARED);
		HICON iconSmall = (HICON)LoadImageW(hinst, MAKEINTRESOURCEW(1), IMAGE_ICON,
			GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
		if (hwnd && iconBig)   SendMessageW(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)iconBig);
		if (hwnd && iconSmall) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)iconSmall);
	}
#endif

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

	// Offscreen FBO + RGBA8 texture. Every frame ImGui renders into
	// this texture, which then gets blitted to the desktop window
	// (for monitor display) AND submitted to the SteamVR dashboard
	// overlay (for in-VR display). Fixed 1200x780 to match the
	// default GLFW window size; the desktop window blit stretches
	// if the user resizes.
	constexpr int kFboWidth = 1200;
	constexpr int kFboHeight = 780;
	GLuint fboTexture = 0;
	GLuint fbo = 0;
	glGenTextures(1, &fboTexture);
	glBindTexture(GL_TEXTURE_2D, fboTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kFboWidth, kFboHeight, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, fboTexture, 0);
	GLenum drawBuffers[1] = { GL_COLOR_ATTACHMENT0 };
	glDrawBuffers(1, drawBuffers);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		fprintf(stderr, "[WKOpenVR] offscreen framebuffer incomplete\n");
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	ShellContext context = CreateShellContext();
	openvr_pair::overlay::ui::ApplyOverlayStyle();
	auto plugins = CreatePlugins();
	for (auto &plugin : plugins) {
		plugin->OnStart(context);
	}

	auto vrOverlay = std::make_unique<VrOverlayHost>();

	while (!glfwWindowShouldClose(window) && !vrOverlay->QuitRequested()) {
		context.TickToggles();

		for (auto &plugin : plugins) {
			if (plugin->IsInstalled(context)) plugin->Tick(context);
		}

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();

		// Drain SteamVR overlay events AFTER ImGui_ImplGlfw_NewFrame
		// so the VR mouse position wins over GLFW's desktop cursor
		// while the dashboard is visible. ImGui processes the event
		// queue in order on NewFrame; later events override earlier
		// ones. When the dashboard is not visible no mouse events
		// fire and GLFW's position is used unchanged.
		const bool dashboardVisible = vrOverlay->TickFrame();

		ImGuiIO &io = ImGui::GetIO();
		io.DisplaySize = ImVec2(static_cast<float>(kFboWidth),
			static_cast<float>(kFboHeight));
		io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
		if (dashboardVisible) {
			io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
		} else {
			io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
		}

		ImGui::NewFrame();

		const ImGuiViewport *vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(vp->WorkPos);
		ImGui::SetNextWindowSize(vp->WorkSize);
		ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoBringToFrontOnFocus;
		ImGui::Begin("WKOpenVR", nullptr, flags);

		if (ImGui::BeginTabBar("tabs")) {
			for (auto &plugin : plugins) {
				if (!plugin->IsInstalled(context)) continue;
				if (ImGui::BeginTabItem(plugin->Name())) {
					plugin->DrawTab(context);
					ImGui::EndTabItem();
				}
			}
			if (ImGui::BeginTabItem("Logs")) {
				DrawGlobalLogs(context, plugins);
				ImGui::EndTabItem();
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

		// Render into the FBO.
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glViewport(0, 0, kFboWidth, kFboHeight);
		glClearColor(0.07f, 0.07f, 0.08f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		// Blit FBO to the desktop window so monitor users still see
		// the same UI.
		int fbw = 0;
		int fbh = 0;
		glfwGetFramebufferSize(window, &fbw, &fbh);
		if (fbw > 0 && fbh > 0) {
			glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			glBlitFramebuffer(0, 0, kFboWidth, kFboHeight, 0, 0, fbw, fbh,
				GL_COLOR_BUFFER_BIT, GL_LINEAR);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
			glfwSwapBuffers(window);
		}

		// Submit the same texture to the dashboard overlay when it's
		// visible inside the headset.
		if (dashboardVisible) {
			vrOverlay->SubmitTexture(fboTexture, kFboWidth, kFboHeight);
		}

		// Wait for input or a frame interval. Tighter cadence when
		// the in-VR overlay is visible so the dashboard stays
		// responsive; broader cadence otherwise lets the desktop
		// process idle cheaply.
		constexpr double kDashboardFrameSeconds = 1.0 / 90.0;
		constexpr double kIdleFrameSeconds = 1.0 / 30.0;
		const double waitSeconds = dashboardVisible
			? kDashboardFrameSeconds
			: kIdleFrameSeconds;
		glfwWaitEventsTimeout(waitSeconds);
	}

	vrOverlay.reset();

	for (auto it = plugins.rbegin(); it != plugins.rend(); ++it) {
		(*it)->OnShutdown(context);
	}

	glDeleteFramebuffers(1, &fbo);
	glDeleteTextures(1, &fboTexture);

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImPlot::DestroyContext();
	ImGui::DestroyContext();
	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}
