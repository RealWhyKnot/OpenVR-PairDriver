namespace OpenVRPair.FaceModuleHost.Workers;

// V1 trade-off: all normalization lives on the driver side (C++) where the
// continuous calibration state machine runs. The host ships raw hardware values
// and the driver normalizes before publishing to SteamVR inputs and the OSC
// /scaled namespace. CalibrationCache is a passthrough stub for V1; it exists
// so future work can mirror the driver's learned bounds here for host-side
// consumers (e.g. a debug UI or a secondary OSC sender that wants normalized
// values without round-tripping through SteamVR).

/// <summary>
/// Mirrors the driver's normalization state for use by host-side consumers.
/// V1: passthrough -- the host writes raw values; the driver normalizes.
/// </summary>
public sealed class CalibrationCache
{
    /// <summary>
    /// Returns the calibrated value for a given shape.
    /// V1 implementation returns the raw value unchanged.
    /// </summary>
    public float Normalize(int shapeIndex, float rawValue) => rawValue;

    /// <summary>
    /// Called when the driver notifies the host of updated calibration bounds.
    /// V1: no-op.
    /// </summary>
    public void UpdateBounds(int shapeIndex, float p02, float p98) { }
}
