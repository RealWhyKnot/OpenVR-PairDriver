# Build environment

## Prerequisites

- **Visual Studio Build Tools 2022** (or the VS 2022 IDE) with the C++ workload installed, plus the Windows 10 SDK component.
- **CMake 4.x** -- the build wraps `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` because the `minhook` submodule pins `cmake_minimum_required(2.8.12)` which CMake 4 rejects.
- **.NET 10 SDK** -- needed by the face-tracking host (`OpenVRPair.FaceModuleHost`). The build is gated by the `OPENVR_PAIR_BUILD_FACE_HOST` CMake option (default `ON`); pass `-DOPENVR_PAIR_BUILD_FACE_HOST=OFF` to skip if the SDK is unavailable. With the host disabled the driver still loads, but the FaceTracking feature stays inert (the supervisor logs once that the host exe is missing).
- **NSIS** -- only needed when building the installer; release CI does this step. Local dev builds skip the installer unless `-Release` is passed.

## One-shot build

```
git clone --recursive https://github.com/RealWhyKnot/WKOpenVR
cd WKOpenVR
./build.ps1
```

`build.ps1` activates `.githooks/` via `core.hooksPath` on first run, stamps `version.txt` and `modules/calibration/src/overlay/BuildStamp.h`, configures via CMake (`-G "Visual Studio 17 2022" -A x64`), runs MSBuild, then stages the C# face-tracking host into the driver tree at `build/driver_openvrpair/resources/facetracking/host/`. Output:

- `build/artifacts/Release/OpenVR-Pair.exe` -- umbrella overlay
- `build/driver_openvrpair/bin/win64/driver_openvrpair.dll` -- shared driver
- `build/driver_openvrpair/resources/facetracking/host/OpenVRPair.FaceModuleHost.exe` -- face-tracking sidecar (when the host build is enabled)

## Incremental builds

`./build.ps1 -SkipConfigure` skips the CMake configure step and just reruns MSBuild. Useful when iterating on a single source file. `./quick.ps1` builds + deploys the driver + overlay + host into the installed SteamVR drivers directory (with the necessary UAC elevation) so the next SteamVR launch picks up the change.

## Release packaging

`./build.ps1 -Release` produces a release-shaped zip + per-file manifest TSV under `release/` alongside the build outputs. The CI workflow (`.github/workflows/release.yml`) does the same staging on a tag push and additionally produces the four per-feature mirror zips. See the [release process](Release-Process) page.

## PowerShell 5.1 gotchas

The default Windows shell is PowerShell 5.1, which wraps every stderr line from a native command as a `NativeCommandError` ErrorRecord. Under `$ErrorActionPreference = "Stop"` that wrap kills the script on the first CMake `message()` line; under `Continue` the lines still render with the noisy `+ CategoryInfo ... NativeCommandError` preamble that buries real diagnostics.

`build.ps1` wraps native commands in `Invoke-NativeQuiet`, which coerces ErrorRecord into plain strings and preserves `$LASTEXITCODE` across the pipe. Reuse this pattern when adding new native invocations to the build scripts.

## Tests

The gtest suite covers calibration, inputhealth, and facetracking math. After a successful build:

```
build/artifacts/Release/spacecal_tests.exe
build/artifacts/Release/facetracking_tests.exe
build/artifacts/Release/inputhealth_tests.exe
```

`release.yml` enumerates every `*_tests.exe` and runs each with `--gtest_brief=1`; any non-zero exit fails the release.
