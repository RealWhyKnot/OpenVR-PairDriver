#pragma once

#include <string>

// In-process shared state: the serial string of the device currently acting as
// the calibration reference (anchor). Written by the calibration overlay on
// each scan tick; read by the smoothing overlay to lock out per-tracker
// prediction-suppression on the anchor device.
//
// Both overlays run in the same process (OpenVRPairOverlay). The two calls are
// always made from the main thread (draw + tick share a thread), so no
// synchronisation is required beyond a plain std::string.

namespace openvr_pair::overlay {

// Set the current calibration anchor serial. Pass an empty string when the
// calibration module is not running or has no valid reference device.
void SetCalibrationAnchorSerial(const std::string &serial);

// Return the most recently set anchor serial. Empty string means no anchor is
// currently known (calibration not running or reference not resolved).
const std::string &GetCalibrationAnchorSerial();

} // namespace openvr_pair::overlay
