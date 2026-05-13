#pragma once

#include "Protocol.h"

#include <cstdint>
#include <string>
#include <vector>

// Overlay-side settings for the FaceTracking feature.
// Serialised to %LocalAppDataLow%\WKOpenVR\profiles\facetracking.json.
// The driver owns its own per-module calibration files; this file holds
// only settings that the overlay itself needs across sessions.

struct FacetrackingProfile
{
    // --- wire-side settings (mirrored to the driver on connect) ---
    bool    eyelid_sync_enabled        = true;
    bool    eyelid_sync_preserve_winks = true;
    int     eyelid_sync_strength       = 70;   // 0..100

    bool    vergence_lock_enabled      = false;
    int     vergence_lock_strength     = 60;   // 0..100

    int     continuous_calib_mode      = 1;    // 0=off 1=conservative 2=aggressive

    bool    output_osc_enabled         = true;
    std::string osc_host               = "127.0.0.1";
    int     osc_port                   = 9000;

    int     gaze_smoothing             = 30;   // 0..100
    int     openness_smoothing         = 20;   // 0..100

    // Which installed modules the user has toggled on in the Modules tab.
    // Empty list = host picks the first available module on its own; a
    // populated list is the user's explicit selection (multi-select-capable
    // UI; the backend currently activates the first entry until the host
    // is upgraded to run multiple simultaneously). Serialised order is
    // load order -- preserve the user's row order so an upgrade to true
    // multi-run has a stable priority.
    std::vector<std::string> enabled_module_uuids;

    // --- overlay-only preferences ---
    bool    show_raw_values            = false;
    int     last_tab_index             = 0;    // remembers which tab was active
};

class FacetrackingProfileStore
{
public:
    // Load from facetracking.json. Returns true on success. On failure the
    // `current` field retains its default-initialised values.
    bool Load();

    // Write `current` to facetracking.json. Returns true on success.
    bool Save() const;

    FacetrackingProfile current;
};
