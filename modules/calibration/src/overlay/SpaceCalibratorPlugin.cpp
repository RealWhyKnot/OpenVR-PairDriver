#include "SpaceCalibratorPlugin.h"

#include "CalibrationMetrics.h"
#include "DebugLogging.h"
#include "DiscordPresenceComposer.h"
#include "EmbeddedFiles.h"
#include "Protocol.h"
#include "ShellContext.h"
#include "SpaceCalibratorUmbrellaRuntime.h"
#include "UserInterface.h"

#include <imgui.h>

#include <memory>
#include <string>

// Defined in UserInterfaceTabsLogs.cpp. Declared here so the global Logs tab
// can surface SC's logs panel without pulling the file-scope forward decl
// out of UserInterface.cpp.
void CCal_DrawLogsPanel();

void SpaceCalibratorPlugin::OnStart(openvr_pair::overlay::ShellContext &)
{
	Metrics::enableLogs = openvr_pair::common::IsDebugLoggingEnabled();

	// Match the standalone SpaceCalibrator binary's typography so the
	// calibration UI looks the way long-time users expect. The umbrella
	// shell otherwise falls through to ImGui's default ProggyClean, which
	// renders too small and too pixelated for this overlay's size.
	auto &io = ImGui::GetIO();
	if (io.Fonts->Fonts.empty() ||
		(io.Fonts->Fonts.size() == 1 && io.Fonts->Fonts[0] == io.FontDefault &&
		 io.FontDefault != nullptr && io.FontDefault->FontSize < 18.0f))
	{
		io.Fonts->AddFontFromMemoryCompressedTTF(
			DroidSans_compressed_data,
			DroidSans_compressed_size,
			24.0f);
	}

	CCal_SetInUmbrella(true);
	CCal_UmbrellaStart();
}

void SpaceCalibratorPlugin::OnShutdown(openvr_pair::overlay::ShellContext &)
{
	CCal_UmbrellaShutdown();
}

void SpaceCalibratorPlugin::Tick(openvr_pair::overlay::ShellContext &)
{
	CCal_UmbrellaTick();
}

void SpaceCalibratorPlugin::DrawTab(openvr_pair::overlay::ShellContext &)
{
	CCal_DrawTab();
}

void SpaceCalibratorPlugin::DrawLogsSection(openvr_pair::overlay::ShellContext &)
{
	// Surfaces the existing SC Logs panel (file list, enable toggle, drift
	// state dump, Explorer button) inside the umbrella's global Logs tab.
	CCal_DrawLogsPanel();
}

void SpaceCalibratorPlugin::OnDebugLoggingChanged(bool enabled)
{
	Metrics::enableLogs = enabled;
}

void SpaceCalibratorPlugin::ProvidePresence(WKOpenVR::PresenceComposer &composer)
{
	// Use the thin POD snapshot to avoid including Calibration.h here (which
	// would pull openvr_driver.h and collide with openvr.h already in scope).
	const CCalPresenceSnapshot snap = CCal_GetPresenceSnapshot();

	WKOpenVR::PresenceUpdate u;

	// state values match CalibrationState: 0=None 1=Begin 2=Rotation
	// 3=Translation 4=Editing 5=Continuous 6=ContinuousStandby
	const int kContinuous        = 5;
	const int kContinuousStandby = 6;
	const int kNone              = 0;

	if (snap.state == kContinuous || snap.state == kContinuousStandby) {
		u.priority = 50;
		u.details  = "Live calibration";
		u.state    = std::to_string(snap.sampleProgress) + "/" +
		             std::to_string(snap.sampleTarget) + " samples | continuous";
	} else if (snap.state != kNone) {
		u.priority = 50;
		u.details  = "Calibration setup";
		u.state    = "awaiting reference / target";
	} else if (snap.validProfile) {
		u.priority = 0;
		u.details  = "Fixed offset active";
		u.state    = snap.targetTrackingSystem.empty() ? "manual" : snap.targetTrackingSystem;
	} else if (!snap.referencePoseOk || !snap.targetPoseOk) {
		u.priority = 30;
		u.details  = "Waiting for SteamVR";
		u.state    = "devices not ready";
	} else {
		u.priority = 0;
		u.details  = "Space Calibrator";
		u.state    = "idle";
	}

	composer.Submit("Space Calibrator", std::move(u));
}

namespace openvr_pair::overlay {

std::unique_ptr<FeaturePlugin> CreateSpaceCalibratorPlugin()
{
	return std::make_unique<SpaceCalibratorPlugin>();
}

} // namespace openvr_pair::overlay
