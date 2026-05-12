# Architecture

OpenVR-Pair is a single SteamVR driver DLL + a single overlay binary that compose four feature modules under one process tree.

## Layout

```
OpenVR-WKPairDriver/
  core/
    src/
      common/   -- Protocol.h, shared utilities used by both sides
      driver/   -- SteamVR driver entry (driver_openvrpair.dll); MinHook setup;
                   per-module driver-side activation via FeatureFlags
      overlay/  -- OpenVR-Pair.exe shell: dashboard overlay rendering, tabs,
                   shared ImGui helpers, the Modules tab that toggles flag files
  modules/
    calibration/   -- src/driver/, src/overlay/, tests/
    smoothing/     -- src/driver/, src/overlay/
    inputhealth/   -- src/driver/, src/overlay/, tests/
    facetracking/  -- src/driver/, src/overlay/, src/host/ (.NET 10), tests/
  driver_openvrpair/  -- staging tree (manifest + resources + bin/win64) copied
                         into release zips and the installer
```

`core/` owns process-global state: MinHook installs, the IPC server, the dashboard overlay handle, the umbrella's tab shell. Each `modules/<feature>/` is otherwise independent: its own static lib pair (driver-side + overlay-side), its own pipe name, its own profile file under `%LocalAppDataLow%\OpenVR-Pair\`.

## Shared driver DLL

A SteamVR driver hooks into `vrserver.exe` via MinHook. MinHook is process-global: only one detour can exist per target function. Four separate driver DLLs trying to patch the same slot of `IVRDriverContext::GetGenericInterface` would collide -- the second install silently fails and that driver's detours never fire. The umbrella driver installs each hook once and dispatches inside the detour by feature, so any subset of features can be enabled simultaneously without conflict.

The driver DLL ships in the release zip as `driver_01openvrpair.dll`. The `01` prefix forces SteamVR's alphabetic-sort driver loader to evaluate it after any other drivers, which matters for the calibration pose hook.

## Feature flags

Each feature is gated by a marker file in the driver's resources directory:

| Flag file | Module | What activates |
|---|---|---|
| `enable_calibration.flag` | calibration | pose-update hook + `\\.\pipe\OpenVR-Calibration` |
| `enable_smoothing.flag` | smoothing | skeletal hook + `\\.\pipe\OpenVR-WKSmoothing` |
| `enable_inputhealth.flag` | inputhealth | boolean/scalar input hooks + `\\.\pipe\OpenVR-WKInputHealth` + 10 Hz snapshot shmem |
| `enable_facetracking.flag` | facetracking | host sidecar spawn + `\\.\pipe\OpenVR-FaceTracking` + ~120 Hz frame shmem |

The driver scans the resources directory at startup; flag presence is checked once per SteamVR session. The umbrella overlay's Modules tab manages these files through an elevated PowerShell helper -- because the driver tree lives under `Program Files (x86)\Steam\steamapps\common\SteamVR\drivers\01openvrpair\`, dropping or removing flag files needs an admin token. The toggle takes effect at the next SteamVR launch.

## Process tree at runtime

1. SteamVR loads `driver_01openvrpair.dll` via the normal driver discovery path.
2. The driver scans `resources/enable_*.flag`, decides which feature modules to activate, and opens the corresponding IPC servers.
3. If `enable_facetracking.flag` is present the driver's `HostSupervisor` spawns `OpenVRPair.FaceModuleHost.exe` (the C# .NET 10 sidecar). The host loads hardware-vendor face-tracking modules in collectible `AssemblyLoadContext`s, normalises samples to the Unified Expression set, and writes per-frame data into the shmem ring. Driver restarts the host with exponential backoff if it crashes.
4. The user launches the umbrella overlay (`OpenVR-Pair.exe`). It registers itself as a SteamVR application via the bundled vrmanifest so it can be auto-launched the next time. The overlay connects to each module's IPC pipe, presents per-module tabs, and renders into both the desktop window and the SteamVR dashboard via an offscreen framebuffer + `vr::IVROverlay::SetOverlayTexture`.

## Logs and profiles

All overlay-side, driver-side, and host-side log files land under `%LocalAppDataLow%\OpenVR-Pair\Logs\`, one per module per session:

- `spacecal_log.<ts>.txt`
- `smoothing_log.<ts>.txt`
- `inputhealth_log.<ts>.txt`
- `facetracking_log.<ts>.txt`

Profile / config files live under `%LocalAppDataLow%\OpenVR-Pair\profiles\`. The umbrella's global Logs tab aggregates all four streams; each module's own tab includes a Logs section scoped to that module.

## Dashboard overlay vs window

The umbrella renders into an offscreen GLFW framebuffer first, then submits the same texture to both the desktop GLFW window and a SteamVR dashboard overlay. Either surface is fully functional; closing the desktop window keeps the dashboard overlay alive. The dashboard overlay icon is `core/src/overlay/dashboard_icon.png`.

A virtual keyboard for in-VR text input is not wired up in this version: the pinned ImGui (`82d0584e7`) is master, not docking, and its `InputText` callbacks operate on UTF-8. SteamVR's `ShowKeyboard` wide-char injection path doesn't round-trip through ImGui cleanly at this pin, so users currently need a physical keyboard to edit text fields while in VR.
