#pragma once

namespace openvr_pair::overlay {

// RunFirstLaunchMigration -- idempotent one-time data migration for the
// OpenVR-Pair -> WKOpenVR product rename.
//
// Call BEFORE any config-loading code runs on each overlay launch.  The
// function short-circuits immediately once the new paths / keys already exist,
// so the overhead on subsequent launches is negligible (one CreateDirectory
// call + two RegOpenKeyEx calls per start).
//
// Migrations performed:
//   1. AppData tree  -- if %LocalAppDataLow%\WKOpenVR\ does not exist but
//      %LocalAppDataLow%\OpenVR-Pair\ does, recursively copies the old tree to
//      the new location (skip_existing so a partial prior copy is safe).
//      The old tree is left in place for the user to remove manually.
//   2. SC registry   -- if HKCU\Software\WKOpenVR-SpaceCalibrator does not
//      exist but HKCU\Software\OpenVR-WKSpaceCalibrator does, copies all
//      values and subkeys from old to new via RegCopyTreeW (unprivileged
//      HKCU copy).  Old key left intact.
void RunFirstLaunchMigration();

} // namespace openvr_pair::overlay
