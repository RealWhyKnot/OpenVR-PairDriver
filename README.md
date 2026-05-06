# OpenVR-PairDriver

The shared SteamVR driver DLL for the OpenVR-SpaceCalibrator + OpenVR-Smoothing pair.

Both consumer projects pull this in as a git submodule, build the same `driver_openvrpair.dll`, and install it to `<SteamVR>\drivers\01openvrpair\`. The driver decides at startup which subsystems to wire up by checking for marker files in its `resources/` folder:

- `enable_calibration.flag` -> install the pose-update hook + open the calibration IPC pipe
- `enable_smoothing.flag` -> install the skeletal hook + open the smoothing IPC pipe

Either consumer's installer drops its own flag. With both installed, the driver wires up both subsystems and runs each over its own pipe; with one installed, the other code path is dormant and the unused hook is never patched.

## Why one DLL instead of two

A SteamVR driver hooks into `vrserver.exe` via MinHook. MinHook is process-global -- only one detour can exist per target function. Two independently-installed driver DLLs both trying to patch `IVRDriverContext::GetGenericInterface` slot 0 would collide; the second install fails silently and that driver's detours never fire. Sharing one DLL means one MinHook install per target function, regardless of which features are enabled.

## Repos in the pair

- [OpenVR-SpaceCalibrator](https://github.com/RealWhyKnot/OpenVR-SpaceCalibrator) -- calibration overlay, drops `enable_calibration.flag`.
- [OpenVR-Smoothing](https://github.com/RealWhyKnot/OpenVR-Smoothing) -- finger-smoothing overlay, drops `enable_smoothing.flag`.

## Build

Requires CMake 3.15+, MSVC (Visual Studio 2022 recommended), and submodules initialized.

```
git clone --recursive https://github.com/RealWhyKnot/OpenVR-PairDriver
cd OpenVR-PairDriver
./build.ps1
```

Output lands at `build/driver_openvrpair/bin/win64/driver_openvrpair.dll`.

## Pipes

- `\\.\pipe\OpenVR-Calibration` -- calibration overlay <-> driver
- `\\.\pipe\OpenVR-Smoothing` -- smoothing overlay <-> driver

Wire format defined in [src/common/Protocol.h](src/common/Protocol.h). Each consumer's overlay sends only its own request types; the driver routes by request type and rejects messages on the wrong pipe.

## License

MIT, see [LICENSE](LICENSE).
