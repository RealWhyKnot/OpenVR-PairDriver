#pragma once

namespace WKOpenVR {

// Initialize Discord Rich Presence. Safe to call when Discord is not running
// or not installed -- logs once on failure, then no-ops for the session.
void DiscordPresence_Init();

// Shut down and release the Discord connection. Call once at overlay exit.
void DiscordPresence_Shutdown();

// Pump Discord SDK callbacks. Call once per frame (or gated to ~2s intervals).
void DiscordPresence_Tick();

// Update the presence state/details strings directly.
void DiscordPresence_SetState(const char* state, const char* details);

// Convenience: set the active module name shown in the presence state.
// Pass nullptr or "" to revert to the idle state.
void DiscordPresence_SetActiveModule(const char* moduleName);

} // namespace WKOpenVR
