// Implements IExtTrackingModuleLegacy by reflecting into an upstream
// VRCFaceTracking ExtTrackingModule instance and the upstream
// UnifiedTracking.Data static singleton.  Method and property names
// follow the VRCFT v2 SDK convention; modules built against older SDKs
// or with renamed APIs need either a per-module wrapper (the manual
// path) or a config-overridable bridge (follow-up work, not in V1).

using System.Numerics;
using System.Reflection;

namespace OpenVRPair.FaceTracking.VrcftCompat;

internal sealed class ReflectingLegacyBridge : IExtTrackingModuleLegacy
{
    private readonly object _upstream;
    private readonly MethodInfo _initialize;
    private readonly MethodInfo _update;
    private readonly MethodInfo _teardown;
    private readonly PropertyInfo _supportedProp;

    // Upstream static data singleton.  Resolved at construction time; may be
    // null if the upstream SDK type cannot be found (in which case Update
    // calls into the upstream module but skips the readback -- the module
    // will appear inert rather than throw).
    private readonly object? _unifiedDataInstance;
    private readonly PropertyInfo? _dataEyeProp;
    private readonly PropertyInfo? _dataShapesProp;

    public ReflectingLegacyBridge(object upstream)
    {
        _upstream = upstream;
        Type upstreamType = upstream.GetType();

        _initialize = upstreamType.GetMethod(
                          "Initialize",
                          BindingFlags.Public | BindingFlags.Instance,
                          binder: null,
                          types: new[] { typeof(bool), typeof(bool) },
                          modifiers: null)
                      ?? throw new InvalidOperationException(
                          $"{upstreamType.FullName}.Initialize(bool, bool) not found. " +
                          "The upstream module does not match VRCFT SDK v2 conventions.");

        _update = upstreamType.GetMethod(
                      "Update",
                      BindingFlags.Public | BindingFlags.Instance,
                      binder: null,
                      types: Type.EmptyTypes,
                      modifiers: null)
                  ?? throw new InvalidOperationException(
                      $"{upstreamType.FullName}.Update() not found.");

        _teardown = upstreamType.GetMethod(
                        "Teardown",
                        BindingFlags.Public | BindingFlags.Instance,
                        binder: null,
                        types: Type.EmptyTypes,
                        modifiers: null)
                    ?? throw new InvalidOperationException(
                        $"{upstreamType.FullName}.Teardown() not found.");

        _supportedProp = upstreamType.GetProperty(
                             "Supported",
                             BindingFlags.Public | BindingFlags.Instance)
                         ?? throw new InvalidOperationException(
                             $"{upstreamType.FullName}.Supported property not found.");

        // Locate the upstream UnifiedTracking.Data singleton.  Try the upstream
        // module's own assembly first (modules sometimes statically reference
        // the SDK by file copy); fall back to scanning every loaded assembly
        // for the expected type name.
        Type? trackingType = upstreamType.Assembly.GetType(
            "VRCFaceTracking.Core.Params.Data.UnifiedTracking",
            throwOnError: false);
        if (trackingType is null)
        {
            foreach (Assembly asm in AppDomain.CurrentDomain.GetAssemblies())
            {
                trackingType = asm.GetType(
                    "VRCFaceTracking.Core.Params.Data.UnifiedTracking",
                    throwOnError: false);
                if (trackingType is not null) break;
            }
        }

        if (trackingType is not null)
        {
            PropertyInfo? dataProp = trackingType.GetProperty(
                "Data",
                BindingFlags.Public | BindingFlags.Static);
            _unifiedDataInstance = dataProp?.GetValue(null);
            if (_unifiedDataInstance is not null)
            {
                Type dataType = _unifiedDataInstance.GetType();
                _dataEyeProp    = dataType.GetProperty("Eye",    BindingFlags.Public | BindingFlags.Instance);
                _dataShapesProp = dataType.GetProperty("Shapes", BindingFlags.Public | BindingFlags.Instance);
            }
        }
    }

    public (bool SupportsEye, bool SupportsExpression) Supported
        => ExtractBoolPair(_supportedProp.GetValue(_upstream));

    public bool Initialize(bool eye, bool expressions)
    {
        object? result = _initialize.Invoke(_upstream, new object[] { eye, expressions });
        // Upstream returns either bool (older SDKs) or (bool, bool) (v2+).
        if (result is bool b) return b;
        (bool e, bool x) = ExtractBoolPair(result);
        return e || x;
    }

