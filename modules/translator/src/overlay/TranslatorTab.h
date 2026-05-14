#pragma once

class TranslatorPlugin;

namespace translator::ui {

// Draws the main translator settings tab content.
// Called by TranslatorPlugin::DrawTab() after the tab bar is set up.
void DrawTranslatorTab(TranslatorPlugin &plugin);

} // namespace translator::ui
