namespace OpenVRPair.FaceTracking.ModuleSdk;

/// <summary>
/// Mutable sink that a <see cref="FaceTrackingModule"/> writes per-eye data into each hardware tick.
/// The host's <c>FrameWriter</c> reads this object under a lock-free snapshot after Update() returns.
/// </summary>
public sealed class EyeFrameSink
{
    public EyeGaze Left { get; } = new();
    public EyeGaze Right { get; } = new();

    public float LeftOpenness  { get; set; }
    public float RightOpenness { get; set; }

    public float PupilDilationLeft  { get; set; }
    public float PupilDilationRight { get; set; }

    /// <summary>
    /// Hardware-reported sample time in 100-nanosecond intervals (FILETIME epoch or QPC-derived).
    /// Modules that cannot supply a hardware timestamp should write 0; the host will substitute
    /// the frame-writer's QPC snapshot.
    /// </summary>
    public long TimestampHns { get; set; }
}
