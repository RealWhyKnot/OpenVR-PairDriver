#pragma once

#include <string>

// Input path classification shared between the overlay learning engine and
// the driver-side compensation cache. Both sides call ClassifyInputPath() so
// they agree on which paths receive compensation, which are observed-only, and
// which are silently ignored.
//
// Classification is path-string-only and has no runtime dependencies so it
// can be called from the driver detour, the overlay tick, and the test suite
// without linking any SteamVR or UI code.

namespace inputhealth {

enum class PathClass : uint8_t
{
    // Path is a trigger scalar: min/max remapping is appropriate.
    Trigger,

    // Path is an analog stick axis (x or y component): rest-offset + radial
    // deadzone compensation is appropriate.
    StickAxis,

    // Path is a controller button / binary scalar (grip, system, A/B, etc.):
    // rest-offset compensation is appropriate (debounce for booleans).
    ControllerButton,

    // Path is observable in the diagnostics UI but must not be pushed into
    // compensation. Examples: facial expression blendshapes, eye openness,
    // any path under /input/eye or /input/face.
    DiagnosticsOnly,

    // Path is not meaningful for any InputHealth feature. Silently ignored.
    // Examples: pupil dilation, proximity sensors, squeeze click, raw IMU.
    Unsupported,
};

// Returns true for every PathClass for which compensation may be applied.
// DiagnosticsOnly and Unsupported must not be pushed into compensation.
inline bool IsCompensationPath(PathClass cls)
{
    return cls == PathClass::Trigger ||
           cls == PathClass::StickAxis ||
           cls == PathClass::ControllerButton;
}

// Returns true for paths that should be visible in the diagnostics tab but
// must not be learned into persistent compensation.
inline bool IsDiagnosticsOnlyPath(PathClass cls)
{
    return cls == PathClass::DiagnosticsOnly;
}

// Path-string-only heuristic: returns true for analog axes whose nominal
// range is [0, 1] with degradation that drags the peak down or the rest
// floor up. The trigger-remap kernel applies to these.
//
// Covers the obvious "trigger" / "Trigger" naming as well as pressure-
// sensitive analog axes that ship under other names: Index Knuckles
// grip force and grip value (squeeze), trackpad force, generic /force or
// /pressure suffixes, and /squeeze/value paths. Boolean variants
// (/click, /touch) are filtered out so e.g. "trigger/click" is not
// misclassified as analog.
inline bool IsTriggerLikePath(const std::string &path)
{
    if (path.find("/click") != std::string::npos ||
        path.find("/touch") != std::string::npos)
    {
        return false;
    }
    if (path.find("trigger") != std::string::npos ||
        path.find("Trigger") != std::string::npos)
    {
        return true;
    }
    const size_t n = path.size();
    auto endsWith = [&](const char *suffix, size_t len) -> bool {
        return n >= len && path.compare(n - len, len, suffix) == 0;
    };
    if (endsWith("/force",    6)) return true;
    if (endsWith("/pressure", 9)) return true;
    if (path.find("/grip/value")    != std::string::npos) return true;
    if (path.find("/squeeze/value") != std::string::npos) return true;
    return false;
}

// Classify a /input/... path string. The path should be the canonical SteamVR
// input path as reported by CreateBooleanComponent / CreateScalarComponent.
//
// Classification order matters: more-specific checks precede broader ones so
// a path like "/input/trigger/value" resolves to Trigger rather than falling
// through to ControllerButton.
inline PathClass ClassifyInputPath(const std::string &path)
{
    // Empty or non-input paths are not meaningful.
    if (path.empty()) return PathClass::Unsupported;

    // Eye/face expressions: diagnostics only so the UI can show raw values
    // without silently building compensation tables for them.
    if (path.find("/eye") != std::string::npos ||
        path.find("/face") != std::string::npos)
    {
        return PathClass::DiagnosticsOnly;
    }

    // Pupil dilation, proximity, misc unsupported sensors.
    if (path.find("/pupil") != std::string::npos ||
        path.find("proximity") != std::string::npos ||
        path.find("/imu") != std::string::npos)
    {
        return PathClass::Unsupported;
    }

    // Trigger-like analog axes: paths named "trigger", plus pressure-sensitive
    // axes (grip force, grip value, trackpad force, /squeeze/value, generic
    // /force or /pressure suffixes). Boolean variants are filtered inside the
    // helper so "trigger/click" stays a button.
    if (IsTriggerLikePath(path)) return PathClass::Trigger;

    // Analog stick axes: path ends in /x or /y (case-insensitive).
    {
        const size_t n = path.size();
        if (n >= 2) {
            const char slash = path[n - 2];
            const char axis  = path[n - 1];
            if (slash == '/' && (axis == 'x' || axis == 'X' ||
                                  axis == 'y' || axis == 'Y'))
            {
                // Make sure there is a joystick/trackpad/thumbstick stem -- not
                // every path ending in /x is an analog stick (some are booleans
                // masquerading). Accept any path containing "stick", "joystick",
                // "trackpad", "thumbstick", or "touchpad".
                if (path.find("stick")     != std::string::npos ||
                    path.find("joystick")  != std::string::npos ||
                    path.find("trackpad")  != std::string::npos ||
                    path.find("thumbstick") != std::string::npos ||
                    path.find("touchpad")  != std::string::npos)
                {
                    return PathClass::StickAxis;
                }
            }
        }
    }

    // Controller buttons and their derivatives: grip, system, menu, A, B, X, Y,
    // bumper, any remaining click/touch/force paths. Heuristic: accept any
    // /input/ path that does not match the unsupported/eye/face buckets above.
    // The cost of a false positive here is minimal (unnecessary debounce
    // learning on a rarely-pressed path), but a false negative silently drops
    // a real button from correction.
    if (path.find("/input/") != std::string::npos) {
        return PathClass::ControllerButton;
    }

    return PathClass::Unsupported;
}

} // namespace inputhealth
