#include "CalibrationAnchor.h"

namespace openvr_pair::overlay {

namespace {

std::string g_anchorSerial;

} // namespace

void SetCalibrationAnchorSerial(const std::string &serial)
{
	g_anchorSerial = serial;
}

const std::string &GetCalibrationAnchorSerial()
{
	return g_anchorSerial;
}

} // namespace openvr_pair::overlay
