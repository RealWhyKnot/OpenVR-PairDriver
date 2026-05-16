#define _CRT_SECURE_NO_DEPRECATE
#include "FaceOscPublisher.h"

#include "RouterPublishApi.h"

#include <cstdint>
#include <cstring>

namespace facetracking {
namespace {

struct OscCounts
{
    uint32_t sent = 0;
    uint32_t dropped = 0;

    void Add(bool ok)
    {
        if (ok) ++sent;
        else ++dropped;
    }

    FaceOscPublishCounts Public() const
    {
        return FaceOscPublishCounts{ sent, dropped };
    }
};

static inline bool OscPublishFloat(const char *address, float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    uint8_t arg_bytes[4] = {
        static_cast<uint8_t>(bits >> 24),
        static_cast<uint8_t>(bits >> 16),
        static_cast<uint8_t>(bits >>  8),
        static_cast<uint8_t>(bits        ),
    };
    return pairdriver::oscrouter::PublishOsc(
        "facetracking", address, ",f", arg_bytes, 4);
}

static const char *const kExprParamNames[protocol::FACETRACKING_EXPRESSION_COUNT] = {
    "EyeLookOutLeft",
    "EyeLookInLeft",
    "EyeLookUpLeft",
    "EyeLookDownLeft",
    "EyeLookOutRight",
    "EyeLookInRight",
    "EyeLookUpRight",
    "EyeLookDownRight",
    "EyeWideLeft",
    "EyeWideRight",
    "EyeSquintLeft",
    "EyeSquintRight",
    "BrowLowererLeft",
    "BrowLowererRight",
    "BrowInnerUpLeft",
    "BrowInnerUpRight",
    "BrowOuterUpLeft",
    "BrowOuterUpRight",
    "BrowPinchLeft",
    "BrowPinchRight",
    "CheekPuffLeft",
    "CheekPuffRight",
    "CheekSuckLeft",
    "CheekSuckRight",
    "NoseSneerLeft",
    "NoseSneerRight",
    "JawOpen",
    "JawForward",
    "JawLeft",
    "JawRight",
    "LipSuckUpperLeft",
    "LipSuckUpperRight",
    "LipSuckLowerLeft",
    "LipSuckLowerRight",
    "LipFunnelUpperLeft",
    "LipFunnelUpperRight",
    "LipFunnelLowerLeft",
    "LipFunnelLowerRight",
    "LipPuckerUpperLeft",
    "LipPuckerUpperRight",
    "MouthClose",
    "MouthUpperLeft",
    "MouthUpperRight",
    "MouthLowerLeft",
    "MouthLowerRight",
    "MouthSmileLeft",
    "MouthSmileRight",
    "MouthSadLeft",
    "MouthSadRight",
    "MouthStretchLeft",
    "MouthStretchRight",
    "MouthDimpleLeft",
    "MouthDimpleRight",
    "MouthRaiserUpper",
    "MouthRaiserLower",
    "MouthPressLeft",
    "MouthPressRight",
    "MouthTightenerLeft",
    "MouthTightenerRight",
    "TongueOut",
    "TongueUp",
    "TongueDown",
    "TongueLeft",
};

static_assert(
    sizeof(kExprParamNames) / sizeof(kExprParamNames[0]) ==
        protocol::FACETRACKING_EXPRESSION_COUNT,
    "kExprParamNames length must match FACETRACKING_EXPRESSION_COUNT");

static OscCounts PublishEye(const protocol::FaceTrackingFrameBody &frame)
{
    OscCounts counts;
    const float pupil = (frame.pupil_dilation_l + frame.pupil_dilation_r) * 0.5f;

    counts.Add(OscPublishFloat("/avatar/parameters/LeftEyeX",     frame.eye_gaze_l[0]));
    counts.Add(OscPublishFloat("/avatar/parameters/LeftEyeY",     frame.eye_gaze_l[1]));
    counts.Add(OscPublishFloat("/avatar/parameters/RightEyeX",    frame.eye_gaze_r[0]));
    counts.Add(OscPublishFloat("/avatar/parameters/RightEyeY",    frame.eye_gaze_r[1]));
    counts.Add(OscPublishFloat("/avatar/parameters/LeftEyeLid",   frame.eye_openness_l));
    counts.Add(OscPublishFloat("/avatar/parameters/RightEyeLid",  frame.eye_openness_r));
    counts.Add(OscPublishFloat("/avatar/parameters/EyesDilation", pupil));

    counts.Add(OscPublishFloat("/avatar/parameters/v2/EyeLeftX",      frame.eye_gaze_l[0]));
    counts.Add(OscPublishFloat("/avatar/parameters/v2/EyeLeftY",      frame.eye_gaze_l[1]));
    counts.Add(OscPublishFloat("/avatar/parameters/v2/EyeRightX",     frame.eye_gaze_r[0]));
    counts.Add(OscPublishFloat("/avatar/parameters/v2/EyeRightY",     frame.eye_gaze_r[1]));
    counts.Add(OscPublishFloat("/avatar/parameters/v2/EyeOpenLeft",   frame.eye_openness_l));
    counts.Add(OscPublishFloat("/avatar/parameters/v2/EyeOpenRight",  frame.eye_openness_r));
    counts.Add(OscPublishFloat("/avatar/parameters/v2/PupilDilation", pupil));
    return counts;
}

static OscCounts PublishExpressions(const protocol::FaceTrackingFrameBody &frame)
{
    static const char kLegacyPrefix[] = "/avatar/parameters/";
    static const char kV2Prefix[]     = "/avatar/parameters/v2/";
    static const size_t kLegacyPrefixLen = sizeof(kLegacyPrefix) - 1;
    static const size_t kV2PrefixLen     = sizeof(kV2Prefix) - 1;

    OscCounts counts;
    char legacy[64];
    char v2addr[64];

    for (uint32_t i = 0; i < protocol::FACETRACKING_EXPRESSION_COUNT; ++i) {
        const char *name = kExprParamNames[i];
        const size_t nameLen = std::strlen(name);
        if (kLegacyPrefixLen + nameLen + 1 > sizeof(legacy)) continue;
        if (kV2PrefixLen     + nameLen + 1 > sizeof(v2addr)) continue;

        std::memcpy(legacy, kLegacyPrefix, kLegacyPrefixLen);
        std::memcpy(legacy + kLegacyPrefixLen, name, nameLen + 1);
        std::memcpy(v2addr, kV2Prefix, kV2PrefixLen);
        std::memcpy(v2addr + kV2PrefixLen, name, nameLen + 1);

        const float value = frame.expressions[i];
        counts.Add(OscPublishFloat(legacy, value));
        counts.Add(OscPublishFloat(v2addr, value));
    }
    return counts;
}

} // namespace

FaceOscPublishCounts PublishFaceFrameOsc(
    const protocol::FaceTrackingFrameBody &frame)
{
    FaceOscPublishCounts counts;
    if ((frame.flags & 0x1u) != 0) counts.Add(PublishEye(frame).Public());
    if ((frame.flags & 0x2u) != 0) counts.Add(PublishExpressions(frame).Public());
    return counts;
}

const char *FaceExpressionOscName(uint32_t index)
{
    if (index >= protocol::FACETRACKING_EXPRESSION_COUNT) return "";
    return kExprParamNames[index];
}

} // namespace facetracking
