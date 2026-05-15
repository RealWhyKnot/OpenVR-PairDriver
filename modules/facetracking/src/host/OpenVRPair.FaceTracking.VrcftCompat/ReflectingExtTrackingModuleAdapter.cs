// Entry-point FaceTrackingModule that loads at runtime via a bridge.json
// config and reflects into an upstream VRCFaceTracking ExtTrackingModule.
//
// Lifecycle:
//   1. Host calls Activator.CreateInstance on this type (parameterless).
//   2. Ctor reads bridge.json from this DLL's directory.
//   3. Ctor loads the upstream assembly + type via reflection.
//   4. Ctor instantiates the upstream module (parameterless ctor, or
//      single-ILogger ctor with NullLogger if available).
//   5. Ctor wraps the upstream instance in ReflectingLegacyBridge, then
//      wraps that bridge in the existing ExtTrackingModuleAdapter.
//   6. Host calls Initialize / Update / Teardown on this adapter; calls
//      delegate to the inner ExtTrackingModuleAdapter which handles the
//      structural mapping (gaze, openness, shapes).
//
// What this lets the registry packager do: drop a VRCFT-format upstream
// DLL plus its SDK dependencies into assemblies/, emit a bridge.json
// alongside this DLL, set manifest.entry_type to
// "OpenVRPair.FaceTracking.VrcftCompat.ReflectingExtTrackingModuleAdapter",
// sign, ship. No per-module C# wrapper is required.

using System.Reflection;
using System.Runtime.Loader;
using System.Text.Json;
using OpenVRPair.FaceTracking.ModuleSdk;

namespace OpenVRPair.FaceTracking.VrcftCompat;

public sealed class ReflectingExtTrackingModuleAdapter : FaceTrackingModule
{
    private readonly ExtTrackingModuleAdapter _inner;

    public static System.Threading.AsyncLocal<string?> CurrentAdapterDir { get; } = new();
    public static System.Threading.AsyncLocal<AssemblyLoadContext?> CurrentUpstreamAlc { get; } = new();
    public static Action<string>? SinkLine { get; set; }

