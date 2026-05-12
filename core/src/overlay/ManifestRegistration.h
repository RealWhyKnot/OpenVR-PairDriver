#pragma once

namespace openvr_pair::overlay {

// One-shot registration of OpenVR-Pair's vrmanifest with SteamVR. Initializes
// the OpenVR runtime in utility mode, registers manifest.vrmanifest (sitting
// next to OpenVR-Pair.exe) if the app key is not already installed, and sets
// autolaunch so SteamVR opens the overlay on the next session start. Best-
// effort removal of the legacy steam.overlay.3368750 registration (the SC
// standalone exe that no longer exists) keeps the SteamVR overlay list clean.
//
// Safe to call whether or not SteamVR is running -- the function no-ops if
// the runtime cannot be reached.
void RegisterApplicationManifest();

// Best-effort removal of the OpenVR-Pair vrmanifest registration. Called by
// the NSIS uninstaller via "OpenVR-Pair.exe --unregister-only" BEFORE the
// installed exe + manifest files are deleted, so SteamVR does not end up
// trying to autolaunch a missing binary on the next session.
void UnregisterApplicationManifest();

} // namespace openvr_pair::overlay
