// Vendored from VRCFaceTracking (Apache-2.0) -- namespace VRCFaceTracking.Core.Params.Expressions preserved verbatim.
// Updated to match upstream v5.1.1.0 enum order: NoseSneer slots inserted at
// 48-49, MouthLowerDeepen removed (no longer in upstream), MouthLowerDown
// moved to 50-51, MouthUpper/Lower direction at 52-55 in Right-then-Left
// pairing. Driver-side remap table in
// core/src/common/facetracking/UpstreamShapeMap.h is the lockstep
// counterpart and must be re-derived whenever this file changes.
namespace VRCFaceTracking.Core.Params.Expressions
{
    /// <summary>
    /// Represents the type of Shape data being sent by UnifiedExpressionData, in the form of enumerated shapes.
    /// </summary>
    public enum UnifiedExpressions
    {
        #region Eye Expressions
        EyeSquintRight,
        EyeSquintLeft,
        EyeWideRight,
        EyeWideLeft,
        #endregion

        #region Eyebrow Expressions
        BrowPinchRight,
        BrowPinchLeft,
        BrowLowererRight,
        BrowLowererLeft,
        BrowInnerUpRight,
        BrowInnerUpLeft,
        BrowOuterUpRight,
        BrowOuterUpLeft,
        #endregion

        #region Nose Expressions
        NasalDilationRight,
        NasalDilationLeft,
        NasalConstrictRight,
        NasalConstrictLeft,
        #endregion

        #region Cheek Expressions
        CheekSquintRight,
        CheekSquintLeft,
        CheekPuffRight,
        CheekPuffLeft,
        CheekSuckRight,
        CheekSuckLeft,
        #endregion

        #region Jaw Exclusive Expressions
        JawOpen,
        JawRight,
        JawLeft,
        JawForward,
        JawBackward,
        JawClench,
        JawMandibleRaise,

        MouthClosed,
        #endregion

        #region Lip Expressions
        LipSuckUpperRight,
        LipSuckUpperLeft,
        LipSuckLowerRight,
        LipSuckLowerLeft,

        LipSuckCornerRight,
        LipSuckCornerLeft,

        LipFunnelUpperRight,
        LipFunnelUpperLeft,
        LipFunnelLowerRight,
        LipFunnelLowerLeft,

        LipPuckerUpperRight,
        LipPuckerUpperLeft,
        LipPuckerLowerRight,
        LipPuckerLowerLeft,

        MouthUpperUpRight,
        MouthUpperUpLeft,

        MouthUpperDeepenRight,
        MouthUpperDeepenLeft,

        // Nose Sneer is positioned here in upstream v5.1.1.0 (slots 48-49)
        // even though semantically it belongs with the nose group. We follow
        // upstream's ordering exactly so module DLLs compiled against
        // upstream resolve to the same numeric slots.
        NoseSneerRight,
        NoseSneerLeft,

        MouthLowerDownRight,
        MouthLowerDownLeft,

        MouthUpperRight,
        MouthUpperLeft,
        MouthLowerRight,
        MouthLowerLeft,

        MouthCornerPullRight,
        MouthCornerPullLeft,
        MouthCornerSlantRight,
        MouthCornerSlantLeft,
        MouthFrownRight,
        MouthFrownLeft,

        MouthStretchRight,
        MouthStretchLeft,
        MouthDimpleRight,
        MouthDimpleLeft,

        MouthRaiserUpper,
        MouthRaiserLower,
        MouthPressRight,
        MouthPressLeft,
        MouthTightenerRight,
        MouthTightenerLeft,
        #endregion

        #region Tongue Expressions
        TongueOut,
        TongueUp,
        TongueDown,
        TongueRight,
        TongueLeft,

        TongueRoll,
        TongueBendDown,
        TongueCurlUp,
        TongueSquish,
        TongueFlat,

        TongueTwistRight,
        TongueTwistLeft,
        #endregion

        #region Throat/Neck Expressions
        SoftPalateClose,
        ThroatSwallow,

        NeckFlexRight,
        NeckFlexLeft,
        #endregion

        Max
    }
}
