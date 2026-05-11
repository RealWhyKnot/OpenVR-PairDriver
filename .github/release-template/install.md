## Install (fresh)

End users do not install this driver directly. It ships inside the consumer overlays as a submodule, and either overlay's installer drops the matching `enable_*.flag` to wire up its features at SteamVR startup.

- For room calibration: install [OpenVR-WKSpaceCalibrator](https://github.com/RealWhyKnot/OpenVR-WKSpaceCalibrator/releases/latest).
- For finger smoothing on Index Knuckles: install [OpenVR-WKSmoothing](https://github.com/RealWhyKnot/OpenVR-WKSmoothing/releases/latest).
- Install both for both features. The shared driver is a single DLL under `<SteamVR>\drivers\01openvrpair\` regardless; only one MinHook install per target function, no conflict.

If you maintain a build of either consumer, bump that repo's `OpenVR-WKPairDriver` submodule pointer to commit `{commit-sha-short}` (this release's tag) and rebuild. The bundled DLL in your installer will then match what shipped here.
