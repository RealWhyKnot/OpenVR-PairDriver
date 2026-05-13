## Install (fresh)

Easiest path -- run an NSIS installer (handles Program Files, SteamVR registration, Start Menu shortcut, and the uninstaller). Every release page has both a zip and a `*-Setup.exe`:

- **WKOpenVR (all features available)**: this repo's `WKOpenVR-v{version}-Setup.exe`. No feature is enabled by default; toggle the ones you want from the Modules tab inside WKOpenVR after install.
- **Room calibration only**: [WKOpenVR-SpaceCalibrator releases](https://github.com/RealWhyKnot/WKOpenVR-SpaceCalibrator/releases/latest) -- `WKOpenVR-Calibration-v{version}-Setup.exe` pre-enables calibration.
- **Finger smoothing only**: [WKOpenVR-Smoothing releases](https://github.com/RealWhyKnot/WKOpenVR-Smoothing/releases/latest) -- pre-enables smoothing.
- **Input health monitoring only**: [WKOpenVR-InputHealth releases](https://github.com/RealWhyKnot/WKOpenVR-InputHealth/releases/latest) -- pre-enables input health.
- **Face / eye tracking only**: [WKOpenVR-VRCFT releases](https://github.com/RealWhyKnot/WKOpenVR-VRCFT/releases/latest) -- pre-enables face tracking (.NET 10 host included).

Manual extract path -- download the matching `.zip` from the same release page and extract into `<SteamVR runtime>\drivers\01wkopenvr\`. Each per-feature zip drops its `enable_<feature>.flag` for you; the umbrella zip drops no flags. Restart SteamVR. The driver loads the features whose flag files it finds in `drivers\01wkopenvr\resources\`.
