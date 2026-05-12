using System.Numerics;

namespace OpenVRPair.FaceTracking.ModuleSdk;

/// <summary>
/// Gaze ray for one eye, in HMD space. +X right, +Y up, -Z forward (OpenVR convention).
/// </summary>
public sealed class EyeGaze
{
    /// <summary>Eye origin in HMD space, metres. Falls back to half-IPD if hardware does not report it.</summary>
    public Vector3 OriginHmd { get; set; }

    /// <summary>Unit gaze direction in HMD space. Straight ahead is (0, 0, -1).</summary>
    public Vector3 DirHmd { get; set; }

    /// <summary>Hardware or synthesised confidence 0..1.</summary>
    public float Confidence { get; set; }
}

/// <summary>
/// HMD world pose at the time of a frame, as reported by the driver via the control pipe.
/// </summary>
public readonly record struct HmdPose(Vector3 Position, System.Numerics.Quaternion Rotation);

/// <summary>
/// Per-frame context passed to <see cref="FaceTrackingModule.Update"/>.
/// </summary>
public readonly record struct FrameContext(HmdPose Hmd, long TimestampHns, float DeltaSeconds);

/// <summary>
/// Hardware plug/unplug and reset events forwarded from the driver.
/// </summary>
public enum DeviceEvent
{
    Connected,
    Disconnected,
    Reset,
}
