# Module: InputHealth

Watches every button, trigger, and thumbstick on connected controllers for drift and degradation. Learns what "normal" looks like for each input path (rest position, full-travel range, polar coverage across thumbstick angles) and silently corrects deviations -- pulling a drifting thumbstick back to centre, remapping a trigger whose full range has shrunk. The Diagnostics tab shows what the driver is observing per input path so the user can see drift being detected before it becomes noticeable.

Source: [modules/inputhealth/](https://github.com/RealWhyKnot/WKOpenVR/tree/main/modules/inputhealth)

## Driver hooks

- **`IVRDriverInput` boolean path** -- detour records button-down counts and observed timing.
- **`IVRDriverInput` scalar path** -- detour records min/max/EWMA/Welford stats per path, applies the active compensation curve before forwarding.
- A 10 Hz snapshot timer writes per-path stats into `OpenVRPairInputHealthMemoryV1` so the overlay's Diagnostics tab can display live numbers without RPC chatter.

## Learning model

Per (device serial, input path) the driver maintains:

- **Welford accumulator** for mean + variance of observed values at rest.
- **Page-Hinkley CUSUM** for one-sided drift detection (positive and negative directions tracked separately).
- **EWMA** of the recent max as a P98 surrogate.
- **Polar coverage histogram** for thumbsticks, so a "weak arc" (a sector the stick can't reach) is flagged separately from full-range drift.

State writes are coalesced -- the driver persists learned curves to per-serial JSON only when the curve has moved meaningfully or on clean shutdown. Resetting stats clears the Welford / Page-Hinkley / EWMA / polar state in memory but leaves the persisted curves alone; resetting compensation also wipes the per-device JSON.

## IPC

`\\.\pipe\OpenVR-WKInputHealth`:

- `RequestSetInputHealthConfig` (v10) -- master toggle + compensation family flags (rest-recenter on / off, trigger remap on / off, observe-only on / off).
- `RequestResetInputHealthStats` (v10) -- clear stats for a device or for every device.
- `RequestSetInputHealthCompensation` (v11) -- push a learned compensation snapshot to the driver (used during profile import or after a re-learn pass).

## Overlay UI

### Diagnostics tab (first tab)

Per-device, per-path table. Columns:

- Device serial
- Path string
- Observed scalar range (min..max)
- Hint -- one of `waiting`, `range`, `drift +`, `drift -`, `rest high?`, `max low?`, `weak arc?`, `coverage ok`, `seen`, `no presses`. The hint summarises what the per-path state machine currently sees.

### Settings tab

- `Rest re-center (sticks)` -- enable learned stick rest-offset / deadzone correction.
- `Trigger remap` -- enable learned trigger min / max remapping.

### Advanced tab

- `Observe only (suppress corrections)` -- driver collects stats but never rewrites inputs. Used to verify detection before allowing corrections on a new device.
- **Reset section**
  - `Reset stats for every device` -- clears in-memory state; learned compensation survives.
  - `Reset stats AND learned compensation for every device` -- nukes everything driver-side; on-disk per-device JSON profiles are also removed.

### Logs tab

`%LocalAppDataLow%\OpenVR-Pair\Logs\inputhealth_log.<ts>.txt`.

## Banners / failure modes

- **Error banner: InputHealth driver problem** -- IPC connect failed or driver rejected config; SteamVR not running or `enable_inputhealth.flag` missing.
- **Grey text: Shmem: <error>** -- snapshot shared-memory not yet open. Transient during startup; the banner clears once the 10 Hz publisher starts.

## Persistence

- Per-device learned compensation: `%LocalAppDataLow%\OpenVR-Pair\profiles\inputhealth_<serial_hash>.json`.
- Session log: `%LocalAppDataLow%\OpenVR-Pair\Logs\inputhealth_log.<ts>.txt`.

## Tests

`modules/inputhealth/tests/inputhealth_tests.exe` covers the Welford / Page-Hinkley primitives, the polar coverage detector, the compensation curve composer, and the fail-open path (any unexpected state must let raw input through rather than blocking the user from acting).
