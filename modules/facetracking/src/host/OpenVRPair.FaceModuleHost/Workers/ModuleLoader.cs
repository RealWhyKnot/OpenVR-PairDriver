using System.Reflection;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;
using OpenVRPair.FaceModuleHost.Logging;
using OpenVRPair.FaceModuleHost.Output;
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
    protected override Assembly? Load(AssemblyName assemblyName)
    {
        // Resolve the assembly from the module's own directory first, then fall
        // back to the default context so the SDK and BCL types are shared.
        string candidate = Path.Combine(modulePath, assemblyName.Name + ".dll");
        return File.Exists(candidate) ? LoadFromAssemblyPath(candidate) : null;
    }
}

/// <summary>
/// Discovers, validates, loads, and (on demand) unloads hardware modules from the
/// per-module install directories under <c>%LocalAppDataLow%\OpenVR-Pair\facetracking\modules\</c>.
/// </summary>
public sealed class ModuleLoader(
    HostOptions opts,
    HostLogger logger)
{
    private static readonly JsonSerializerOptions JsonOpts = new(JsonSerializerDefaults.Web);
    private readonly List<LoadedModule> _loaded = [];
    private LoadedModule? _active;

    public IReadOnlyList<LoadedModule> Loaded => _loaded;

    public async Task<IReadOnlyList<LoadedModule>> LoadAllAsync()
    {
        _loaded.Clear();
        if (!Directory.Exists(opts.ModulesInstallDir))
        {
            logger.Info("Module install directory does not exist; no modules loaded.");
            return _loaded;
        }

        var trustStore = new TrustStore();
        try   { trustStore.Load(); }
        catch (Exception ex) { logger.Warn($"Trust store load failed: {ex.Message}"); }

        foreach (string uuidDir in Directory.EnumerateDirectories(opts.ModulesInstallDir))
        {
            // Each uuid directory may contain multiple version sub-directories.
            // Use the lexicographically greatest version (good enough for semver A.B.C).
            string? versionDir = Directory.EnumerateDirectories(uuidDir)
                .OrderDescending()
                .FirstOrDefault();
            if (versionDir is null) continue;

            await TryLoadModuleAsync(versionDir, trustStore);
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
    /// into the frame writer and OSC sender. Runs until cancellation.
    /// </summary>
    public async Task RunActiveAsync(
        FrameWriter writer,
        OscSender osc,
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

                    await writer.PublishAsync(
                        module.EyeSinkInternal,
                        module.ExpressionSinkInternal,
                        eye, expr, uuidHash, ct);

                    osc.SendFrame(
                        module.EyeSinkInternal,
                        module.ExpressionSinkInternal,
                        eye, expr);

                    // Yield so other tasks get CPU time; hardware ticks at ~120 Hz.
                    await Task.Yield();
                }
            }
            catch (OperationCanceledException) { break; }
            catch (Exception ex)
            {
                logger.Error($"Module {loadedModule.Manifest.Name} threw during Update: {ex}");
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

    private async Task TryLoadModuleAsync(string dir, TrustStore trustStore)
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

        // Signature verification before assembly load.
        string signaturePath = Path.Combine(dir, "signature.bin");
        if (File.Exists(signaturePath))
        {
            byte[] sigBytes;
            try   { sigBytes = await File.ReadAllBytesAsync(signaturePath); }
            catch (Exception ex)
            {
                logger.Warn($"Failed to read signature for {manifest.Name}: {ex.Message}; skipping.");
                return;
            }

            if (!Ed25519Verifier.VerifyPayloadHash(manifest, sigBytes, trustStore))
            {
                logger.Error($"Signature verification failed for {manifest.Name} ({manifest.Uuid}); skipping.");
                return;
            }

            logger.Info($"Signature verified for {manifest.Name} ({manifest.Uuid}).");
        }
        else
        {
            // No signature file present.
            if (!opts.AllowUnsigned)
            {
                logger.Warn($"No signature file for {manifest.Name} ({manifest.Uuid}) and --allow-unsigned not set; skipping.");
                return;
            }
            logger.Warn($"No signature file for {manifest.Name} ({manifest.Uuid}); loading anyway (--allow-unsigned).");
        }

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
