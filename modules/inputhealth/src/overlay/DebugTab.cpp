#include "DebugTab.h"

#include "InputHealthPlugin.h"
#include "SnapshotReader.h"
#include "UiHelpers.h"

#include <imgui/imgui.h>

namespace inputhealth::ui {

void DrawAdvancedTab(InputHealthPlugin &ui)
{
	openvr_pair::overlay::ui::DrawTextWrapped(
		"Power-user toggles and reset controls. Normal use does not need any of "
		"these; reach for them when triaging a misbehaving controller or capturing "
		"data for a bug report.");

	openvr_pair::overlay::ui::DrawSectionHeading("Driver mode");

	if (openvr_pair::overlay::ui::CheckboxWithTooltip(
			"Observe only (suppress corrections)", &ui.pending_config_.diagnostics_only,
			"Driver collects stats but never rewrites input values.\n"
			"Useful to verify detection logic before allowing corrections.")) {
		ui.PushConfigToDriver();
	}

	openvr_pair::overlay::ui::DrawSectionHeading("Reset");
	openvr_pair::overlay::ui::DrawTextWrapped(
		"Reset everything the driver has learned. Per-device JSON profiles on disk "
		"are untouched and will reload on the next driver start.");
	ImGui::Spacing();

	if (ImGui::Button("Reset stats for every device")) {
		ui.SendReset(0, true, false, false);
	}
	openvr_pair::overlay::ui::TooltipForLastItem(
		"Clears Welford / Page-Hinkley / EWMA-min / polar histogram for every device.\n"
		"Keeps learned compensation curves.");

	ImGui::Spacing();
	if (ImGui::Button("Reset stats AND learned compensation for every device")) {
		ui.SendReset(0, true, false, true);
	}
	openvr_pair::overlay::ui::TooltipForLastItem(
		"Nukes everything driver-side. On-disk JSON profiles survive, so the next\n"
		"driver start reloads whatever was last saved.");
}

void DrawLogsTab(InputHealthPlugin &ui)
{
	openvr_pair::overlay::ui::DrawSectionHeading("Live state");
	ImGui::Text("IPC: %s", ui.ipc_.IsConnected() ? "connected" : "disconnected");
	ImGui::Text("Snapshot shmem: %s", ui.reader_.IsOpen() ? "open" : "closed");
	ImGui::Text("publish_tick: %llu", (unsigned long long)ui.reader_.LastPublishTick());

	openvr_pair::overlay::ui::DrawSectionHeading("File locations");
	openvr_pair::overlay::ui::DrawTextWrapped(
		"File logging is always on. Paths are listed verbatim so they can be "
		"copy-pasted into Explorer.");
	ImGui::Spacing();
	ImGui::TextWrapped("Driver:   %%LocalAppDataLow%%\\WKOpenVR\\Logs\\driver_log.<ts>.txt");
	ImGui::TextWrapped("Overlay:  %%LocalAppDataLow%%\\WKOpenVR\\Logs\\overlay_log.<ts>.txt");
	ImGui::TextWrapped("Profiles: %%LocalAppDataLow%%\\WKOpenVR\\profiles\\<hash>.json");
}

} // namespace inputhealth::ui
