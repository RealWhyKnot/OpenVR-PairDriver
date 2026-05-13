# WKOpenVR

Umbrella SteamVR overlay + driver for the OpenVR-Pair toolset. One binary (`OpenVR-Pair.exe`) and one driver DLL (`driver_openvrpair.dll`) host four feature modules. Each module can be toggled on or off via a marker flag file the overlay's Modules tab manages.

## Modules

- **Calibration** -- align lighthouse-tracked trackers with a non-lighthouse HMD. ([deep-dive](Module-Calibration))
- **Smoothing** -- finger-curl smoothing and per-tracker pose-prediction control for Valve Index Knuckles. ([deep-dive](Module-Smoothing))
- **InputHealth** -- drift and degradation detection for buttons / axes / fingers, with learned compensation. ([deep-dive](Module-InputHealth))
- **FaceTracking** -- face and eye tracking integration with a C# .NET 10 host sidecar. ([deep-dive](Module-FaceTracking))

## Reference

- [Architecture](Architecture) -- shared driver DLL, module composition, feature flags, marker files.
- [Build](Build) -- toolchain prerequisites, CMake invocation, host opt-out switch.
- [Protocol](Protocol) -- named pipes, shared-memory rings, `Protocol.h` v15 reference, version-bump discipline.
- [Release process](Release-Process) -- how `release.yml` produces the umbrella zip and four per-feature mirror zips, how the mirrors get pushed.
- [Changelog](Changelog) -- auto-appended from conventional-commit subjects; tagged sections promoted on release.

## Source and mirrors

- Source of truth: [WKOpenVR](https://github.com/RealWhyKnot/WKOpenVR) (this repo).
- Per-feature release mirrors, each shipping the umbrella zip with one `enable_<feature>.flag` pre-dropped:
  - [WKOpenVR-SpaceCalibrator](https://github.com/RealWhyKnot/WKOpenVR-SpaceCalibrator)
  - [WKOpenVR-Smoothing](https://github.com/RealWhyKnot/WKOpenVR-Smoothing)
  - [WKOpenVR-InputHealth](https://github.com/RealWhyKnot/WKOpenVR-InputHealth)
  - [WKOpenVR-VRCFT](https://github.com/RealWhyKnot/WKOpenVR-VRCFT)
