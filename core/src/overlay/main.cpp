#include "FeaturePlugin.h"
#include "ManifestRegistration.h"
#include "Migration.h"
#include "ShellContext.h"
#include "Theme.h"
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
			const openvr_pair::overlay::ui::SemanticPalette &pal =
				openvr_pair::overlay::ui::GetPalette();
			const char *statusText = nullptr;
			ImVec4 statusColor{};
			bool statusColored = false;
			if (isPending) {
				statusText = (it != wanted.end() && it->second)
					? "Enabling -- takes effect on next SteamVR launch"
					: "Disabling -- takes effect on next SteamVR launch";
				statusColor = pal.statusPending;
				statusColored = true;
			} else if (installed) {
				statusText = "Enabled";
				statusColor = pal.statusOk;
				statusColored = true;
			} else {
				statusText = "Disabled";
			}
			openvr_pair::overlay::ui::RightAlignText(statusText, statusColor, statusColored);

			// Column 2: enabled checkbox, far right. When a UAC toggle is
			// in flight, disable the checkbox and surface the reason on
			// hover so the user is not staring at an unresponsive control.
			ImGui::TableNextColumn();
			ImGui::PushID(key.c_str());
			const std::string pendingReason =
				"Waiting for the elevated helper to finish. Reopens after SteamVR picks up the change.";
			openvr_pair::overlay::ui::DisabledSection disabled(
				isPending, isPending ? pendingReason.c_str() : nullptr);
			bool checkbox = displayState;
			const std::string tooltip = std::string("Enable or disable ") + plugin->Name() +
			                            " for this profile. Takes effect next SteamVR launch.";
			if (openvr_pair::overlay::ui::CheckboxWithTooltip(
					"##enabled", &checkbox, tooltip.c_str())) {
				wanted[key] = checkbox;
				context.SetFlagPresent(plugin->FlagFileName(), checkbox);
			}
			disabled.AttachReasonTooltip();
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
}

