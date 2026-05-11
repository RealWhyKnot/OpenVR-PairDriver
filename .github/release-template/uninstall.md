## Uninstall

Run the uninstaller of whichever consumer overlay you installed (OpenVR-WKSpaceCalibrator and / or OpenVR-WKSmoothing). They each remove their own `enable_*.flag`; with neither flag present the shared driver loads but stays inert.

To remove the shared driver folder entirely after both consumers are uninstalled, delete `<SteamVR>\drivers\01openvrpair\` while SteamVR is closed.
