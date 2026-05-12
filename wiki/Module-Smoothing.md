# Module: Smoothing

Two independent features for Valve Index Knuckles controllers, both controllable per-tracker / per-finger from the Settings tab:

- **Finger smoothing** -- a per-bone slerp filter on the skeleton data so hand poses sent to VRChat (and any other `/input/skeleton` consumer) are smoothed rather than jittery.
- **Pose-prediction suppression** -- dials down how aggressively SteamVR extrapolates each tracker's position. Helpful for IMU-based trackers (SlimeVR, Quest controller passthrough) that feel jittery from over-eager prediction.

Source: [modules/smoothing/](https://github.com/RealWhyKnot/OpenVR-WKPairDriver/tree/main/modules/smoothing)

## Driver hooks

- **`IVRDriverInput` skeletal path** -- detour intercepts skeletal bone arrays before they're forwarded to consumers; applies the per-finger slerp using the global + per-finger strength sliders.
- **Pose-prediction path** -- per-device suppression scales the extrapolation delta SteamVR applies to a pose before downstream consumers see it.

## IPC

`\\.\pipe\OpenVR-WKSmoothing`:

- `RequestSetFingerSmoothing` -- enabled mask + global strength + per-finger strength array.
- `RequestSetDevicePrediction` -- per-device prediction smoothness (split off `SetDeviceTransform` at protocol v12 so smoothing didn't have to share an IPC schema with calibration).

## Overlay UI

### Settings tab

**Prediction**

- Status pill: green `No external smoothing tool detected` or amber `DETECTED: <name> is running.` The amber state warns that running two smoothing layers stacked is unsupported.
- Per-tracker smoothness sliders (0..100%) -- one row per tracked device, dynamically generated from the live device list. HMD locked at 0; 0 = raw motion, 100 = fully suppressed.

**Finger smoothing**

- `Enable finger smoothing` -- master toggle. Off means bone arrays pass through untouched.
- `Strength` slider (0..100%) -- global slerp strength baseline.
- Per-finger enable grid (2 rows x 5 columns: Left/Right x Thumb/Index/Middle/Ring/Pinky) with `Enable all` / `Disable all` shortcuts.
- Per-finger strength grid -- same 2 x 5 shape; cell value 0..100. A cell at 0 uses the global Strength instead of its own.

### Logs tab

`%LocalAppDataLow%\OpenVR-Pair\Logs\smoothing_log.<ts>.txt`.

## Banners / failure modes

- **Error banner: Smoothing driver connection failed** -- driver pipe not reachable; SteamVR not running or `enable_smoothing.flag` absent.
- **Amber: External tool conflict** -- a known smoothing tool process is running alongside this module. The pill names the conflicting process.

## Persistence

- Settings: `%LocalAppDataLow%\OpenVR-Pair\profiles\smoothing.txt`.
- Session log: `%LocalAppDataLow%\OpenVR-Pair\Logs\smoothing_log.<ts>.txt`.

## Hot-path notes

The per-bone slerp is constant-time per bone; the driver applies it inline in the skeletal hook so there's no extra cross-thread handoff. Per-finger strength is read out of a small lookup table that's swapped atomically by the IPC consumer thread, so config changes apply on the very next frame without locking.
