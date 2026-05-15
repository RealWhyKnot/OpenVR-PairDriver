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

    // Trigger scalars: path contains "trigger" and is a value or click sub-path.
    // Distinguish from "trigger/click" boolean paths: the click path is a button.
    {
        const bool hasTrigger = path.find("trigger") != std::string::npos ||
                                path.find("Trigger") != std::string::npos;
        const bool isClick = path.find("/click") != std::string::npos ||
                             path.find("/touch") != std::string::npos;
        if (hasTrigger && !isClick) return PathClass::Trigger;
    }

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
