# Module: Calibration

Keeps a non-lighthouse HMD (Quest, Pimax, etc.) aligned with lighthouse-tracked full-body trackers. Continuously compares poses from both tracking systems, solves a rigid transform between them, and applies that transform to every tracker so they appear in the correct position relative to the headset. Without this module, SteamVR trackers and the HMD live in separate coordinate frames and the user's virtual feet are in a different room than their head.

Forked from the original OpenVR-SpaceCalibrator. The fork keeps the same Basic/Advanced UI shape so existing users can find familiar controls.

Source: [modules/calibration/](https://github.com/RealWhyKnot/OpenVR-WKPairDriver/tree/main/modules/calibration)

## Driver hooks

- `IVRDriverContext::GetGenericInterface` slot 0 -- MinHook detour intercepts the SteamVR driver host queries during driver startup so the calibration module can attach its `TrackedDevicePoseUpdated` interposer.
- `TrackedDevicePoseUpdated` -- the active interposer rewrites pose updates for non-reference devices through the current calibration transform.

## IPC

`\\.\pipe\OpenVR-Calibration` carries calibration-specific requests:

- `RequestSetDeviceTransform` -- updates the active transform for a device.
- `RequestSetAlignmentSpeedParams` -- pushes the speed thresholds shown on the Advanced tab.
- `RequestSetTrackingSystemFallback` -- chooses the fallback reference when the HMD's tracking system is unavailable.
- `RequestDebugOffset` -- single-shot nudge used by diagnostics.

Driver-side pose telemetry is streamed back to the overlay over the `OpenVRPairPoseMemoryV1` shmem segment.

## Overlay UI

### Basic tab

- **Devices** -- read-only table: reference device (system / model / serial + status: OK / NOT FOUND / NOT TRACKING) and target device on the same row.
- **Actions** -- `Cancel Continuous Calibration` ends continuous mode; `Restart sampling` pushes a random offset so the solver re-searches from fresh samples; `Pause updates` / `Resume updates` freezes the live offset without ending continuous mode (amber highlight while paused).
- **Profile-mismatch banner** -- shown in amber if the saved profile was created with a different HMD tracking system. `Clear profile` and `Recalibrate` buttons.

### Advanced tab

Available in continuous mode.

- **Toggles** -- `Hide tracker` (suppress target tracker's pose so calibration setups don't show a duplicate device); `Ignore outliers` (drop sample pairs whose rotation axis disagrees with the consensus).
- **Calibration speeds** -- radio: Auto / Fast (100 samples) / Slow (250) / Very Slow (500). Auto mode shows resolved speed + jitter readouts inline.
- **Speed thresholds** -- Decel / Slow / Fast x Translation (mm) / Rotation (degrees) sliders; right-click to reset each.
- **Alignment speeds** -- Decel / Slow / Fast rate sliders.
- **Thresholds** -- Jitter, Recalibration, Max relative error.
- **Continuous calibration (advanced)** -- Target latency offset (ms, -100..100) + Auto-detect target latency checkbox.
- **Legacy panel** -- "Legacy (pre-fork upstream math)" master toggle for the pre-fork solve path; useful for regression testing.
- **Diagnostics** -- Watchdog reset count + time; HMD long-stall count + time. Counters highlight amber within 15 s of the last event so transient issues are visible during a session.

### Logs tab

Tail of `%LocalAppDataLow%\OpenVR-Pair\Logs\spacecal_log.<ts>.txt`. Aggregated alongside the other modules' logs in the umbrella's global Logs tab.

## Banners / failure modes

- **Profile-mismatch (amber)** -- saved profile's HMD tracking system != current HMD; calibration is not applied. Use the banner's Clear profile button.
- **NOT FOUND** -- device serial not found by SteamVR; tracker is off or not paired.
- **NOT TRACKING** -- device found but not providing valid poses; tracking lost mid-session.
- **Watchdog reset (amber for 15 s)** -- solver has been rejecting every sample for ~25 s; the watchdog discarded the in-flight estimate and restarted collection. Usually means the user took the headset off mid-cal.
- **HMD long-stall (amber for 15 s)** -- HMD stopped reporting poses for ~1.5 s+ (headset removed, runtime hiccup).

## Persistence

- Profile + offsets: `%LocalAppDataLow%\OpenVR-Pair\profiles\<profile>.json` (one profile per HMD tracking system).
- Session log: `%LocalAppDataLow%\OpenVR-Pair\Logs\spacecal_log.<ts>.txt`.

## Tests

`modules/calibration/tests/spacecal_tests.exe` covers the solver, the watchdog, the wedge / corroboration gates, the rest-locked-yaw helper, and the protocol version pin (which the test bumps in lockstep with `Protocol.h`).
