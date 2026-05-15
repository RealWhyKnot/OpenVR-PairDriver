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
// Called by PresenceComposer; not normally called from plugin code.
void DiscordPresence_SetState(const char* state, const char* details);

} // namespace WKOpenVR
