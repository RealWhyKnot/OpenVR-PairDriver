#pragma once

#include <cstdint>

// Runtime feature detection. The driver looks for marker files in its own
// resources directory at Init() and only wires up the matching subsystems.
// Each consumer overlay's installer drops the appropriate flag:
//
//   resources/enable_calibration.flag  -- OpenVR-SpaceCalibrator
//   resources/enable_smoothing.flag    -- OpenVR-Smoothing
//
// Either, both, or neither may be present. With neither present the driver
// loads but stays inert (no hooks installed, no pipes opened, no shmem
// segment) so SteamVR's auto-load doesn't break for users who installed the
// shared driver without either consumer.

namespace pairdriver {

constexpr uint32_t kFeatureCalibration = 1u << 0;
constexpr uint32_t kFeatureSmoothing   = 1u << 1;

// Returns the bitwise OR of detected feature flags. Logs the path it scanned
// and the result to the driver log so install issues are easy to diagnose.
uint32_t DetectFeatureFlags();

} // namespace pairdriver
