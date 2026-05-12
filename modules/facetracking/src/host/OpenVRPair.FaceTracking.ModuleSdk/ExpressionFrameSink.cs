namespace OpenVRPair.FaceTracking.ModuleSdk;

/// <summary>
/// Mutable sink for the 63 Unified Expressions v2 shape values.
/// Indices match the <see cref="UnifiedExpression"/> enum; the host copies this buffer
/// verbatim into the shmem ring's expressions[63] field.
/// </summary>
public sealed class ExpressionFrameSink
{
    private readonly float[] _values = new float[(int)UnifiedExpression.Count];

    /// <summary>Gets or sets a shape value by enum index. Values should be normalised to [0, 1].</summary>
    public float this[UnifiedExpression shape]
    {
        get => _values[(int)shape];
        set => _values[(int)shape] = value;
    }

    /// <summary>Direct read-only span over the backing array for zero-copy publish.</summary>
    public ReadOnlySpan<float> Values => _values;

    /// <summary>Hardware sample timestamp; semantics match <see cref="EyeFrameSink.TimestampHns"/>.</summary>
    public long TimestampHns { get; set; }

    /// <summary>Zeroes all 63 shape values and resets the timestamp.</summary>
    public void Reset()
    {
        Array.Clear(_values);
        TimestampHns = 0;
    }
}
