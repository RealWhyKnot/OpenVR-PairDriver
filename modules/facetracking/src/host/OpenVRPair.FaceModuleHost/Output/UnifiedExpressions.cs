// V1: 1:1 mapping of UnifiedExpression enum name to VRChat avatar parameter name.
// TODO: canonical parameter mapping needs review against published Unified Expressions v2 spec
// before any cross-avatar compatibility claim. Some modules use prefixed
// param names (e.g. "FT/v2/JawOpen") -- confirm the target avatar standard.

using OpenVRPair.FaceTracking.ModuleSdk;

namespace OpenVRPair.FaceModuleHost.Output;

public static class UnifiedExpressionParams
{
    private static readonly string[] ParamNames;

    static UnifiedExpressionParams()
    {
        int count  = (int)UnifiedExpression.Count;
        ParamNames = new string[count];
        for (int i = 0; i < count; i++)
            ParamNames[i] = ((UnifiedExpression)i).ToString();
    }

    /// <summary>Returns the OSC avatar parameter name for a given expression index.</summary>
    public static string GetParamName(int index)
        => (uint)index < (uint)ParamNames.Length ? ParamNames[index] : $"Expr{index}";

    /// <summary>Returns the OSC avatar parameter name for a given expression enum value.</summary>
    public static string GetParamName(UnifiedExpression expr)
        => GetParamName((int)expr);
}
