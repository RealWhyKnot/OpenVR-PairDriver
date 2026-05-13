#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Per-device-serial profile persistence.
//
// Stored as `%LocalAppDataLow%\WKOpenVR\profiles\<serial>.json`,
// one file per device serial. Each file holds the wizard's "start fresh"
// baseline (factory-norm references), the user's per-category opt-in
// toggles, and the most recently observed health classification. Profiles
// are written when the wizard completes a step or when a passive detection
// crosses a threshold; they are read at startup to seed the diagnostics
// view before the driver has published its first snapshot.
//
// Only fields needed by the diagnostics tab and learned compensation pipeline
// are stored at this stage.

struct LearnedPathRecord
{
	std::string path;
	std::string kind;
	uint64_t    sample_count = 0;
	bool        ready = false;
	double      learned_rest_offset = 0.0;
	double      learned_stddev = 0.0;
	double      learned_trigger_min = 0.0;
	double      learned_trigger_max = 0.0;
	double      learned_deadzone_radius = 0.0;
	uint32_t    learned_debounce_us = 0;
	uint64_t    last_updated_unix = 0;
	uint32_t    drift_shift_resets = 0;
};

struct DeviceProfile
{
	std::string serial;       // empty if the profile is from a deleted device
	uint64_t    serial_hash;  // FNV-1a 64 of `serial`
	std::string display_name; // last-known controller model name; UI hint only

	// Preferences. Defaults match the driver-side InputHealthConfig
	// defaults so a freshly-created profile does not surprise the user.
	bool enable_diagnostics_only = true;
	bool enable_rest_recenter    = false;
	bool enable_trigger_remap    = false;
	bool corrections_enabled     = true;

	std::vector<LearnedPathRecord> learned_paths;
};

class ProfileStore
{
public:
	// Load every existing profile from disk. Idempotent; a second call
	// re-scans the profiles directory and merges new entries.
	void LoadAll();

	// Persist `profile` to disk. Creates the profiles directory if
	// missing. Returns true on success; on failure the in-memory copy is
	// retained so the UI can show "unsaved" state instead of dropping the
	// edit silently.
	bool Save(const DeviceProfile &profile);

	// Return a profile by serial-hash. Creates a default-initialized
	// profile if none exists, so the diagnostics tab can call this once
	// per visible device without checking for existence.
	DeviceProfile &GetOrCreate(uint64_t serial_hash);

	// Read-only iteration. Useful when the wizard wants to enumerate
	// every known device the user has previously calibrated.
	const std::unordered_map<uint64_t, DeviceProfile> &All() const { return profiles_; }

private:
	std::unordered_map<uint64_t, DeviceProfile> profiles_;
};
