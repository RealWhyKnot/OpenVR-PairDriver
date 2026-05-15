#include "InputHealthCompensation.h"

#include "ServerTrackedDeviceProvider.h"

#include <algorithm>
#include <cmath>

namespace {

float ClampFloat(float value, float lo, float hi)
{
	return std::max(lo, std::min(value, hi));
}

bool PathContainsTrigger(const std::string &path)
{
	return path.find("trigger") != std::string::npos ||
		path.find("Trigger") != std::string::npos;
}

} // namespace

namespace inputhealth {

float ApplyScalarCompensation(
	ServerTrackedDeviceProvider *driver,
	const protocol::InputHealthCompensationEntry &entry,
	const ComponentStats &stats,
	float rawValue,
	float partnerValue,
	bool hasPartner,
	const std::string &partnerPath)
{
	const bool isStick = entry.kind == protocol::InputHealthCompStickX ||
		entry.kind == protocol::InputHealthCompStickY;
	const bool isTrigger = PathContainsTrigger(stats.path);

	if (isTrigger) {
		// Trigger range remap: stretch [trigger_min, trigger_max] onto [0, 1].
		// Do NOT subtract learned_rest_offset first -- trigger_min already
		// encodes the resting floor, so double-subtracting it would push a
		// released trigger below zero and saturate to negative clamp.
		if (entry.learned_trigger_min > 0.0f || entry.learned_trigger_max > 0.0f) {
			const float maxValue = entry.learned_trigger_max > 0.0f
				? entry.learned_trigger_max : 1.0f;
			const float range = std::max(0.001f, maxValue - entry.learned_trigger_min);
			const float remapped = (rawValue - entry.learned_trigger_min) / range;
			return ClampFloat(remapped, 0.0f, 1.0f);
		}
		return ClampFloat(rawValue, 0.0f, 1.0f);
	}

	// Sticks and generic scalars: subtract rest offset, then apply radial
	// deadzone for paired stick axes.
	float value = rawValue - entry.learned_rest_offset;

	if (isStick && entry.learned_deadzone_radius > 0.0f) {
		float radialPartner = partnerValue;
		if (hasPartner && stats.device_serial_hash != 0 && !partnerPath.empty()) {
			protocol::InputHealthCompensationEntry partnerEntry{};
			if (driver->LookupInputHealthCompensation(stats.device_serial_hash, partnerPath, partnerEntry)) {
				radialPartner = partnerValue - partnerEntry.learned_rest_offset;
			}
		}
		const float radius = hasPartner
			? std::sqrt(value * value + radialPartner * radialPartner)
			: std::fabs(value);
		if (radius < entry.learned_deadzone_radius) value = 0.0f;
	}

	if (isStick) return ClampFloat(value, -1.0f, 1.0f);
	return value;
}

bool ShouldSwallowBooleanUpdate(
	const ComponentStats &stats,
	const protocol::InputHealthCompensationEntry &entry,
	bool newValue,
	uint64_t nowUs)
{
	return entry.kind == protocol::InputHealthCompBoolean
		&& entry.learned_debounce_us != 0
		&& newValue != stats.last_boolean
		&& stats.last_committed_us != 0
		&& nowUs - stats.last_committed_us < entry.learned_debounce_us;
}

} // namespace inputhealth
