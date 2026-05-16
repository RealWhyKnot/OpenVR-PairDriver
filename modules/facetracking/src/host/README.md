# OpenVRPair.FaceModuleHost

This directory contains the C# (.NET 10) host sidecar that loads hardware face and eye tracking
vendor modules, normalises their per-frame output, and publishes frames into the named shared-memory
ring `WKOpenVRFaceTrackingFrameRingV2`. The SteamVR driver reads that ring on its pose-update
path and applies continuous calibration, eyelid sync, and vergence lock before pushing data to
SteamVR inputs and the OSC sender.

To add a hardware module, implement `FaceTrackingModule` (from `OpenVRPair.FaceTracking.ModuleSdk`),
package it as a `.zip` with a `manifest.json` matching the v1 schema served at
`legacy-registry.whyknot.dev`, and install it under
`%LocalAppDataLow%\WKOpenVR\facetracking\modules\<uuid>\<version>\`. The host discovers and
loads new modules on startup and responds to `SelectModule` messages over the driver control pipe.
Modules are integrity-checked at install time via the manifest's `payload_sha256`; there is no
cryptographic signature, because both the curated legacy mirror and the future native registry are
maintainer-controlled.

Existing upstream VRCFaceTracking `ExtTrackingModule` implementations are wrapped at runtime by
`OpenVRPair.FaceTracking.VrcftCompat.ReflectingExtTrackingModuleAdapter` -- no per-module C# is
required.
