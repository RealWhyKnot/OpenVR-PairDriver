using System.Reflection;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;
using OpenVRPair.FaceModuleHost.Logging;
using OpenVRPair.FaceTracking.ModuleSdk;
using OpenVRPair.FaceTracking.Registry;

namespace OpenVRPair.FaceModuleHost.Workers;

public sealed class LoadedModule(
    FaceTrackingModule instance,
    Manifest manifest,
    ModuleLoadContext context,
    ulong uuidHash)
{
    public FaceTrackingModule Instance { get; } = instance;
    public Manifest Manifest { get; } = manifest;
    public ModuleLoadContext Context { get; } = context;
    /// <summary>FNV-1a-64 of the UTF-8 UUID string. Cached at load time.</summary>
    public ulong UuidHash { get; } = uuidHash;
}

/// <summary>
/// Per-module collectible <see cref="AssemblyLoadContext"/> so a module can be
/// unloaded and reloaded without restarting the host process.
/// </summary>
public sealed class ModuleLoadContext(string modulePath) : AssemblyLoadContext(
    name: Path.GetFileName(modulePath), isCollectible: true)
{
    public string AssembliesDir => modulePath;

    // Assemblies that MUST always come from the host's default load context.
    // The host casts module instances to FaceTrackingModule (in ModuleSdk)
    // and many modules use the reflecting bridge (in VrcftCompat). If a
    // module ships its own copy of either DLL in assemblies/ and we let the
    // per-module ALC load it, the loaded type ends up distinct from the
    // host's type even though the FullName matches -- and the cast to
    // FaceTrackingModule throws InvalidCastException. Force these names to
    // resolve in the default context by returning null here; .NET then
    // falls back to the default ALC, sharing the host's types.
    internal static readonly HashSet<string> SharedAssemblyNames = new()
    {
        "OpenVRPair.FaceTracking.ModuleSdk",
        "OpenVRPair.FaceTracking.VrcftCompat",
        // Must resolve from the default ALC (where the host's own reference lives)
        // so the bridge's reflection on VRCFaceTracking.UnifiedTracking.Data finds
        // the same singleton that modules write to. If each module loads its own
        // copy the writes and reads go to different objects and no data flows.
        "VRCFaceTracking.Core",
        // VRCFaceTracking.Core v5.1.1.1 has a strong-name reference to v7.0.0.0;
        // modules that depend on VRCFaceTracking.Core inherit this dependency.
        // Pin to the host's vendored copy so all ALCs share one instance.
        "Microsoft.Extensions.Logging.Abstractions",
    };

    protected override Assembly? Load(AssemblyName assemblyName)
    {
        if (assemblyName.Name is { } name && SharedAssemblyNames.Contains(name))
        {
            return null;
        }

        // Resolve the assembly from the module's own directory first, then fall
        // back to the default context so the SDK and BCL types are shared.
        string candidate = Path.Combine(modulePath, assemblyName.Name + ".dll");
        return File.Exists(candidate) ? LoadFromAssemblyPath(candidate) : null;
    }

    // Native libraries the upstream module pulls in via P/Invoke (SRanipal,
    // openxr_loader, starvr_api, tobii_stream_engine, libHTC_License, nanomsg,
    // etc.) live in the same assemblies/ folder as the managed DLLs. .NET's
    // default native-library search doesn't probe an ALC's per-context
    // directory; we point it there explicitly so a vendor's first P/Invoke
    // doesn't throw DllNotFoundException with the file sitting right next to it.
    protected override IntPtr LoadUnmanagedDll(string unmanagedDllName)
    {
        foreach (string name in new[] { unmanagedDllName, unmanagedDllName + ".dll" })
        {
            string candidate = Path.Combine(modulePath, name);
            if (File.Exists(candidate))
            {
                return LoadUnmanagedDllFromPath(candidate);
            }
        }
        return IntPtr.Zero; // fall back to the default resolver
    }
}

