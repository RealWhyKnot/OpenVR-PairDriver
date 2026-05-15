#include "CalibrationAnchor.h"

namespace openvr_pair::overlay {

namespace {

std::string g_anchorSerial;
std::vector<openvr_pair::overlay::CalibrationDeviceLock> g_locks;

} // namespace

void SetCalibrationDeviceLocks(const std::vector<CalibrationDeviceLock> &locks)
{
	g_locks.clear();
	g_anchorSerial.clear();

	for (const auto &lock : locks) {
		if (lock.serial.empty()) continue;

		bool duplicate = false;
		for (const auto &existing : g_locks) {
			if (existing.serial == lock.serial) {
				duplicate = true;
				break;
			}
		}
		if (duplicate) continue;

		if (lock.kind == CalibrationDeviceLockKind::Reference &&
			g_anchorSerial.empty()) {
			g_anchorSerial = lock.serial;
		}
		g_locks.push_back(lock);
	}
}

bool TryGetCalibrationDeviceLockKind(const std::string &serial,
	CalibrationDeviceLockKind &kind)
{
	for (const auto &lock : g_locks) {
		if (lock.serial == serial) {
			kind = lock.kind;
			return true;
		}
	}
	return false;
}

void SetCalibrationAnchorSerial(const std::string &serial)
{
	if (serial.empty()) {
		SetCalibrationDeviceLocks({});
		return;
	}

	SetCalibrationDeviceLocks({
		CalibrationDeviceLock{ serial, CalibrationDeviceLockKind::Reference }
	});
}

const std::string &GetCalibrationAnchorSerial()
{
	return g_anchorSerial;
}

} // namespace openvr_pair::overlay
