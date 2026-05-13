## Install (fresh)

Download the zip for the feature you want from the appropriate release page and extract it into your SteamVR drivers folder:

- **Room calibration only**: [WKOpenVR-SpaceCalibrator releases](https://github.com/RealWhyKnot/WKOpenVR-SpaceCalibrator/releases/latest) -- zip includes `enable_calibration.flag`.
- **Finger smoothing only**: [WKOpenVR-Smoothing releases](https://github.com/RealWhyKnot/WKOpenVR-Smoothing/releases/latest) -- zip includes `enable_smoothing.flag`.
- **Input health monitoring only**: [WKOpenVR-InputHealth releases](https://github.com/RealWhyKnot/WKOpenVR-InputHealth/releases/latest) -- zip includes `enable_inputhealth.flag`.
- **All features**: download and extract all three zips into the same driver folder; each adds its own flag file.

Or download this repo's `WKOpenVR-{version}.zip` (no flag files) and add the flag files yourself.

Extract into: `<SteamVR runtime>\drivers\01wkopenvr\`. Restart SteamVR. The driver loads the features whose flag files it finds.
