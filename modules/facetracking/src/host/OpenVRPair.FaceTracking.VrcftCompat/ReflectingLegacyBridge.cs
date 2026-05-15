// Implements IExtTrackingModuleLegacy by reflecting into an upstream
// VRCFaceTracking ExtTrackingModule instance and the upstream
// UnifiedTracking.Data static singleton.  Method and property names
// follow the VRCFT v2 SDK convention; modules built against older SDKs
// or with renamed APIs need either a per-module wrapper (the manual
// path) or a config-overridable bridge (follow-up work, not in V1).

using System.Numerics;
using System.Reflection;
using OpenVRPair.FaceTracking.ModuleSdk;

namespace OpenVRPair.FaceTracking.VrcftCompat;

public sealed class ReflectingLegacyBridge : IExtTrackingModuleLegacy
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
    private readonly FieldInfo?    _dataShapesField;  // UnifiedTrackingData.Shapes is a field, not a property

    // Name-based mapping: upstream expression index -> our UnifiedExpression int value.
    // -1 means no matching entry in our enum; those upstream shapes are dropped.
    private readonly int[] _upstreamToOursMap;

    // One-shot first-nonzero-shape log guard.
    private bool _firstNonZeroLogged = false;

    public static Action<string>? SinkLine { get; set; }

    private static void Log(string msg)
    {
        var sink = SinkLine;
        if (sink is not null) { sink($"[bridge] {msg}"); return; }
        Console.Error.WriteLine($"[bridge] {msg}");
    }

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

        // Inject a NullLogger into the module instance so modules that call
        // this.Logger.X() do not NRE on a null logger field.
        InjectNullLogger(upstream);

        // Locate the upstream UnifiedTracking singleton.
        // The type lives in namespace VRCFaceTracking, class UnifiedTracking,
        // inside VRCFaceTracking.Core.dll.  An earlier version of this bridge
        // searched for "VRCFaceTracking.Core.Params.Data.UnifiedTracking"
        // (which does not exist as a top-level type); the correct fully-qualified
        // name is "VRCFaceTracking.UnifiedTracking".
        Type? trackingType = FindTypeAcrossAssemblies("VRCFaceTracking.UnifiedTracking");

        if (trackingType is not null)
        {
            // Data is a static field (not a property) on UnifiedTracking.
            FieldInfo? dataField = trackingType.GetField(
                "Data",
                BindingFlags.Public | BindingFlags.Static);
            PropertyInfo? dataProp = dataField is null
                ? trackingType.GetProperty("Data", BindingFlags.Public | BindingFlags.Static)
                : null;

            _unifiedDataInstance = dataField?.GetValue(null) ?? dataProp?.GetValue(null);

            if (_unifiedDataInstance is not null)
            {
                Type dataType = _unifiedDataInstance.GetType();
                _dataEyeProp    = dataType.GetProperty("Eye",    BindingFlags.Public | BindingFlags.Instance);
                // Shapes is declared as a field (UnifiedExpressionShape[] Shapes) in upstream.
                _dataShapesField = dataType.GetField("Shapes", BindingFlags.Public | BindingFlags.Instance);

                Log($"resolved UnifiedTracking type at {trackingType.Assembly.GetName().Name}, " +
                    $"Data field type={dataType.Name}; eye={_dataEyeProp != null} shapes={_dataShapesField != null}");

                // Build upstream-to-ours expression index map.
                Type? upstreamExprEnum = FindTypeAcrossAssemblies(
                    "VRCFaceTracking.Core.Params.Expressions.UnifiedExpressions");
                _upstreamToOursMap = upstreamExprEnum is not null
                    ? BuildUpstreamToOursMap(upstreamExprEnum)
                    : Array.Empty<int>();
            }
            else
            {
                Log("WARNING: UnifiedTracking.Data returned null. " +
                    "Module Update calls will succeed but readback will be skipped.");
                _upstreamToOursMap = Array.Empty<int>();
            }
        }
        else
        {
            var searchedNames = string.Join(", ",
                AppDomain.CurrentDomain.GetAssemblies()
                    .Select(a => a.GetName().Name));
            Log($"FATAL: could not resolve VRCFaceTracking.UnifiedTracking type.\n" +
                $"Searched assemblies:\n  - {string.Join("\n  - ", AppDomain.CurrentDomain.GetAssemblies().Select(a => a.FullName))}\n" +
                $"Module load will continue but no data will flow.");
            _upstreamToOursMap = Array.Empty<int>();
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

        if (_unifiedDataInstance is null || _dataEyeProp is null || _dataShapesField is null)
        {
            return;
        }

        object? upstreamEye = _dataEyeProp.GetValue(_unifiedDataInstance);
        if (upstreamEye is not null)
        {
            CopyEye(upstreamEye, data.Eye);
        }

        Array? shapes = _dataShapesField.GetValue(_unifiedDataInstance) as Array;
        if (shapes is not null && shapes.Length > 0 && _upstreamToOursMap.Length > 0)
        {
            CopyShapesMapped(shapes, data.Shapes);
        }
    }

    public void Teardown() => _teardown.Invoke(_upstream, null);

    // -----------------------------------------------------------------------

    private static Type? FindTypeAcrossAssemblies(string fullName)
    {
        foreach (Assembly asm in AppDomain.CurrentDomain.GetAssemblies())
        {
            Type? t = asm.GetType(fullName, throwOnError: false);
            if (t is not null) return t;
        }
        return null;
    }

    private static int[] BuildUpstreamToOursMap(Type upstreamUnifiedExpressionsType)
    {
        string[] upstreamNames  = Enum.GetNames(upstreamUnifiedExpressionsType);
        int      upstreamCount  = upstreamNames.Length;
        string[] ourNames       = Enum.GetNames(typeof(UnifiedExpression));
        Array    ourValues      = Enum.GetValues(typeof(UnifiedExpression));

        var map    = new int[upstreamCount];
        int mapped = 0;
        int dropped = 0;
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
        Log($"upstream-to-ours expression map: {mapped} mapped, {dropped} dropped " +
            $"(no equivalent in our {ourNames.Length}-entry enum)");
        if (dropped > 0)
        {
            var droppedNames = Enumerable.Range(0, upstreamCount)
                .Where(u => map[u] < 0)
                .Select(u => $"{upstreamNames[u]}(u{u})");
            Log($"dropped upstream shapes: {string.Join(", ", droppedNames)}");
        }
        return map;
    }

    private void CopyShapesMapped(Array upstreamShapes, float[] dst)
    {
        // Upstream Shapes array contains UnifiedExpressionShape structs with a Weight float.
        // Detect whether elements are structs-with-Weight or bare floats on first element.
        object? first = upstreamShapes.GetValue(0);
        if (first is null) return;

        PropertyInfo? weightProp = first is float
            ? null
            : first.GetType().GetProperty("Weight", BindingFlags.Public | BindingFlags.Instance);

        int n = Math.Min(_upstreamToOursMap.Length, upstreamShapes.Length);
        int firstNonZeroUpIdx = -1;
        float firstNonZeroVal = 0f;

        for (int u = 0; u < n; u++)
        {
            int o = _upstreamToOursMap[u];
            if (o < 0 || o >= dst.Length) continue;

            float w = 0f;
            object? shape = upstreamShapes.GetValue(u);
            if (shape is null) continue;
            if (shape is float f) {
                w = f;
            } else if (weightProp is not null) {
                object? wobj = weightProp.GetValue(shape);
                if (wobj is float fw) w = fw;
            }

            dst[o] = w;

            if (!_firstNonZeroLogged && w != 0f && firstNonZeroUpIdx < 0)
            {
                firstNonZeroUpIdx  = u;
                firstNonZeroVal    = w;
            }
        }

        if (!_firstNonZeroLogged && firstNonZeroUpIdx >= 0)
        {
            int ourIdx = _upstreamToOursMap[firstNonZeroUpIdx];
            string ourName = ourIdx >= 0
                ? ((UnifiedExpression)ourIdx).ToString()
                : "?";
            Log($"first non-zero shape: upstream[{firstNonZeroUpIdx}] value={firstNonZeroVal:F3} mapped to ours[{ourIdx}]={ourName}");
            _firstNonZeroLogged = true;
        }
    }

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
        object? gazeObj =
            t.GetProperty("Gaze",            BindingFlags.Public | BindingFlags.Instance)?.GetValue(upstreamEyeState) ??
            t.GetProperty("GazeNormalized",  BindingFlags.Public | BindingFlags.Instance)?.GetValue(upstreamEyeState) ??
            t.GetField(   "Gaze",            BindingFlags.Public | BindingFlags.Instance)?.GetValue(upstreamEyeState);
        if (gazeObj is not null)
        {
            // Upstream uses VRCFaceTracking.Core.Types.Vector2, not System.Numerics.Vector2.
            // Extract X and Y by name to bridge the type gap.
            Type gt = gazeObj.GetType();
            object? xObj = gt.GetField("x", BindingFlags.Public | BindingFlags.Instance)?.GetValue(gazeObj)
                        ?? gt.GetField("X", BindingFlags.Public | BindingFlags.Instance)?.GetValue(gazeObj)
                        ?? gt.GetProperty("X", BindingFlags.Public | BindingFlags.Instance)?.GetValue(gazeObj);
            object? yObj = gt.GetField("y", BindingFlags.Public | BindingFlags.Instance)?.GetValue(gazeObj)
                        ?? gt.GetField("Y", BindingFlags.Public | BindingFlags.Instance)?.GetValue(gazeObj)
                        ?? gt.GetProperty("Y", BindingFlags.Public | BindingFlags.Instance)?.GetValue(gazeObj);
            float gx = xObj is float fx ? fx : 0f;
            float gy = yObj is float fy ? fy : 0f;
            dst.Gaze = new Vector2(gx, gy);
        }

        object? opennessObj =
            t.GetProperty("Openness",  BindingFlags.Public | BindingFlags.Instance)?.GetValue(upstreamEyeState) ??
            t.GetField(   "Openness",  BindingFlags.Public | BindingFlags.Instance)?.GetValue(upstreamEyeState);
        if (opennessObj is float openness)
            dst.Openness = openness;

        // Upstream sometimes calls this "PupilDilation", sometimes "PupilDiameter_MM" -- accept either.
        object? dilation =
            t.GetProperty("PupilDilation",     BindingFlags.Public | BindingFlags.Instance)?.GetValue(upstreamEyeState) ??
            t.GetProperty("PupilDiameter_MM",  BindingFlags.Public | BindingFlags.Instance)?.GetValue(upstreamEyeState) ??
            t.GetField(   "PupilDiameter_MM",  BindingFlags.Public | BindingFlags.Instance)?.GetValue(upstreamEyeState);
        if (dilation is float d) dst.PupilDilation = d;
    }

    // Inject a NullLogger.Instance into the module's Logger field/property so
    // modules calling this.Logger.LogXxx() do not throw NullReferenceException.
    private static void InjectNullLogger(object moduleInstance)
    {
        const string abstractionsName = "Microsoft.Extensions.Logging.Abstractions";
        const string nullLoggerName   = "Microsoft.Extensions.Logging.Abstractions.NullLogger";

        Assembly? abstractionsAsm = AppDomain.CurrentDomain.GetAssemblies()
            .FirstOrDefault(a => string.Equals(
                a.GetName().Name, abstractionsName, StringComparison.Ordinal));

        if (abstractionsAsm is null) return;

        Type? nullLoggerType = abstractionsAsm.GetType(nullLoggerName, throwOnError: false);
        if (nullLoggerType is null) return;

        object? nullLoggerInstance =
            nullLoggerType.GetProperty("Instance", BindingFlags.Public | BindingFlags.Static)
                         ?.GetValue(null);
        if (nullLoggerInstance is null) return;

        Type instanceType = moduleInstance.GetType();

        FieldInfo? loggerField = instanceType.GetField(
            "Logger",
            BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.FlattenHierarchy);
        if (loggerField is not null)
        {
            try
            {
                loggerField.SetValue(moduleInstance, nullLoggerInstance);
                Log($"injected NullLogger into Logger field of {instanceType.Name}");
                return;
            }
            catch { /* field type mismatch -- try property below */ }
        }

        PropertyInfo? loggerProp = instanceType.GetProperty(
            "Logger",
            BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.FlattenHierarchy);
        if (loggerProp is not null && loggerProp.CanWrite)
        {
            try
            {
                loggerProp.SetValue(moduleInstance, nullLoggerInstance);
                Log($"injected NullLogger into Logger property of {instanceType.Name}");
            }
            catch { /* type mismatch; leave as-is */ }
        }
    }
}
