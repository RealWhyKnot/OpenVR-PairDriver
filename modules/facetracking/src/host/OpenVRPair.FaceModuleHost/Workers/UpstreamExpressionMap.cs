using OpenVRPair.FaceTracking.ModuleSdk;
using VRCFaceTracking.Core.Params.Expressions;

namespace OpenVRPair.FaceModuleHost.Workers;

/// <summary>
/// Name-based mapping table from upstream VRCFaceTracking's UnifiedExpressions enum
/// to our 63-slot UnifiedExpression enum. Built once at static-class init.
///
/// Upstream has more shapes than we have (~74 vs 63 in v2). Shapes with no
/// name-match in our enum are dropped (stored as -1 in the table); behaviour is
/// identical to what the in-process ReflectingLegacyBridge already produced.
/// </summary>
public static class UpstreamExpressionMap
{
    private static readonly int[] _upstreamToOurs;
    private static readonly int   _mapped;
    private static readonly int   _dropped;
    private static readonly string[] _droppedNames;

    static UpstreamExpressionMap()
    {
        string[] upstreamNames = Enum.GetNames(typeof(UnifiedExpressions));
        string[] ourNames      = Enum.GetNames(typeof(UnifiedExpression));
        Array    ourValues     = Enum.GetValues(typeof(UnifiedExpression));

        // UnifiedExpressions includes "Max" sentinel at the end; strip it.
        int upstreamCount = upstreamNames.Length;
        if (upstreamCount > 0 && upstreamNames[upstreamCount - 1] == "Max")
            upstreamCount--;

        var map      = new int[upstreamCount];
        int mapped   = 0;
        int dropped  = 0;

        for (int u = 0; u < upstreamCount; u++)
        {
            int found = -1;
            for (int o = 0; o < ourNames.Length; o++)
            {
                if (string.Equals(upstreamNames[u], ourNames[o], StringComparison.OrdinalIgnoreCase))
                {
                    found = (int)(UnifiedExpression)ourValues.GetValue(o)!;
                    break;
                }
            }
            map[u] = found;
            if (found >= 0) mapped++; else dropped++;
        }

        _upstreamToOurs = map;
        _mapped         = mapped;
        _dropped        = dropped;
        _droppedNames   = Enumerable.Range(0, upstreamCount)
            .Where(u => map[u] < 0)
            .Select(u => upstreamNames[u])
            .ToArray();
    }

    /// <summary>Maps upstream index to our UnifiedExpression index, or -1 if no match.</summary>
    public static int[] UpstreamToOurs => _upstreamToOurs;

    /// <summary>Build stats for logging at startup.</summary>
    public static (int mapped, int dropped, string[] droppedNames) Stats
        => (_mapped, _dropped, _droppedNames);

    /// <summary>
    /// Copies the upstream expression shapes array into <paramref name="dst"/>,
    /// translating upstream indices to our indices via the precomputed table.
    /// </summary>
    public static void CopyShapes(ReadOnlySpan<float> upstreamShapes, ExpressionFrameSink dst)
    {
        int n = Math.Min(_upstreamToOurs.Length, upstreamShapes.Length);
        for (int u = 0; u < n; u++)
        {
            int o = _upstreamToOurs[u];
            if (o < 0) continue;
            if (o >= (int)UnifiedExpression.Count) continue;
            dst[(UnifiedExpression)o] = upstreamShapes[u];
        }
    }

    /// <summary>Emits the "built map" log line to the provided sink.</summary>
    public static void LogTo(Action<string> sink)
    {
        sink($"[map] built upstream->ours: {_mapped} mapped, {_dropped} dropped" +
             (_dropped > 0 ? $": {string.Join(", ", _droppedNames)}" : ""));
    }
}
