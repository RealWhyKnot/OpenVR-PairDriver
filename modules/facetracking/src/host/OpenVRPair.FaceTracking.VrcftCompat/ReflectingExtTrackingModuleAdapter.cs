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

    public ReflectingExtTrackingModuleAdapter()
    {
        string adapterLocation = typeof(ReflectingExtTrackingModuleAdapter).Assembly.Location;
        string adapterDir = Path.GetDirectoryName(adapterLocation)
            ?? throw new InvalidOperationException("Could not resolve the adapter assembly directory.");

        string bridgePath = Path.Combine(adapterDir, "bridge.json");
        if (!File.Exists(bridgePath))
        {
            throw new FileNotFoundException(
                $"bridge.json not found at {bridgePath}. The vrcft-registry import script must " +
                "emit this file alongside OpenVRPair.FaceTracking.VrcftCompat.dll.",
                bridgePath);
        }

        BridgeConfig cfg = ParseBridgeConfig(bridgePath);

        Assembly upstreamAsm = LoadUpstreamAssembly(adapterDir, cfg.UpstreamAssembly);
        Type upstreamType = upstreamAsm.GetType(cfg.UpstreamType, throwOnError: false)
            ?? throw new InvalidOperationException(
                $"Upstream type '{cfg.UpstreamType}' not found in '{cfg.UpstreamAssembly}'.");

        object upstreamInstance = TryConstruct(upstreamType, adapterDir)
            ?? throw new InvalidOperationException(
                $"Could not construct upstream module '{cfg.UpstreamType}'. " +
                "Expected a public parameterless constructor or a single-parameter " +
                "constructor accepting Microsoft.Extensions.Logging.ILogger.");

        ReflectingLegacyBridge legacy = new(upstreamInstance);

        ModuleManifest manifest = new()
        {
            Uuid          = cfg.Uuid,
            Name          = cfg.Name,
            Vendor        = cfg.Vendor,
            Version       = Version.Parse(cfg.Version),
            SupportedHmds = cfg.SupportedHmds.ToArray(),
            Capabilities  = ParseCapabilities(cfg.Capabilities),
        };

        _inner = new ExtTrackingModuleAdapter(legacy, manifest);
    }

    public override ModuleManifest Manifest => _inner.Manifest;

    public override (bool eye, bool expression) Initialize(bool eyeAvailable, bool expressionAvailable)
        => _inner.Initialize(eyeAvailable, expressionAvailable);

    public override void Update(in FrameContext ctx) => _inner.Update(in ctx);

    public override void Teardown() => _inner.Teardown();

    private static BridgeConfig ParseBridgeConfig(string path)
    {
        string json = File.ReadAllText(path);
        JsonSerializerOptions opts = new(JsonSerializerDefaults.Web);
        BridgeConfig? cfg = JsonSerializer.Deserialize<BridgeConfig>(json, opts);
        return cfg ?? throw new InvalidOperationException($"bridge.json at {path} is empty or unparseable.");
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

        AssemblyLoadContext alc = AssemblyLoadContext.GetLoadContext(
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