    public ReflectingExtTrackingModuleAdapter()
    {
        string adapterDir = CurrentAdapterDir.Value
            ?? Path.GetDirectoryName(typeof(ReflectingExtTrackingModuleAdapter).Assembly.Location)
            ?? throw new InvalidOperationException("Could not resolve the adapter assembly directory.");
        string adapterAlcName = AssemblyLoadContext.GetLoadContext(typeof(ReflectingExtTrackingModuleAdapter).Assembly)?.Name ?? "default";
        string bridgeAlcName = AssemblyLoadContext.GetLoadContext(typeof(ReflectingLegacyBridge).Assembly)?.Name ?? "default";
        Log($"[ctor] adapterDir={adapterDir} typeAlc={adapterAlcName} bridgeAlc={bridgeAlcName}");

        string bridgePath = Path.Combine(adapterDir, "bridge.json");
        if (!File.Exists(bridgePath))
        {
            throw new FileNotFoundException(
                $"bridge.json not found at {bridgePath}. The wkvrcft-legacy-registry import script must " +
                "emit this file alongside OpenVRPair.FaceTracking.VrcftCompat.dll.",
                bridgePath);
        }

        BridgeConfig cfg = ParseBridgeConfig(bridgePath);
        Log($"loading: uuid={cfg.Uuid} name=\"{cfg.Name}\" version={cfg.Version} " +
            $"upstream_assembly={cfg.UpstreamAssembly} upstream_type={cfg.UpstreamType}");

        Assembly upstreamAsm = LoadUpstreamAssembly(adapterDir, cfg.UpstreamAssembly);
        Log($"loaded upstream assembly: {upstreamAsm.GetName().Name} v{upstreamAsm.GetName().Version}");

        Type? upstreamType = upstreamAsm.GetType(cfg.UpstreamType, throwOnError: false);
        if (upstreamType is null)
        {
            Type[] visibleTypes;
            try { visibleTypes = upstreamAsm.GetTypes(); }
            catch (ReflectionTypeLoadException rtle)
            {
                visibleTypes = rtle.Types.Where(t => t is not null).ToArray()!;
                foreach (var le in rtle.LoaderExceptions.Where(e => e is not null).Take(5))
                    Log($"  LoaderException: {le!.GetType().Name}: {le.Message}");
            }
            Log($"  configured type '{cfg.UpstreamType}' missing; scanning {visibleTypes.Length} visible types for any inheriting from ExtTrackingModule");
            var candidates = visibleTypes
                .Where(t => t is not null && t.IsClass && !t.IsAbstract && InheritsExtTrackingModule(t))
                .ToArray();
            foreach (var c in candidates) Log($"  candidate: {c.FullName}");
            upstreamType = candidates.FirstOrDefault()
                ?? throw new InvalidOperationException(
                    $"Upstream type '{cfg.UpstreamType}' not found in '{cfg.UpstreamAssembly}' and no fallback ExtTrackingModule subclass discovered. " +
                    $"Visible types ({visibleTypes.Length}): {string.Join(", ", visibleTypes.Take(10).Select(t => t?.FullName))}");
            Log($"upstream type resolved via inheritance scan: {upstreamType.FullName} (configured was {cfg.UpstreamType})");
        }
        else
        {
            Log($"upstream type resolved: {upstreamType.FullName}");
        }

        object upstreamInstance = TryConstruct(upstreamType, adapterDir)
            ?? throw new InvalidOperationException(
                $"Could not construct upstream module '{cfg.UpstreamType}'. " +
                "Expected a public parameterless constructor or a single-parameter " +
                "constructor accepting Microsoft.Extensions.Logging.ILogger.");
        Log($"upstream instance constructed via {upstreamInstance.GetType().GetConstructors().FirstOrDefault()?.GetParameters().Length switch { 0 => "parameterless ctor", 1 => "(ILogger) ctor", _ => "other ctor" }}");

        ReflectingLegacyBridge legacy = new(upstreamInstance);
        Log("reflecting bridge ready; constructing inner ExtTrackingModuleAdapter");

        ModuleManifest manifest = new()
        {
            Uuid          = cfg.Uuid,
            Name          = cfg.Name,
            Vendor        = cfg.Vendor,
            Version       = ParseLenientVersion(cfg.Version),
            SupportedHmds = cfg.SupportedHmds.ToArray(),
            Capabilities  = ParseCapabilities(cfg.Capabilities),
        };

        _inner = new ExtTrackingModuleAdapter(legacy, manifest);
    }

    public override ModuleManifest Manifest => _inner.Manifest;

    public override (bool eye, bool expression) Initialize(bool eyeAvailable, bool expressionAvailable)
    {
        Log($"Initialize(eyeAvailable={eyeAvailable}, expressionAvailable={expressionAvailable})");
        var result = _inner.Initialize(eyeAvailable, expressionAvailable);
        Log($"Initialize returned (eye={result.eye}, expression={result.expression})");
        return result;
    }

    public override void Update(in FrameContext ctx) => _inner.Update(in ctx);

    public override void Teardown()
    {
        Log("Teardown");
        _inner.Teardown();
    }

    // Writes a single line to stderr so the driver-side HostSupervisor picks it
    // up into facetracking_log.<ts>.txt. The C# host's HostLogger is not
    // injected into VrcftCompat (the adapter is a leaf assembly with no host
    // dependencies); stderr is the simplest single-direction back-channel.
    private static void Log(string message)
    {
        var sink = SinkLine;
        if (sink is not null) { sink($"[ft/vrcft-bridge] {message}"); return; }
        Console.Error.WriteLine($"[ft/vrcft-bridge] {message}");
    }

    private static BridgeConfig ParseBridgeConfig(string path)
    {
        string json = File.ReadAllText(path);
        JsonSerializerOptions opts = new(JsonSerializerDefaults.Web);
        BridgeConfig? cfg = JsonSerializer.Deserialize<BridgeConfig>(json, opts);
        return cfg ?? throw new InvalidOperationException($"bridge.json at {path} is empty or unparseable.");
    }

