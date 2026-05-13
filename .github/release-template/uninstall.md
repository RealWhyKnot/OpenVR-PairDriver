## Uninstall

Delete the flag file(s) for the feature(s) you want to disable from `<SteamVR runtime>\drivers\01wkopenvr\resources\`:

- `enable_calibration.flag` -- disables calibration
- `enable_smoothing.flag` -- disables finger smoothing
- `enable_inputhealth.flag` -- disables input health monitoring

Restart SteamVR. With no flag files present the driver loads but stays inert (no hooks, no pipes).

To remove the driver entirely: delete `<SteamVR runtime>\drivers\01wkopenvr\` while SteamVR is closed.
