#pragma once

#include "Profiles.h"
#include "Protocol.h"
#include "inputhealth/WelfordAccumulator.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class IPCClient;
class ProfileStore;
class SnapshotReader;

struct LearningPathView
{
	uint64_t sample_count = 0;
	uint64_t threshold = 0;
	bool ready = false;
	bool drift_shift_pending = false;
	bool corrections_enabled = true;
	std::string status;
	std::string correction;
	uint64_t last_updated_unix = 0;
};

class LearningEngine
{
public:
	LearningEngine(IPCClient &ipc, ProfileStore &profiles);

	void Tick(const SnapshotReader &reader);
	void Flush();

	void SetRestBurstActive(bool active);
	bool IsRestBurstActive() const { return rest_burst_active_; }

	LearningPathView GetPathView(uint64_t serial_hash, const char *path) const;
	void PushReadyCompensations();
	void SetDeviceCorrectionsEnabled(uint64_t serial_hash, bool enabled);
	void UnlearnPath(uint64_t serial_hash, const char *path);
	void UnlearnDevice(uint64_t serial_hash);

private:
	struct PathState
	{
		uint64_t serial_hash = 0;
		std::string path;
		uint8_t kind = protocol::InputHealthCompScalarSingle;
		inputhealth::WelfordState rest;
		double rest_credit = 0.0;
		double trigger_peak = 0.0;
		double short_mean = 0.0;
		bool short_mean_initialized = false;
		uint64_t last_press_count = 0;
		uint64_t last_press_time_us = 0;
		std::vector<uint64_t> inter_press_us;
		uint64_t sample_count = 0;
		bool ready = false;
		bool drift_shift_pending = false;
		uint64_t drift_exceeded_since_us = 0;
		uint64_t drift_cooldown_until_us = 0;
		uint64_t last_persist_us = 0;
		double learned_rest_offset = 0.0;
		double learned_stddev = 0.0;
		double learned_trigger_min = 0.0;
		double learned_trigger_max = 0.0;
		double learned_deadzone_radius = 0.0;
		uint32_t learned_debounce_us = 0;
		uint64_t last_updated_unix = 0;
		uint32_t drift_shift_resets = 0;
	};

	IPCClient &ipc_;
	ProfileStore &profiles_;
	bool rest_burst_active_ = false;
	std::unordered_map<std::string, PathState> states_;
	std::unordered_map<uint64_t, uint64_t> device_button_quiet_until_us_;

	PathState &StateFor(uint64_t serial_hash, const std::string &path);
	const PathState *FindState(uint64_t serial_hash, const std::string &path) const;
	void SyncProfile(uint64_t serial_hash, PathState &state, bool immediate);
	void PushCompensation(uint64_t serial_hash, const PathState &state, bool enabled);
	// Wraps profiles_.Save() in try/catch; logs on failure and returns false.
	bool TrySaveProfile(const DeviceProfile &profile);
};
