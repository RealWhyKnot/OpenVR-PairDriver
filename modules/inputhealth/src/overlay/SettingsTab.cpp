#include "SettingsTab.h"

#include "InputHealthPlugin.h"
#include "UiHelpers.h"

#include <imgui/imgui.h>

namespace inputhealth::ui {

void DrawSettingsTab(InputHealthPlugin &ui)
{
	openvr_pair::overlay::ui::DrawSectionHeading("Compensation families");

	if (openvr_pair::overlay::ui::CheckboxWithTooltip(
			"Rest re-center (sticks)", &ui.pending_config_.enable_rest_recenter,
			"Enables learned stick rest-offset and deadzone correction for paths\n"
			"that have enough clean samples. Off = observe without applying this\n"
			"compensation family.")) {
		ui.PushConfigToDriver();
		ui.SaveGlobalConfig();
	}

	if (openvr_pair::overlay::ui::CheckboxWithTooltip(
			"Trigger remap", &ui.pending_config_.enable_trigger_remap,
			"Enables learned trigger min/max remapping for paths that have enough\n"
			"range data. Off = observe without applying this compensation family.")) {
		ui.PushConfigToDriver();
		ui.SaveGlobalConfig();
	}
}

} // namespace inputhealth::ui