/// <summary>
/// Discovers, validates, loads, and (on demand) unloads hardware modules from the
/// per-module install directories under <c>%LocalAppDataLow%\WKOpenVR\facetracking\modules\</c>.
/// </summary>
public sealed class ModuleLoader(
    HostOptions opts,
    HostLogger logger)
{
    private static readonly JsonSerializerOptions JsonOpts = new(JsonSerializerDefaults.Web);
    private readonly List<LoadedModule> _loaded = [];
    private LoadedModule? _active;

    public IReadOnlyList<LoadedModule> Loaded => _loaded;

    /// <summary>The currently-active module, or null if no module is selected.</summary>
    public LoadedModule? Active => _active;

    public static IReadOnlyCollection<string> SharedAssemblyNamesSnapshot() => ModuleLoadContext.SharedAssemblyNames;

    public async Task<IReadOnlyList<LoadedModule>> LoadAllAsync()
    {
        _loaded.Clear();
        if (!Directory.Exists(opts.ModulesInstallDir))
        {
            logger.Info("Module install directory does not exist; no modules loaded.");
            return _loaded;
        }

        foreach (string uuidDir in Directory.EnumerateDirectories(opts.ModulesInstallDir))
        {
            // Each uuid directory may contain multiple version sub-directories.
            // Use the lexicographically greatest version (good enough for semver A.B.C).
            string? versionDir = Directory.EnumerateDirectories(uuidDir)
                .OrderDescending()
                .FirstOrDefault();
            if (versionDir is null) continue;

            await TryLoadModuleAsync(versionDir);
        }

        return _loaded;
    }

    public async Task UnloadAllAsync()
    {
        foreach (var m in _loaded)
        {
            try   { m.Instance.Teardown(); }
            catch (Exception ex) { logger.Warn($"Teardown error [{m.Manifest.Name}]: {ex.Message}"); }
            m.Context.Unload();
        }
        _loaded.Clear();
        _active = null;
        await Task.CompletedTask;
    }

    public void SelectActive(string uuid)
    {
        var m = _loaded.FirstOrDefault(m => m.Manifest.Uuid == uuid);
        if (m is null)
        {
            logger.Warn($"SelectActive: module {uuid} not found in loaded set.");
            return;
        }
        _active = m;
        logger.Info($"Active module set to {m.Manifest.Name} ({uuid}).");
    }

    /// <summary>
    /// Main per-frame loop. Calls the active module's Update and pushes the result
    /// into the frame writer. Runs until cancellation.
    /// </summary>
    public async Task RunActiveAsync(
        FrameWriter writer,
        CalibrationCache calib,
        CancellationToken ct)
    {
        // Idle if no module is active; re-check every 250 ms.
        while (!ct.IsCancellationRequested)
        {
            if (_active is null || _loaded.Count == 0)
            {
                await Task.Delay(250, ct);
                continue;
            }

            var loadedModule = _active;
            var module       = loadedModule.Instance;
            ulong uuidHash   = loadedModule.UuidHash;
            var (eye, expr)  = module.Initialize(eyeAvailable: true, expressionAvailable: true);
            logger.Info($"Module {loadedModule.Manifest.Name} initialized. eye={eye} expression={expr}");

            long prevTick    = System.Diagnostics.Stopwatch.GetTimestamp();
            float invFreq    = 1.0f / (float)System.Diagnostics.Stopwatch.Frequency;
            long  frameNum   = 0;
            int   noDataRun  = 0;

            try
            {
                while (!ct.IsCancellationRequested && ReferenceEquals(_active, loadedModule))
                {
                    long   now       = System.Diagnostics.Stopwatch.GetTimestamp();
                    float  delta     = (now - prevTick) * invFreq;
                    prevTick         = now;
                    long   hns       = DateTime.UtcNow.ToFileTimeUtc();
                    var    ctx       = new FrameContext(default, hns, delta);

                    module.Update(in ctx);

                    // Heartbeat: log sample values every 60 frames.
                    frameNum++;
                    var exprSink = module.ExpressionSinkInternal;
                    var eyeSink  = module.EyeSinkInternal;
                    ReadOnlySpan<float> shapes = exprSink.Values;
                    int nonZero = 0;
                    foreach (float v in shapes) if (v != 0f) nonZero++;
                    float jawOpen  = shapes.Length > 26 ? shapes[26] : 0f;
                    float leftLid  = eyeSink.LeftOpenness;

                    if (nonZero == 0 && leftLid == 0f) {
                        noDataRun++;
                        if (noDataRun % 60 == 0)
                            logger.Warn($"[host] module={loadedModule.Manifest.Name} no-data for {noDataRun} frames");
                    } else {
                        noDataRun = 0;
                    }

                    if (frameNum % 60 == 0)
                    {
                        logger.Info($"[host] module={loadedModule.Manifest.Name} frame={frameNum} jawOpen={jawOpen:F3} leftEyeLid={leftLid:F3} nonZeroShapes={nonZero}/{shapes.Length}");
                    }

                    await writer.PublishAsync(
                        eyeSink,
                        exprSink,
                        eye, expr, uuidHash, ct);

                    // Yield so other tasks get CPU time; hardware ticks at ~120 Hz.
                    await Task.Yield();
                }
            }
            catch (OperationCanceledException) { break; }
            catch (Exception ex)
            {
                logger.Error($"[host] module={loadedModule.Manifest.Name} threw during Update: {ex.GetType().Name} {ex.Message}");
                _active = null;
            }
        }
    }

    /// <summary>FNV-1a-64 over the UTF-8 bytes of <paramref name="s"/>.</summary>
    private static ulong Fnv1a64(string s)
    {
        const ulong OffsetBasis = 14695981039346656037UL;
        const ulong Prime       = 1099511628211UL;

        ulong hash = OffsetBasis;
        foreach (byte b in Encoding.UTF8.GetBytes(s))
        {
            hash ^= b;
            hash *= Prime;
        }
        return hash;
    }

    private async Task TryLoadModuleAsync(string dir)
    {
        string manifestPath = Path.Combine(dir, "manifest.json");
        if (!File.Exists(manifestPath))
        {
            logger.Warn($"No manifest.json in {dir}; skipping.");
            return;
        }

        Manifest manifest;
        try
        {
            await using var f = File.OpenRead(manifestPath);
            manifest = await JsonSerializer.DeserializeAsync<Manifest>(f, JsonOpts)
                ?? throw new InvalidDataException("Null manifest.");
        }
        catch (Exception ex)
        {
            logger.Warn($"Failed to parse manifest in {dir}: {ex.Message}");
            return;
        }

        // Integrity: the manifest's payload_sha256 was checked at install
        // time against the downloaded zip. We don't re-hash on every load
        // (the assemblies/ tree is already extracted and the user's own
        // disk is trusted). No cryptographic signature is required --
        // both registries (legacy mirror + the future native one) are
        // curated by the maintainer, so trust is administrative.

        string assemblyPath = Path.Combine(dir, "assemblies", manifest.EntryAssembly);
        if (!File.Exists(assemblyPath))
        {
            logger.Warn($"Entry assembly not found: {assemblyPath}");
            return;
        }

        try
        {
            var ctx      = new ModuleLoadContext(Path.Combine(dir, "assemblies"));
            var asm      = ctx.LoadFromAssemblyPath(assemblyPath);
            var type     = asm.GetType(manifest.EntryType)
                ?? throw new InvalidOperationException(
                    $"Type {manifest.EntryType} not found in {manifest.EntryAssembly}.");

            var instance = (FaceTrackingModule)(Activator.CreateInstance(type)
                ?? throw new InvalidOperationException("Activator returned null."));

            ulong uuidHash = Fnv1a64(manifest.Uuid);
            _loaded.Add(new LoadedModule(instance, manifest, ctx, uuidHash));
            logger.Info($"Loaded module {manifest.Name} {manifest.Version} from {dir}.");
        }
        catch (Exception ex)
        {
            logger.Error($"Failed to load module from {dir}: {ex}");
        }
    }
}
