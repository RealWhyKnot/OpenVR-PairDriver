// Unified Expressions v2 ordered enum.
// The wire index (the integer value of each member) is the ABI -- do NOT reorder
// or insert entries without bumping FaceTrackingFrameBody in Protocol.h and
// issuing a host/driver paired release. The canonical ordering must be confirmed
// against upstream VRCFaceTracking UnifiedExpressions.cs
// (https://github.com/benaclejames/VRCFaceTracking) before any production release
// that expects cross-module compatibility.

namespace OpenVRPair.FaceTracking.ModuleSdk;

public enum UnifiedExpression
{
    // Eye -- outward/inward gaze (indices 0-7)
    EyeLookOutLeft     = 0,
    EyeLookInLeft      = 1,
    EyeLookUpLeft      = 2,
    EyeLookDownLeft    = 3,
    EyeLookOutRight    = 4,
    EyeLookInRight     = 5,
    EyeLookUpRight     = 6,
    EyeLookDownRight   = 7,

    // Eyelid wide / squeeze (8-11)
    EyeWideLeft        = 8,
    EyeWideRight       = 9,
    EyeSquintLeft      = 10,
    EyeSquintRight     = 11,

    // Brow (12-19)
    BrowLowererLeft    = 12,
    BrowLowererRight   = 13,
    BrowInnerUpLeft    = 14,
    BrowInnerUpRight   = 15,
    BrowOuterUpLeft    = 16,
    BrowOuterUpRight   = 17,
    BrowPinchLeft      = 18,
    BrowPinchRight     = 19,

    // Cheek / nose (20-25)
    CheekPuffLeft      = 20,
    CheekPuffRight     = 21,
    CheekSuckLeft      = 22,
    CheekSuckRight     = 23,
    NoseSneerLeft      = 24,
    NoseSneerRight     = 25,

    // Jaw / mouth open (26-29)
    JawOpen            = 26,
    JawForward         = 27,
    JawLeft            = 28,
    JawRight           = 29,

    // Lip upper (30-35)
    LipSuckUpperLeft   = 30,
    LipSuckUpperRight  = 31,
    LipSuckLowerLeft   = 32,
    LipSuckLowerRight  = 33,
    LipFunnelUpperLeft = 34,
    LipFunnelUpperRight= 35,

    // Lip lower (36-39)
    LipFunnelLowerLeft = 36,
    LipFunnelLowerRight= 37,
    LipPuckerUpperLeft = 38,
    LipPuckerUpperRight= 39,

    // Mouth close / wide / stretch (40-49)
    MouthClose         = 40,
    MouthUpperLeft     = 41,
    MouthUpperRight    = 42,
    MouthLowerLeft     = 43,
    MouthLowerRight    = 44,
    MouthSmileLeft     = 45,
    MouthSmileRight    = 46,
    MouthSadLeft       = 47,
    MouthSadRight      = 48,
    MouthStretchLeft   = 49,

    // Mouth continued (50-57)
    MouthStretchRight  = 50,
    MouthDimpleLeft    = 51,
    MouthDimpleRight   = 52,
    MouthRaiserUpper   = 53,
    MouthRaiserLower   = 54,
    MouthPressLeft     = 55,
    MouthPressRight    = 56,
    MouthTightenerLeft = 57,

    // Tongue (58-62)
    MouthTightenerRight= 58,
    TongueOut          = 59,
    TongueUp           = 60,
    TongueDown         = 61,
    TongueLeft         = 62,

    Count              = 63,
}