    public void Update(LegacyUnifiedTrackingData data)
    {
        _update.Invoke(_upstream, null);

        if (_unifiedDataInstance is null || _dataEyeProp is null || _dataShapesProp is null)
        {
            return;
        }

        object? upstreamEye = _dataEyeProp.GetValue(_unifiedDataInstance);
        if (upstreamEye is not null)
        {
            CopyEye(upstreamEye, data.Eye);
        }

        if (_dataShapesProp.GetValue(_unifiedDataInstance) is Array shapes && shapes.Length > 0)
        {
            CopyShapes(shapes, data.Shapes);
        }
    }

    public void Teardown() => _teardown.Invoke(_upstream, null);

    private static (bool, bool) ExtractBoolPair(object? value)
    {
        if (value is null) return (false, false);
        Type t = value.GetType();
        object? item1 = t.GetField("Item1")?.GetValue(value)
                        ?? t.GetProperty("Item1")?.GetValue(value);
        object? item2 = t.GetField("Item2")?.GetValue(value)
                        ?? t.GetProperty("Item2")?.GetValue(value);
        bool b1 = item1 is bool ib1 && ib1;
        bool b2 = item2 is bool ib2 && ib2;
        return (b1, b2);
    }

    private static void CopyEye(object upstreamEye, LegacyEyeData dst)
    {
        Type t = upstreamEye.GetType();
        object? left  = t.GetProperty("Left",  BindingFlags.Public | BindingFlags.Instance)?.GetValue(upstreamEye);
        object? right = t.GetProperty("Right", BindingFlags.Public | BindingFlags.Instance)?.GetValue(upstreamEye);
        if (left  is not null) CopyEyeState(left,  dst.Left);
        if (right is not null) CopyEyeState(right, dst.Right);
    }

    private static void CopyEyeState(object upstreamEyeState, LegacyEyeState dst)
    {
        Type t = upstreamEyeState.GetType();

        // Upstream gaze field naming: "Gaze" (Vector2) in v2. Some forks use
        // "GazeDirection" / "GazeNormalized"; try the most common variants.
        if (t.GetProperty("Gaze",            BindingFlags.Public | BindingFlags.Instance)?.GetValue(upstreamEyeState) is Vector2 gaze)
            dst.Gaze = gaze;
        else if (t.GetProperty("GazeNormalized", BindingFlags.Public | BindingFlags.Instance)?.GetValue(upstreamEyeState) is Vector2 gaze2)
            dst.Gaze = gaze2;

        if (t.GetProperty("Openness",      BindingFlags.Public | BindingFlags.Instance)?.GetValue(upstreamEyeState) is float openness)
            dst.Openness = openness;

        // Upstream sometimes calls this "PupilDilation", sometimes "PupilDiameter_MM" -- accept either.
        object? dilation = t.GetProperty("PupilDilation",     BindingFlags.Public | BindingFlags.Instance)?.GetValue(upstreamEyeState)
                           ?? t.GetProperty("PupilDiameter_MM", BindingFlags.Public | BindingFlags.Instance)?.GetValue(upstreamEyeState);
        if (dilation is float d) dst.PupilDilation = d;
    }

    private static void CopyShapes(Array upstreamShapes, float[] dst)
    {
        // Each upstream element is either a struct/class with a float Weight
        // property (v2 SDK) or a bare float (very old forks). Detect once.
        object? first = upstreamShapes.GetValue(0);
        if (first is null) return;

        if (first is float)
        {
            int count = Math.Min(dst.Length, upstreamShapes.Length);
            for (int i = 0; i < count; i++)
            {
                if (upstreamShapes.GetValue(i) is float w) dst[i] = w;
            }
            return;
        }

        PropertyInfo? weightProp = first.GetType().GetProperty(
            "Weight",
            BindingFlags.Public | BindingFlags.Instance);
        if (weightProp is null) return;

        int n = Math.Min(dst.Length, upstreamShapes.Length);
        for (int i = 0; i < n; i++)
        {
            object? shape = upstreamShapes.GetValue(i);
            if (shape is null) continue;
            if (weightProp.GetValue(shape) is float w) dst[i] = w;
        }
    }
}