void DrawThemes(openvr_pair::overlay::ShellContext & /*context*/)
{
	using namespace openvr_pair::overlay::ui;

	DrawTextWrapped("Choose a color theme. Changes apply immediately and persist across launches.");
	ImGui::Spacing();

	const ThemeId current = GetCurrentThemeId();
	for (int i = 0; i < (int)ThemeId::Count_; ++i) {
		const ThemeId id = (ThemeId)i;
		const bool selected = (id == current);
		ImGui::PushID(i);
		if (ImGui::RadioButton(ThemeName(id), selected)) {
			SetTheme(id);
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%s", ThemeCaption(id));
		ImGui::PopID();
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

	// Floor the window size so the Modules table's fixed Status/Enabled
	// columns always have room and the tab strip remains usable. The
	// dashboard overlay continues to render at a fixed 1200x780 regardless
	// of the desktop window size, so this only affects monitor-side use.
	glfwSetWindowSizeLimits(window, 640, 480, GLFW_DONT_CARE, GLFW_DONT_CARE);

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
	// Theme colors are applied by InitThemeFromDisk further down; no
	// StyleColorsDark() call needed here. ApplyOverlayStyle still owns
	// padding/spacing/rounding (theme-independent).
	ImGui::GetIO().IniFilename = nullptr;

	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 130");

	// Two render targets, one chosen per frame:
	//
	//  - vrFbo (fixed 1200x780): submitted to the SteamVR dashboard. The
	//    fixed size keeps the in-VR overlay's apparent resolution and the
	//    SetOverlayMouseScale mapping stable across desktop-window resizes.
	//
	//  - winFbo (matches the GLFW framebuffer, reallocated on resize):
	//    used when the dashboard is not visible so the desktop window
	//    blits 1:1, no stretching, and ImGui lays out at the actual
	//    window size.
	constexpr int kVrFboWidth = 1200;
	constexpr int kVrFboHeight = 780;

	auto allocFboTexture = [](GLuint &fbo, GLuint &tex, int w, int h) {
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
			GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glGenFramebuffers(1, &fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex, 0);
		GLenum drawBuffers[1] = { GL_COLOR_ATTACHMENT0 };
		glDrawBuffers(1, drawBuffers);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			fprintf(stderr, "[WKOpenVR] framebuffer incomplete (%dx%d)\n", w, h);
		}
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	};

	GLuint vrFbo = 0, vrTexture = 0;
	allocFboTexture(vrFbo, vrTexture, kVrFboWidth, kVrFboHeight);

	int initialFbw = 0, initialFbh = 0;
	glfwGetFramebufferSize(window, &initialFbw, &initialFbh);
	if (initialFbw <= 0) initialFbw = kVrFboWidth;
	if (initialFbh <= 0) initialFbh = kVrFboHeight;
	GLuint winFbo = 0, winTexture = 0;
	allocFboTexture(winFbo, winTexture, initialFbw, initialFbh);
	int curWinFbW = initialFbw;
	int curWinFbH = initialFbh;

	auto reallocWinFbo = [&](int w, int h) {
		// Only the texture storage needs to change; the FBO handle and
		// the texture attachment stay valid because the binding tracks
		// the texture name, not its dimensions.
		glBindTexture(GL_TEXTURE_2D, winTexture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
			GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		curWinFbW = w;
		curWinFbH = h;
	};

	ShellContext context = CreateShellContext();
	openvr_pair::overlay::ui::ApplyOverlayStyle();
	openvr_pair::overlay::ui::InitThemeFromDisk(context);
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
		if (dashboardVisible) {
			// VR render target is fixed-resolution; override what GLFW
			// reported so ImGui lays out at the FBO size and the VR mouse
			// coords (which are in submitted-texture pixel space) map back
			// onto ImGui widgets correctly.
			io.DisplaySize = ImVec2(static_cast<float>(kVrFboWidth),
				static_cast<float>(kVrFboHeight));
			io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
			io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
		} else {
			// Let ImGui_ImplGlfw_NewFrame's DisplaySize / FramebufferScale
			// stand. They reflect the live GLFW window so layout reflows
			// at the actual size instead of stretching a fixed FBO blit.
			io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
		}

		ImGui::NewFrame();

		const ImGuiViewport *vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(vp->WorkPos);
		ImGui::SetNextWindowSize(vp->WorkSize);
		// NoScrollbar/NoScrollWithMouse: every feature tab that needs to
		// scroll already owns an inner child for it (e.g. SC's
		// "SCTabBody" reserves footer space and scrolls the rest). If
		// the outer shell window also offered its own scrollbar, those
		// tabs would render two vertical scrollbars side by side.
		ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoBringToFrontOnFocus |
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
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
			if (ImGui::BeginTabItem("Themes")) {
				DrawThemes(context);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		DrawTransientStatus(context);

		ImGui::End();
		ImGui::Render();

		// Two render paths share one ImGui::GetDrawData() call: the
		// content laid out above is rasterised into whichever FBO the
		// current frame targets. The clear color tracks the theme's
		// WindowBg so a Light theme does not leave a dark gutter around
		// the ImGui rectangle.
		const ImVec4 clearCol = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);

		int fbw = 0;
		int fbh = 0;
		glfwGetFramebufferSize(window, &fbw, &fbh);

		if (dashboardVisible) {
			// VR path: render into the fixed-size FBO and submit. The
			// desktop blit stretches because monitor users behind a VR
			// session are not the primary audience here.
			glBindFramebuffer(GL_FRAMEBUFFER, vrFbo);
			glViewport(0, 0, kVrFboWidth, kVrFboHeight);
			glClearColor(clearCol.x, clearCol.y, clearCol.z, clearCol.w);
			glClear(GL_COLOR_BUFFER_BIT);
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			if (fbw > 0 && fbh > 0) {
				glBindFramebuffer(GL_READ_FRAMEBUFFER, vrFbo);
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
				glBlitFramebuffer(0, 0, kVrFboWidth, kVrFboHeight,
					0, 0, fbw, fbh,
					GL_COLOR_BUFFER_BIT, GL_LINEAR);
				glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
				glfwSwapBuffers(window);
			}

			vrOverlay->SubmitTexture(vrTexture, kVrFboWidth, kVrFboHeight);
		} else if (fbw > 0 && fbh > 0) {
			// Desktop path: keep the window FBO sized to the actual
			// framebuffer so the 1:1 blit below preserves pixel sharpness
			// and ImGui's layout (already taken at GLFW's DisplaySize)
			// covers the visible area exactly.
			if (fbw != curWinFbW || fbh != curWinFbH) {
				reallocWinFbo(fbw, fbh);
			}

			glBindFramebuffer(GL_FRAMEBUFFER, winFbo);
			glViewport(0, 0, fbw, fbh);
			glClearColor(clearCol.x, clearCol.y, clearCol.z, clearCol.w);
			glClear(GL_COLOR_BUFFER_BIT);
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			glBindFramebuffer(GL_READ_FRAMEBUFFER, winFbo);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			glBlitFramebuffer(0, 0, fbw, fbh, 0, 0, fbw, fbh,
				GL_COLOR_BUFFER_BIT, GL_NEAREST);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
			glfwSwapBuffers(window);
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

	glDeleteFramebuffers(1, &vrFbo);
	glDeleteTextures(1, &vrTexture);
	glDeleteFramebuffers(1, &winFbo);
	glDeleteTextures(1, &winTexture);

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImPlot::DestroyContext();
	ImGui::DestroyContext();
	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}
