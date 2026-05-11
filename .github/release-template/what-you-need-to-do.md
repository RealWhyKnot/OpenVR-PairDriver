## What you need to do

End users: nothing. You only see this release through the OpenVR-WKSpaceCalibrator or OpenVR-WKSmoothing release that bundles it. The consumer overlays' updaters carry the bundled DLL forward.

Maintainers of either consumer overlay: bump the `OpenVR-WKPairDriver` submodule pointer to commit `{commit-sha-short}` (this release's tag), rebuild the overlay, and ship a paired release. The shared-driver version-gate in each consumer's installer compares the bundled DLL's build stamp against whatever is already installed and only overwrites if strictly newer, so a user who has both consumers installed never accidentally downgrades the shared driver by updating one of them with an older bundled DLL.
