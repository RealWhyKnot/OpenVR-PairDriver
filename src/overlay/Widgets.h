#pragma once

#include <functional>

namespace openvr_pair::ui {

enum class Status {
	Idle,
	Learning,
	Ready,
	Warning,
	Danger,
	Disabled,
};

bool StatusPill(const char *label, Status s, bool clickable = false);
void Card(const char *title, const char *subtitle, const std::function<void()> &body_fn);
void SectionHeader(const char *label);
void Stat(const char *label, const char *value, Status accent = Status::Idle);
void ProgressBar(float frac, const char *text, Status state = Status::Learning, float height = 14.0f);
void StatusDot(const char *label, Status s);
bool SidebarItem(const char *icon_glyph, const char *label, bool selected);
void TopBarStatus(const char *label, bool ok);
bool ToggleSwitch(const char *label, bool *value);

} // namespace openvr_pair::ui
