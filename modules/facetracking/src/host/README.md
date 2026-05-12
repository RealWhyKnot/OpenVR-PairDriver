# OpenVRPair.FaceModuleHost

This directory contains the C# (.NET 10) host sidecar that loads hardware face and eye tracking
vendor modules, normalises their per-frame output, and publishes frames into the named shared-memory
ring `OpenVRPairFaceTrackingFrameRingV1`. The SteamVR driver reads that ring on its pose-update
path and applies continuous calibration, eyelid sync, and vergence lock before pushing data to
SteamVR inputs and the OSC sender.

To add a hardware module, implement `FaceTrackingModule` (from `OpenVRPair.FaceTracking.ModuleSdk`),
package it as a `.zip` with a `manifest.json` matching the whyknot.dev registry schema, sign the
payload with an Ed25519 key trusted in `trust.json`, and install it under
`%LocalAppDataLow%\OpenVR-Pair\facetracking\modules\<uuid>\<version>\`. The host discovers and
loads new modules on startup and responds to `SelectModule` messages over the driver control pipe.
Existing upstream VRCFaceTracking `ExtTrackingModule` implementations can be ported via the thin
shim in `OpenVRPair.FaceTracking.VrcftCompat`.
