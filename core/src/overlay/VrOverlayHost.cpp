#include "VrOverlayHost.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace openvr_pair::overlay {

namespace {

// Must match the manifest in core/src/overlay/manifest.vrmanifest and the
// constant in ManifestRegistration.cpp. SteamVR keys overlays by app key,
// so re-using the manifest key makes the dashboard tile and the autolaunch
// registration point at the same entity.
constexpr const char *kAppKey = "wk.wkopenvr";
constexpr const char *kFriendlyName = "WKOpenVR";

// Width of the dashboard panel in metres. SC used 3.0; same value
// renders comfortably at typical viewing distance.
constexpr float kOverlayWidthMeters = 3.0f;

// Retry the VR session probe at most once per second. Matches SC.
constexpr double kInitRetrySeconds = 1.0;

// Discrete-scroll quantization SC found readable on Index controllers.
// xdelta / ydelta from VREvent_Scroll_t are in fraction-of-full-tick;
// multiply by 360*8 to scale into ImGui's wheel-tick range.
constexpr float kScrollScale = 360.0f * 8.0f;

#ifdef _WIN32
std::filesystem::path ExeDir()
{
	char buf[MAX_PATH] = {};
	const DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
	if (len == 0 || len == MAX_PATH) return {};
	std::filesystem::path p(buf);
	return p.parent_path();
}
#else
std::filesystem::path ExeDir()
{
	return {};
}
#endif

double NowSeconds()
{
#ifdef _WIN32
	LARGE_INTEGER freq{};
	LARGE_INTEGER counter{};
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&counter);
	return double(counter.QuadPart) / double(freq.QuadPart);
#else
	return 0.0;
#endif
}

} // namespace

VrOverlayHost::VrOverlayHost() = default;

VrOverlayHost::~VrOverlayHost()
{
	if (overlayCreated_ && vr::VROverlay()) {
		if (mainHandle_)  vr::VROverlay()->DestroyOverlay(mainHandle_);
		if (thumbHandle_) vr::VROverlay()->DestroyOverlay(thumbHandle_);
	}
	if (vrReady_) {
		vr::VR_Shutdown();
	}
}

std::string VrOverlayHost::ResolveIconPath() const
{
	std::filesystem::path p = ExeDir();
	if (p.empty()) return {};
	p /= "dashboard_icon.png";
	return p.string();
}

bool VrOverlayHost::TryInitVrStack()
{
	if (vrReady_) return true;

	// Phase 1: probe with Background. Background-mode VR_Init fails fast
	// when vrserver isn't running instead of auto-launching SteamVR, which
	// is what we want -- the umbrella should not bring SteamVR up by
	// merely existing. Mirrors SpaceCalibrator.cpp:312-336.
	vr::EVRInitError err = vr::VRInitError_None;
	vr::VR_Init(&err, vr::VRApplication_Background);
	if (err != vr::VRInitError_None) {
		// Not fatal. The most common error here is
		// VRInitError_Init_NoServerForBackgroundApp, which just means
		// SteamVR is not running yet.
		return false;
	}

	// Phase 2: tear the probe down and re-init as Overlay so we can
	// create dashboard overlays.
	vr::VR_Shutdown();

	vr::VR_Init(&err, vr::VRApplication_Overlay);
	if (err != vr::VRInitError_None) {
		fprintf(stderr, "[VrOverlayHost] VR_Init(Overlay) failed: %s\n",
			vr::VR_GetVRInitErrorAsEnglishDescription(err));
		return false;
	}

	if (!vr::VR_IsInterfaceVersionValid(vr::IVRSystem_Version)
		|| !vr::VR_IsInterfaceVersionValid(vr::IVROverlay_Version))
	{
		fprintf(stderr, "[VrOverlayHost] OpenVR interface version mismatch; "
			"runtime DLL out of date.\n");
		vr::VR_Shutdown();
		return false;
	}

	vrReady_ = true;
	return true;
}

