#pragma once

#include "Protocol.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace facetracking {

// Number of shape slots in upstream VRCFaceTracking's UnifiedExpressions
// enum, excluding the trailing Max sentinel. The host writes a dense
// array of this many floats into FaceTrackingFrameBody::expressions;
// the driver remaps them into our protocol::FACETRACKING_EXPRESSION_COUNT
// slots via the table below.
inline constexpr int kUpstreamShapeCount = 88;

// Index into protocol::FACETRACKING_EXPRESSION_COUNT slots for each
// upstream shape, or -1 if the upstream shape has no equivalent in our
// enum (silently dropped). The table is the result of a case-insensitive
// name match against:
//   upstream: VRCFaceTracking.Core.Params.Expressions.UnifiedExpressions
//             (modules/facetracking/src/host/OpenVRPair.FaceTracking.UpstreamRuntime/
//              Params/Expressions/UnifiedExpressions.cs)
//   ours:     OpenVRPair.FaceTracking.ModuleSdk.UnifiedExpression
//             (modules/facetracking/src/host/OpenVRPair.FaceTracking.ModuleSdk/
//              UnifiedExpression.cs)
// Hand-computed; verified by a unit test against both enum lists. If
// either enum gains entries, this table needs a matching pass.
inline constexpr int kUpstreamToOurs[kUpstreamShapeCount] = {
    // 0..3 Eye Squint/Wide
    11, // [ 0] EyeSquintRight       -> EyeSquintRight (11)
    10, // [ 1] EyeSquintLeft        -> EyeSquintLeft (10)
     9, // [ 2] EyeWideRight         -> EyeWideRight (9)
     8, // [ 3] EyeWideLeft          -> EyeWideLeft (8)
    // 4..11 Brow
    19, // [ 4] BrowPinchRight       -> BrowPinchRight (19)
    18, // [ 5] BrowPinchLeft        -> BrowPinchLeft (18)
    13, // [ 6] BrowLowererRight     -> BrowLowererRight (13)
    12, // [ 7] BrowLowererLeft      -> BrowLowererLeft (12)
    15, // [ 8] BrowInnerUpRight     -> BrowInnerUpRight (15)
    14, // [ 9] BrowInnerUpLeft      -> BrowInnerUpLeft (14)
    17, // [10] BrowOuterUpRight     -> BrowOuterUpRight (17)
    16, // [11] BrowOuterUpLeft      -> BrowOuterUpLeft (16)
    // 12..15 Nose (we have NoseSneer, not NasalDilation/NasalConstrict)
    -1, // [12] NasalDilationRight
    -1, // [13] NasalDilationLeft
    -1, // [14] NasalConstrictRight
    -1, // [15] NasalConstrictLeft
    // 16..21 Cheek
    -1, // [16] CheekSquintRight     (we don't have a cheek-squint shape)
    -1, // [17] CheekSquintLeft
    21, // [18] CheekPuffRight       -> CheekPuffRight (21)
    20, // [19] CheekPuffLeft        -> CheekPuffLeft (20)
    23, // [20] CheekSuckRight       -> CheekSuckRight (23)
    22, // [21] CheekSuckLeft        -> CheekSuckLeft (22)
    // 22..29 Jaw / Mouth closed
    26, // [22] JawOpen              -> JawOpen (26)
    29, // [23] JawRight             -> JawRight (29)
    28, // [24] JawLeft              -> JawLeft (28)
    27, // [25] JawForward           -> JawForward (27)
    -1, // [26] JawBackward
    -1, // [27] JawClench
    -1, // [28] JawMandibleRaise
    -1, // [29] MouthClosed          (we have MouthClose with no trailing 'd';
                                  //  the rename loses the case-insensitive match)
    // 30..35 Lip Suck Upper/Lower
    31, // [30] LipSuckUpperRight    -> LipSuckUpperRight (31)
    30, // [31] LipSuckUpperLeft     -> LipSuckUpperLeft (30)
    33, // [32] LipSuckLowerRight    -> LipSuckLowerRight (33)
    32, // [33] LipSuckLowerLeft     -> LipSuckLowerLeft (32)
    -1, // [34] LipSuckCornerRight
    -1, // [35] LipSuckCornerLeft
    // 36..43 Lip Funnel/Pucker
    35, // [36] LipFunnelUpperRight  -> LipFunnelUpperRight (35)
    34, // [37] LipFunnelUpperLeft   -> LipFunnelUpperLeft (34)
    37, // [38] LipFunnelLowerRight  -> LipFunnelLowerRight (37)
    36, // [39] LipFunnelLowerLeft   -> LipFunnelLowerLeft (36)
    39, // [40] LipPuckerUpperRight  -> LipPuckerUpperRight (39)
    38, // [41] LipPuckerUpperLeft   -> LipPuckerUpperLeft (38)
    -1, // [42] LipPuckerLowerRight  (our enum has Upper only)
    -1, // [43] LipPuckerLowerLeft
    // 44..51 Mouth Upper/Lower Up/Down/Deepen (no equivalents)
    -1, // [44] MouthUpperUpRight
    -1, // [45] MouthUpperUpLeft
    -1, // [46] MouthLowerDownRight
    -1, // [47] MouthLowerDownLeft
    -1, // [48] MouthUpperDeepenRight
    -1, // [49] MouthUpperDeepenLeft
    -1, // [50] MouthLowerDeepenRight
    -1, // [51] MouthLowerDeepenLeft
    // 52..55 Mouth Upper/Lower Direction
    41, // [52] MouthUpperLeft       -> MouthUpperLeft (41)
    42, // [53] MouthUpperRight      -> MouthUpperRight (42)
    43, // [54] MouthLowerLeft       -> MouthLowerLeft (43)
    44, // [55] MouthLowerRight      -> MouthLowerRight (44)
    // 56..61 Corner / Slant / Frown (we have MouthSmile/MouthSad in those roles)
    -1, // [56] MouthCornerPullRight
    -1, // [57] MouthCornerPullLeft
    -1, // [58] MouthCornerSlantRight
    -1, // [59] MouthCornerSlantLeft
    -1, // [60] MouthFrownRight
    -1, // [61] MouthFrownLeft
    // 62..71 Stretch / Dimple / Raiser / Press / Tightener
    50, // [62] MouthStretchRight    -> MouthStretchRight (50)
    49, // [63] MouthStretchLeft     -> MouthStretchLeft (49)
    52, // [64] MouthDimpleRight     -> MouthDimpleRight (52)
    51, // [65] MouthDimpleLeft      -> MouthDimpleLeft (51)
    53, // [66] MouthRaiserUpper     -> MouthRaiserUpper (53)
    54, // [67] MouthRaiserLower     -> MouthRaiserLower (54)
    56, // [68] MouthPressRight      -> MouthPressRight (56)
    55, // [69] MouthPressLeft       -> MouthPressLeft (55)
    58, // [70] MouthTightenerRight  -> MouthTightenerRight (58)
    57, // [71] MouthTightenerLeft   -> MouthTightenerLeft (57)
    // 72..83 Tongue
    59, // [72] TongueOut            -> TongueOut (59)
    60, // [73] TongueUp             -> TongueUp (60)
    61, // [74] TongueDown           -> TongueDown (61)
    -1, // [75] TongueRight          (our enum has TongueLeft only)
    62, // [76] TongueLeft           -> TongueLeft (62)
    -1, // [77] TongueRoll
    -1, // [78] TongueBendDown
    -1, // [79] TongueCurlUp
    -1, // [80] TongueSquish
    -1, // [81] TongueFlat
    -1, // [82] TongueTwistRight
    -1, // [83] TongueTwistLeft
    // 84..87 Soft Palate / Throat / Neck (no equivalents)
    -1, // [84] SoftPalateClose
    -1, // [85] ThroatSwallow
    -1, // [86] NeckFlexRight
    -1, // [87] NeckFlexLeft
};

// Remap an upstream dense expression array into our protocol-ordered
// dense array. Indices unmapped on the upstream side are silently
// dropped (the corresponding slot in `dst` stays at whatever the
// caller initialised it to, typically zero). NaN / Inf in src are
// also dropped. Each output is clamped to [0, 1].
//
// `src` must have at least kUpstreamShapeCount entries; `dst` must
// have at least protocol::FACETRACKING_EXPRESSION_COUNT entries.
inline void RemapUpstreamShapes(const float* src, float* dst)
{
    constexpr int kOurCount = static_cast<int>(protocol::FACETRACKING_EXPRESSION_COUNT);
    for (int u = 0; u < kUpstreamShapeCount; ++u) {
        const int o = kUpstreamToOurs[u];
        if (o < 0 || o >= kOurCount) continue;
        const float v = src[u];
        if (std::isnan(v) || std::isinf(v)) continue;
        dst[o] = std::clamp(v, 0.0f, 1.0f);
    }
}

} // namespace facetracking
