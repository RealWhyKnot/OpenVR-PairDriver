#define _CRT_SECURE_NO_DEPRECATE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "ActionBindings.h"
#include "Logging.h"

#include <openvr.h>

#include <string>

// ---------------------------------------------------------------------------
// Manifest path resolution
// ---------------------------------------------------------------------------

std::string ActionBindings::ResolveManifestPath()
{
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string path(buf);
    auto sep = path.find_last_of("/\\");
    if (sep != std::string::npos) path = path.substr(0, sep);
    path += "\\actions.json";
    return path;
}

bool ActionBindings::Register(const std::string &manifest_path)
{
    auto *input = vr::VRInput();
    if (!input) {
        TH_LOG("[actions] VRInput() not available");
        return false;
    }

    vr::EVRInputError err = input->SetActionManifestPath(manifest_path.c_str());
    if (err != vr::VRInputError_None) {
        TH_LOG("[actions] SetActionManifestPath failed: %d (path='%s')",
            (int)err, manifest_path.c_str());
        return false;
    }

    err = input->GetActionSetHandle("/actions/translator", &action_set_);
    if (err != vr::VRInputError_None) {
        TH_LOG("[actions] GetActionSetHandle failed: %d", (int)err);
        return false;
    }

    err = input->GetActionHandle("/actions/translator/in/push_to_talk", &ptt_action_);
    if (err != vr::VRInputError_None) {
        TH_LOG("[actions] GetActionHandle (push_to_talk) failed: %d", (int)err);
        return false;
    }

    registered_ = true;
    TH_LOG("[actions] manifest registered, action set and handle obtained");
    return true;
}

bool ActionBindings::Poll()
{
    if (!registered_) return false;
    auto *input = vr::VRInput();
    if (!input) return false;

    vr::VRActiveActionSet_t active{};
    active.ulActionSet = action_set_;
    input->UpdateActionState(&active, sizeof(active), 1);

    vr::InputDigitalActionData_t data{};
    vr::EVRInputError err = input->GetDigitalActionData(
        ptt_action_, &data, sizeof(data), vr::k_ulInvalidInputValueHandle);

    if (err != vr::VRInputError_None) return false;
    last_state_ = data.bActive && data.bState;
    return last_state_;
}

bool ActionBindings::IsActionActive() const
{
    return last_state_;
}