void VrOverlayHost::TryCreateOverlay()
{
	if (overlayCreated_ || !vr::VROverlay()) return;

	const vr::EVROverlayError err = vr::VROverlay()->CreateDashboardOverlay(
		kAppKey, kFriendlyName, &mainHandle_, &thumbHandle_);

	if (err == vr::VROverlayError_KeyInUse) {
		// Another instance of the umbrella is already registered with
		// vrserver. Log but don't crash -- the user can fix this by
		// closing the other instance.
		fprintf(stderr, "[VrOverlayHost] CreateDashboardOverlay: key in use "
			"(another WKOpenVR instance is registered)\n");
		return;
	}
	if (err != vr::VROverlayError_None) {
		fprintf(stderr, "[VrOverlayHost] CreateDashboardOverlay failed: %s\n",
			vr::VROverlay()->GetOverlayErrorNameFromEnum(err));
		return;
	}

	vr::VROverlay()->SetOverlayWidthInMeters(mainHandle_, kOverlayWidthMeters);
	vr::VROverlay()->SetOverlayInputMethod(mainHandle_,
		vr::VROverlayInputMethod_Mouse);
	vr::VROverlay()->SetOverlayFlag(mainHandle_,
		vr::VROverlayFlags_SendVRDiscreteScrollEvents, true);

	const std::string iconPath = ResolveIconPath();
	if (!iconPath.empty() && std::filesystem::exists(iconPath)) {
		vr::VROverlay()->SetOverlayFromFile(thumbHandle_, iconPath.c_str());
	}

	overlayCreated_ = true;
	fprintf(stderr, "[VrOverlayHost] dashboard overlay created (main=%llu thumb=%llu)\n",
		(unsigned long long)mainHandle_, (unsigned long long)thumbHandle_);
}

bool VrOverlayHost::IsDashboardVisible() const
{
	if (!overlayCreated_ || !vr::VROverlay()) return false;
	return vr::VROverlay()->IsActiveDashboardOverlay(mainHandle_);
}

void VrOverlayHost::DrainOverlayEvents()
{
	if (!overlayCreated_ || !vr::VROverlay()) return;

	ImGuiIO &io = ImGui::GetIO();
	vr::VREvent_t ev{};
	while (vr::VROverlay()->PollNextOverlayEvent(mainHandle_, &ev, sizeof(ev)))
	{
		switch (ev.eventType) {
		case vr::VREvent_MouseMove:
			io.AddMousePosEvent(ev.data.mouse.x, ev.data.mouse.y);
			break;
		case vr::VREvent_MouseButtonDown:
			io.AddMouseButtonEvent(
				(ev.data.mouse.button & vr::VRMouseButton_Left) == vr::VRMouseButton_Left
					? 0 : 1,
				true);
			break;
		case vr::VREvent_MouseButtonUp:
			io.AddMouseButtonEvent(
				(ev.data.mouse.button & vr::VRMouseButton_Left) == vr::VRMouseButton_Left
					? 0 : 1,
				false);
			break;
		case vr::VREvent_ScrollDiscrete: {
			const float x = ev.data.scroll.xdelta * kScrollScale;
			const float y = ev.data.scroll.ydelta * kScrollScale;
			io.AddMouseWheelEvent(x, y);
			break;
		}
		case vr::VREvent_Quit:
			quitRequested_ = true;
			break;
		default:
			break;
		}
	}
}

bool VrOverlayHost::TickFrame()
{
	// Stage 1: bring the VR session up. While SteamVR is down the rest
	// of this function is a no-op and the umbrella continues to render
	// to its desktop window only.
	if (!vrReady_) {
		const double now = NowSeconds();
		if (now - lastInitRetry_ >= kInitRetrySeconds) {
			lastInitRetry_ = now;
			TryInitVrStack();
		}
		if (!vrReady_) return false;
	}

	// Stage 2: register the dashboard overlay (one-shot).
	TryCreateOverlay();

	// Stage 3: drain OpenVR events into ImGui IO. Virtual-keyboard
	// support is intentionally deferred -- replacing an active
	// ImGui InputText buffer needs imgui_internal API that varies
	// across the pin range and is best landed after the dashboard
	// render path is verified end-to-end.
	DrainOverlayEvents();

	return IsDashboardVisible();
}

void VrOverlayHost::SubmitTexture(unsigned int glTextureId, int width, int height)
{
	if (!overlayCreated_ || !vr::VROverlay()) return;
	if (!IsDashboardVisible()) return;
	if (glTextureId == 0 || width <= 0 || height <= 0) return;

	vr::Texture_t tex{};
	tex.handle = reinterpret_cast<void *>(static_cast<uintptr_t>(glTextureId));
	tex.eType = vr::TextureType_OpenGL;
	tex.eColorSpace = vr::ColorSpace_Auto;

	vr::VROverlay()->SetOverlayTexture(mainHandle_, &tex);

	vr::HmdVector2_t mouseScale{};
	mouseScale.v[0] = static_cast<float>(width);
	mouseScale.v[1] = static_cast<float>(height);
	vr::VROverlay()->SetOverlayMouseScale(mainHandle_, &mouseScale);
}

} // namespace openvr_pair::overlay
