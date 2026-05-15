#pragma once

#include "Config.h"
#include "FeaturePlugin.h"
#include "IPCClient.h"

#include <string>
#include <set>

class SmoothingPlugin final : public openvr_pair::overlay::FeaturePlugin
{
public:
	const char *Name() const override { return "Smoothing"; }
	const char *FlagFileName() const override { return "enable_smoothing.flag"; }
	const char *PipeName() const override { return OPENVR_PAIRDRIVER_SMOOTHING_PIPE_NAME; }

	void OnStart(openvr_pair::overlay::ShellContext &context) override;
	void Tick(openvr_pair::overlay::ShellContext &context) override;
	void DrawTab(openvr_pair::overlay::ShellContext &context) override;
	void DrawLogsSection(openvr_pair::overlay::ShellContext &context) override;

private:
	SmoothingConfig cfg_ = LoadConfig();
	SmoothingIPCClient ipc_;
	std::string connectError_;

	// Cached state for the external-smoothing-tool banner shown in the
	// Prediction sub-tab. Updated by Tick() at most every 5 seconds; the UI
	// reads them directly without further synchronisation since draw +
	// detection both run on the main thread.
	bool externalSmoothingDetected_ = false;
	std::string externalSmoothingToolName_;
	double lastExternalScanSeconds_ = 0.0;

	// Most recent continuous-calibration device locks seen by Tick(). When a
	// serial becomes locked, send smoothness=0 to the driver so any stale value
	// already in that device slot is cleared.
	std::set<std::string> lastKnownCalibrationLocks_;

	void ConnectIfNeeded();
	void SendConfig();                                        // finger smoothing config
	void SendDevicePrediction(uint32_t openVRID, int smoothness); // per-device prediction
	void ReplayDevicePredictions();                           // resend whole map on connect
	void TickExternalToolDetection();
	void TickCalibrationLockClear();                          // zero driver slots when locks change
	void DrawSettingsTab();
	void DrawAdvancedTab();
	void DrawLogsTab();
	void DrawPredictionTab();
	void DrawFingersTab();
};