    private static bool InheritsExtTrackingModule(Type t)
    {
        for (Type? cur = t.BaseType; cur is not null && cur != typeof(object); cur = cur.BaseType)
        {
            if (cur.Name == "ExtTrackingModule") return true;
        }
        return false;
    }

    private static Assembly LoadUpstreamAssembly(string dir, string filename)
    {
        string upstreamPath = Path.Combine(dir, filename);
        if (!File.Exists(upstreamPath))
        {
            throw new FileNotFoundException(
                $"Upstream assembly '{filename}' not found alongside the adapter DLL.",
                upstreamPath);
        }

        AssemblyLoadContext alc = CurrentUpstreamAlc.Value
                                  ?? AssemblyLoadContext.GetLoadContext(
                                         typeof(ReflectingExtTrackingModuleAdapter).Assembly)
                                  ?? AssemblyLoadContext.Default;
        return alc.LoadFromAssemblyPath(upstreamPath);
    }

    private static object? TryConstruct(Type type, string adapterDir)
    {
        ConstructorInfo? parameterless = type.GetConstructor(Type.EmptyTypes);
        if (parameterless is not null)
        {
            return parameterless.Invoke(parameters: null);
        }

        // Try a single-parameter constructor that accepts ILogger.
        foreach (ConstructorInfo ctor in type.GetConstructors())
        {
            ParameterInfo[] parameters = ctor.GetParameters();
            if (parameters.Length != 1) continue;
            if (parameters[0].ParameterType.FullName != "Microsoft.Extensions.Logging.ILogger") continue;

            object? nullLogger = ResolveNullLogger(adapterDir);
            if (nullLogger is null) continue;
            return ctor.Invoke(new[] { nullLogger });
        }

        return null;
    }

    private static object? ResolveNullLogger(string adapterDir)
    {
        const string AbstractionsAsmName = "Microsoft.Extensions.Logging.Abstractions";
        const string NullLoggerTypeName  = "Microsoft.Extensions.Logging.Abstractions.NullLogger";

        Assembly? loggingAsm = AppDomain.CurrentDomain.GetAssemblies()
            .FirstOrDefault(a => string.Equals(a.GetName().Name, AbstractionsAsmName, StringComparison.Ordinal));

        if (loggingAsm is null)
        {
            string sidecar = Path.Combine(adapterDir, $"{AbstractionsAsmName}.dll");
            if (File.Exists(sidecar))
            {
                AssemblyLoadContext alc = AssemblyLoadContext.GetLoadContext(
                                              typeof(ReflectingExtTrackingModuleAdapter).Assembly)
                                          ?? AssemblyLoadContext.Default;
                loggingAsm = alc.LoadFromAssemblyPath(sidecar);
            }
        }

        Type? nullLoggerType = loggingAsm?.GetType(NullLoggerTypeName, throwOnError: false);
        return nullLoggerType?.GetProperty("Instance", BindingFlags.Public | BindingFlags.Static)?.GetValue(null);
    }

    // System.Version.Parse rejects any non-numeric component, so legitimate
    // VRCFT module versions like "1.0.5-fix" or "1.6.0-beta" throw before
    // the module ever loads. Strip everything from the first '-' or '+'
    // onward (semver pre-release / build-metadata suffixes), then trim any
    // trailing dots, then fall back to a zeroed Version if the result still
    // doesn't parse. A bad-version-string module is still useful to load --
    // the version is descriptive metadata, not a load gate.
    private static Version ParseLenientVersion(string raw)
    {
        string cleaned = raw ?? string.Empty;
        int cut = cleaned.IndexOfAny(new[] { '-', '+' });
        if (cut >= 0) cleaned = cleaned[..cut];
        cleaned = cleaned.TrimEnd('.', ' ');
        return Version.TryParse(cleaned, out Version? v) ? v : new Version(0, 0, 0, 0);
    }

    private static Capabilities ParseCapabilities(IList<string> caps)
    {
        Capabilities result = Capabilities.None;
        foreach (string c in caps)
        {
            result |= c.Trim().ToLowerInvariant() switch
            {
                "eye"                          => Capabilities.Eye,
                "expression" or "expressions"  => Capabilities.Expression,
                _                              => Capabilities.None,
            };
        }
        return result;
    }
}
