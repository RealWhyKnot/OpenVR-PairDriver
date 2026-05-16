# Module: FaceTracking

Feeds live face and eye data from a hardware tracker (Quest Pro face sensors, Vive Facial Tracker, similar) into SteamVR and VRChat. A C# .NET 10 sidecar process loads the vendor module, normalises output against the Unified Expression set, and writes per-frame data into a shared-memory ring at ~120 Hz. The driver applies continuous calibration, eyelid sync (reduces asymmetric flicker), and vergence lock (corrects crossed-eye artefacts from per-eye drift) before publishing.

Source: [modules/facetracking/](https://github.com/RealWhyKnot/WKOpenVR/tree/main/modules/facetracking)

## V1 SDK gap

The pinned `openvr_driver.h` does not expose `IVRDriverInput::CreatePoseComponent` or `Prop_HasEyeTracking_Bool` / `Prop_HasGazeTracking_Bool`. Until the openvr submodule is bumped, native OpenXR `XR_EXT_eye_gaze_interaction` output is not wired:

- Eye gaze is published via `TrackedDevicePoseUpdated` on a `TrackedDeviceClass_GenericTracker` device.
- Only openness + pupil dilation scalars and the left-eye pose go through public input components.
- The OSC path through the C# host remains the primary VRChat route.

Several Advanced-tab readouts (host status, native status, OSC status, focus distance, IPC estimate) show `n/a` for the same reason; they're V1 stubs pending driver telemetry plumbing.

## Three-process pipeline

1. **`OpenVRPair.FaceModuleHost.exe`** (C# .NET 10, sidecar) -- spawned by the driver's `HostSupervisor` with exponential-backoff restart. Loads vendor `FaceTrackingModule` DLLs in collectible `AssemblyLoadContext`s, normalises output to Unified Expressions, writes 67 calibration shape values + per-eye openness + per-eye pupil dilation into the shmem ring under seqlock with `Volatile.Read` / `Volatile.Write`. Sends OSC to VRChat (default `127.0.0.1:9000`) and broadcasts via mDNS unless the discovery toggle is off.
2. **`driver_wkopenvr.dll`** -- the driver's `FaceFrameReader` owns the shmem ring lifecycle. `CalibrationEngine` learns observed per-shape min/max envelopes and remaps live samples to the [0, 1] normalised range. `VergenceLock` reconstructs the focus distance via skew-line midpoint and corrects per-eye drift. `EyelidSync` mirrors the higher-confidence eye to suppress asymmetric flicker while preserving intentional winks. The processed sample is published via `TrackedDevicePoseUpdated`.
3. **`WKOpenVR.exe` (overlay)** -- the FaceTracking tab manages the C# host's runtime config through the driver pipe; the driver forwards control-channel writes to the host so the user doesn't need to know about the inter-process detail.

## IPC

`\\.\pipe\WKOpenVR-FaceTracking`:

- `RequestSetFaceTrackingConfig` -- master enable, sync / lock strengths, smoothing factors, OSC enable + host + port, native enable, mDNS toggle.
- `RequestSetFaceCalibrationCommand` -- begin / end / save / reset-all / reset-eye / reset-expr (enum values 0..5).
- `RequestSetFaceActiveModule` -- module UUID forwarded to the host control channel.

Shmem ring: `WKOpenVRFaceTrackingFrameRingV2`, 32 slots, seqlock with magic + version validation on attach.

## Overlay UI

### Settings tab

- `Enable Face Tracking` master toggle.
- **Eyelid Sync** -- on/off, Sync Strength (0..100%), Preserve intentional winks.
- **Vergence Lock** -- on/off, Lock Strength (0..100%).
- **Output**
  - `OSC (VRChat)` toggle + host text field + port int field. OSC status shows `n/a` in V1.
  - `Native (OpenXR eye-gaze)` toggle. Native status shows `n/a` in V1.

### Calibration tab

- **Continuous calibration** -- Mode combo (Off / Conservative / Aggressive), Pause / Resume button, Reset learned data (with confirmation popup).
- **Vergence readout** -- Focus distance + IPD estimate. Both `n/a` in V1.
- **Shape readiness grid** -- 65 dots (63 Unified Expressions + Eye-Openness-L + Eye-Openness-R). Grey = cold (under 200 samples); green = warm. Hover shows the shape name.

### Modules tab

- **Installed modules** -- placeholder while the host channel is wired up.
- `Select active module by UUID` -- text input + Apply button.
- **Trust and security**
  - `Enable unsigned modules (developer mode)` -- with confirmation popup.
  - `Add trusted publisher` -- paste a base64 Ed25519 public key.

### Advanced tab

- **Signal smoothing** -- Gaze smoothing slider, Openness smoothing slider.
- **Host process** -- Host status (`n/a` in V1), Restart host process button.
- **OSC discovery** -- `Disable mDNS VRChat discovery`.
- **Value preview** -- `Show raw (un-calibrated) values`.

### Logs section

Tail of `%LocalAppDataLow%\WKOpenVR\Logs\facetracking_log.<ts>.txt`. Driver, overlay, and host all append to the same per-day file with `FileShare.ReadWrite | Delete` so each process can write without locking the others out.

## Banners / failure modes

- **Error banner (FacetrackingPlugin)** -- `Not connected to the FaceTracking driver. Is SteamVR running?` (or other IPC error text).
- **FaceTracking driver protocol mismatch during heartbeat** -- driver and overlay are different versions; reinstall both.
- Every `n/a (... not wired in V1)` notice is an expected placeholder, not an error.

## Persistence

- Settings: `%LocalAppDataLow%\WKOpenVR\profiles\facetracking.json` (overlay-owned).
- Per-module learned calibration: `%LocalAppDataLow%\WKOpenVR\profiles\facetracking_calib_<module_uuid>.json` (driver-owned, single-writer; flushed on `FaceCalibSave` and on clean driver shutdown).
- Trust list: `%LocalAppDataLow%\WKOpenVR\facetracking\trust.json` (host-owned).
- Module install dir: `%LocalAppDataLow%\WKOpenVR\facetracking\modules\<uuid>\<version>\`.
- Session log: `%LocalAppDataLow%\WKOpenVR\Logs\facetracking_log.<ts>.txt`.

## Math reference

### Continuous calibration (`CalibrationEngine`)

Per shape (63 expressions + 2 eye openness + 2 pupil dilation = 67):

- P-square percentile algorithm tracks P02 directly as a robust lower bound.
- EMA-max envelope tracks P98 as the upper bound (hold-time-gated to avoid latching onto spikes).
- Asymmetric decay: `alpha_up = 0.02`, `alpha_down = 0.0005`. The envelope expands quickly when a new max is observed and contracts slowly so a brief facial extreme doesn't get rolled back immediately.
- Cold-start passthrough: outputs raw values until 200 samples have been observed for that shape.
- Velocity gate: 4-sigma threshold rejects single-frame glitches.
- Hold-time gate: a candidate new max requires 6 consecutive accepted frames AND a >= 80 ms window.
- Frame-age gate: samples older than 33 ms (more than one host frame) are dropped.

### Vergence lock (`VergenceLock`)

Reconstructs focus distance as the skew-line midpoint of the two eye-gaze rays.

- Focus distance clamped to `[0.10 m, 20 m]`.
- Parallel-gaze guard: `denom < 1e-6` (rays effectively parallel) -> bypass without correction.
- Eye-dropout fallback: when one eye's confidence drops below 0.3 or its frame age exceeds 100 ms, the surviving eye's gaze drives both. The dropped eye's direction is lerped toward the surviving one; an earlier draft of this code made the rays parallel inside the dropout block, tripping the parallel guard and silently voiding the correction. The fix lerps then writes the lerped direction directly so the parallel guard is satisfied by intent rather than accident.

### Eyelid sync (`EyelidSync`)

Confidence-weighted blend of the two eyes' openness:

- Both-eyes confidence > 0.6 is the threshold for an intentional-wink bypass (so winks are preserved through the sync).
- Wink threshold: asymmetry above 0.45 + a 120 ms dwell counts as an intentional wink.
- Temporal smoothing: 80 ms low-pass on the blended value to suppress single-frame jitter.

## Tests

`modules/facetracking/tests/facetracking_tests.exe` covers `CalibrationEngine` warmup / range mapping / reset / cold-start flags, `VergenceLock` convergence / parallel-bypass / invalid-flag / dropout, and `EyelidSync` sync / wink preservation / single-frame glitch smoothing.
