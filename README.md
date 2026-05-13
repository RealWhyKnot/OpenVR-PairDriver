# WKOpenVR

Umbrella SteamVR overlay + driver for the OpenVR-Pair toolset. One binary (`OpenVR-Pair.exe`) and one driver DLL (`driver_openvrpair.dll`) host four feature modules under `modules/`:

- **calibration** -- continuous calibration of HMDs against lighthouse-tracked full-body trackers. Forked from the original OpenVR-SpaceCalibrator. Mirrors releases to [WKOpenVR-SpaceCalibrator](https://github.com/RealWhyKnot/WKOpenVR-SpaceCalibrator).
- **smoothing** -- One-Euro finger smoothing and per-device pose-prediction suppression for Valve Index Knuckles. Mirrors releases to [WKOpenVR-Smoothing](https://github.com/RealWhyKnot/WKOpenVR-Smoothing).
- **inputhealth** -- per-button / per-axis / per-finger drift and degradation detection with learned compensation. Mirrors releases to [WKOpenVR-InputHealth](https://github.com/RealWhyKnot/WKOpenVR-InputHealth).
- **facetracking** -- face and eye tracking via a C# .NET 10 host sidecar that loads hardware-vendor modules, normalises against Unified Expressions, and feeds the driver over a shared-memory ring. Mirrors releases to [WKOpenVR-VRCFT](https://github.com/RealWhyKnot/WKOpenVR-VRCFT).

Each feature is wired up at SteamVR startup based on a marker file in the driver's `resources/` directory:

- `enable_calibration.flag` -- pose-update hook + calibration IPC pipe
- `enable_smoothing.flag` -- skeletal hook + smoothing IPC pipe
- `enable_inputhealth.flag` -- boolean / scalar input hooks + input-health IPC pipe
- `enable_facetracking.flag` -- face-tracking host sidecar + IPC pipe + shmem ring

The umbrella overlay's Modules tab toggles these flags at runtime.

## Why one DLL instead of four

A SteamVR driver hooks into `vrserver.exe` via MinHook. MinHook is process-global: only one detour can exist per target function. Four independently-installed driver DLLs trying to patch the same slot of `IVRDriverContext::GetGenericInterface` would collide; the second install silently fails and that driver's detours never fire. Sharing one DLL means one MinHook install per target function regardless of which features are enabled.

## Build

Requires CMake 3.15+, Visual Studio Build Tools 2022 (or the VS 2022 IDE), and submodules initialised. The .NET 10 SDK is needed for the face-tracking host; the build skips that target when the SDK is missing or when `-DOPENVR_PAIR_BUILD_FACE_HOST=OFF` is passed.

```
git clone --recursive https://github.com/RealWhyKnot/WKOpenVR
cd WKOpenVR
./build.ps1
```

Output:

- `build/artifacts/Release/OpenVR-Pair.exe`
- `build/driver_openvrpair/bin/win64/driver_openvrpair.dll`
- `build/driver_openvrpair/resources/facetracking/host/OpenVRPair.FaceModuleHost.exe` (when the host build is enabled)

## Pipes and shared memory

- `\\.\pipe\OpenVR-Calibration` -- calibration overlay <-> driver
- `\\.\pipe\OpenVR-WKSmoothing` -- smoothing overlay <-> driver
- `\\.\pipe\OpenVR-WKInputHealth` -- input-health overlay <-> driver
- `\\.\pipe\OpenVR-FaceTracking` -- face-tracking overlay <-> driver

Plus the shmem ring `OpenVRPairFaceTrackingFrameRingV1` for high-rate samples from the C# host into the driver, and `OpenVRPairInputHealthMemoryV1` for the input-health snapshot stream.

Wire format is defined in [core/src/common/Protocol.h](core/src/common/Protocol.h) at protocol version 15. Each overlay sends only its own request types; the driver routes by request type and rejects messages on the wrong pipe. The handshake fails fast on version skew so a mismatched pair is caught at startup rather than misrouting bytes.

## Documentation

See the [wiki](https://github.com/RealWhyKnot/WKOpenVR/wiki) for architecture overview, per-module deep-dives, protocol reference, build environment notes, and the release pipeline.

## License

GNU General Public License v3.0; see [LICENSE](LICENSE). Project copyright lines and third-party attributions in [NOTICE](NOTICE). Earlier contributions from Justin Li and Hyblocker were originally MIT-licensed and remain available under MIT terms from their origin repos; the combined work in this repository is GPL-3.0 going forward.
