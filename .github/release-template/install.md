## Install (fresh)

Download the zip for the feature you want from the appropriate release page and extract it into your SteamVR drivers folder:

- **Room calibration only**: [OpenVR-WKSpaceCalibrator releases](https://github.com/RealWhyKnot/OpenVR-WKSpaceCalibrator/releases/latest) -- zip includes `enable_calibration.flag`.
- **Finger smoothing only**: [OpenVR-WKSmoothing releases](https://github.com/RealWhyKnot/OpenVR-WKSmoothing/releases/latest) -- zip includes `enable_smoothing.flag`.
- **Input health monitoring only**: [OpenVR-WKInputHealth releases](https://github.com/RealWhyKnot/OpenVR-WKInputHealth/releases/latest) -- zip includes `enable_inputhealth.flag`.
- **All features**: download and extract all three zips into the same driver folder; each adds its own flag file.

Or download this repo's `OpenVR-Pair-{version}.zip` (no flag files) and add the flag files yourself.

Extract into: `<SteamVR runtime>\drivers\01openvrpair\`. Restart SteamVR. The driver loads the features whose flag files it finds.
