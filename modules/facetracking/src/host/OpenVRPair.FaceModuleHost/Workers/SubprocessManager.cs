using System.Diagnostics;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using OpenVRPair.FaceModuleHost.Logging;
using OpenVRPair.FaceTracking.ModuleSdk;
using OpenVRPair.FaceTracking.Registry;
using VRCFaceTracking.Core.Library;
using VRCFaceTracking.Core.Sandboxing;
using VRCFaceTracking.Core.Sandboxing.IPC;

namespace OpenVRPair.FaceModuleHost.Workers;

/// <summary>
/// Drop-in replacement for <see cref="ModuleLoader"/> using upstream's subprocess-per-module
/// architecture. Spawns one <c>WKOpenVR.FaceModuleProcess.exe</c> at a time; tears the old one
/// down before starting a new one when <see cref="SelectActive"/> is called.
///
/// Program.cs still instantiates ModuleLoader in this commit; this class compiles but is not
/// called until the runtime-switch commit.
/// </summary>
public sealed class SubprocessManager : IDisposable
{
    // bridge.json fields we read to resolve the upstream DLL path.
    private sealed class BridgeConfig
    {
        [JsonPropertyName("upstream_assembly")] public required string UpstreamAssembly { get; init; }
    }

    private sealed class ActiveSubprocess : IDisposable
    {
        public Process Process { get; init; } = null!;
        public int Port { get; init; }
        public DiscoveredModule Module { get; init; } = null!;
        public bool Exited { get; private set; }

        public void MarkExited() => Exited = true;

        public void Dispose()
        {
            try { if (!Process.HasExited) Process.Kill(entireProcessTree: false); } catch { }
            Process.Dispose();
        }
    }

    private static readonly JsonSerializerOptions JsonOpts = new(JsonSerializerDefaults.Web);

    private const string VrcftCompatAdapterType =
        "OpenVRPair.FaceTracking.VrcftCompat.ReflectingExtTrackingModuleAdapter";

    private readonly HostOptions    _opts;
    private readonly HostLogger     _logger;
    private readonly VrcftSandboxServer _server;
    private readonly string         _subprocessExePath;

    private readonly List<DiscoveredModule> _loaded = [];
    private DiscoveredModule?   _activeModule;
    private ActiveSubprocess?   _activeProcess;

    // Circuit breaker state: track consecutive fast-exit crashes per module uuid.
    private string? _breakerUuid;
    private int     _breakerCount;
    private const int CrashHaltThreshold = 3;
    private static readonly TimeSpan FastExitThreshold = TimeSpan.FromSeconds(5);

    // Minimal logger factory for VrcftSandboxServer (it takes ILoggerFactory).
    // We provide a no-op factory since the server's internal logger output goes nowhere useful
    // in our host; we surface events via our own HostLogger.
    private static readonly Microsoft.Extensions.Logging.ILoggerFactory _nullLoggerFactory =
        Microsoft.Extensions.Logging.Abstractions.NullLoggerFactory.Instance;

    public SubprocessManager(HostOptions opts, HostLogger logger)
    {
        _opts   = opts;
        _logger = logger;
        _subprocessExePath = Path.Combine(
            Path.GetDirectoryName(typeof(SubprocessManager).Assembly.Location)!,
            "WKOpenVR.FaceModuleProcess.exe");

        if (!File.Exists(_subprocessExePath))
            throw new FileNotFoundException(
                $"[SubprocessManager] subprocess exe not found at {_subprocessExePath}");

        _server = new VrcftSandboxServer(_nullLoggerFactory, reservedPorts: []);
        _server.OnPacketReceived += OnServerPacket;
    }

    // -------------------------------------------------------------------------
    // Public surface (mirrors ModuleLoader)
    // -------------------------------------------------------------------------

    public DiscoveredModule? Active => _activeModule;
    public IReadOnlyList<DiscoveredModule> Loaded => _loaded;

    public async Task<IReadOnlyList<DiscoveredModule>> LoadAllAsync()
    {
        _loaded.Clear();
        if (!Directory.Exists(_opts.ModulesInstallDir))
        {
            _logger.Info("[SubprocessManager] Module install directory does not exist; no modules loaded.");
            return _loaded;
        }

        foreach (string uuidDir in Directory.EnumerateDirectories(_opts.ModulesInstallDir))
        {
            string? versionDir = Directory.EnumerateDirectories(uuidDir)
                .OrderDescending()
                .FirstOrDefault();
            if (versionDir is null) continue;

            await TryDiscoverModuleAsync(versionDir);
        }

        return _loaded;
    }

    public async Task UnloadAllAsync()
    {
        await TeardownActiveAsync(ct: CancellationToken.None);
        _loaded.Clear();
        _activeModule = null;
    }

    public void SelectActive(string uuid)
    {
        var m = _loaded.FirstOrDefault(m => m.Uuid == uuid);
        if (m is null)
        {
            _logger.Warn($"[SubprocessManager] SelectActive: module {uuid} not found.");
            return;
        }
        _activeModule = m;
        _logger.Info($"[SubprocessManager] Active module set to {m.Manifest.Name} ({uuid}).");
    }

    /// <summary>
    /// Pull loop: spawns the active subprocess, drives EventUpdate at ~120 Hz,
    /// decodes ReplyUpdate, maps shapes, calls FrameWriter.PublishAsync.
    /// </summary>
    public async Task RunActiveAsync(
        FrameWriter writer,
        CalibrationCache calib,
        CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            if (_activeModule is null)
            {
                await Task.Delay(250, ct);
                continue;
            }

            var module = _activeModule;

            // Circuit-breaker: stop respawning if this module keeps crashing fast.
            if (_breakerUuid == module.Uuid && _breakerCount >= CrashHaltThreshold)
            {
                _logger.Error($"[spawn] subprocess crashed {CrashHaltThreshold} times; halting respawn for {module.Uuid}");
                _activeModule = null;
                continue;
            }

            await SpawnAndRunAsync(module, writer, ct);
        }
    }

    public void Dispose()
    {
        _activeProcess?.Dispose();
        _activeProcess = null;
        _server.Dispose();
    }

    // -------------------------------------------------------------------------
    // Internal: discovery
    // -------------------------------------------------------------------------

    private async Task TryDiscoverModuleAsync(string dir)
    {
        string manifestPath = Path.Combine(dir, "manifest.json");
        if (!File.Exists(manifestPath))
        {
            _logger.Warn($"[SubprocessManager] No manifest.json in {dir}; skipping.");
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
            _logger.Warn($"[SubprocessManager] Failed to parse manifest in {dir}: {ex.Message}");
            return;
        }

        // Resolve the upstream module DLL path.
        // If the manifest uses the VrcftCompat adapter, read bridge.json to get the real DLL.
        string moduleDllPath;
        if (manifest.EntryType == VrcftCompatAdapterType)
        {
            string bridgePath = Path.Combine(dir, "assemblies", "bridge.json");
            if (!File.Exists(bridgePath))
            {
                _logger.Warn($"[SubprocessManager] bridge.json not found at {bridgePath}; skipping {manifest.Name}.");
                return;
            }
            try
            {
                await using var f = File.OpenRead(bridgePath);
                var cfg = await JsonSerializer.DeserializeAsync<BridgeConfig>(f, JsonOpts)
                    ?? throw new InvalidDataException("Null bridge config.");
                moduleDllPath = Path.Combine(dir, "assemblies", cfg.UpstreamAssembly);
            }
            catch (Exception ex)
            {
                _logger.Warn($"[SubprocessManager] Failed to parse bridge.json in {dir}: {ex.Message}");
                return;
            }
        }
        else
        {
            moduleDllPath = Path.Combine(dir, "assemblies", manifest.EntryAssembly);
        }

        if (!File.Exists(moduleDllPath))
        {
            _logger.Warn($"[SubprocessManager] Module DLL not found: {moduleDllPath}; skipping {manifest.Name}.");
            return;
        }

        ulong hash = Fnv1a64(manifest.Uuid);
        _loaded.Add(new DiscoveredModule(manifest.Uuid, manifest, moduleDllPath, hash));
        _logger.Info($"[SubprocessManager] Discovered {manifest.Name} {manifest.Version} dll={Path.GetFileName(moduleDllPath)}");
    }

    // -------------------------------------------------------------------------
    // Internal: subprocess lifecycle
    // -------------------------------------------------------------------------

    private readonly TaskCompletionSource<ReplyUpdatePacket> _updateTcs =
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    private TaskCompletionSource<bool> _handshakeTcs =
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    private TaskCompletionSource<ReplySupportedPacket> _supportedTcs =
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    private TaskCompletionSource<ReplyInitPacket> _initTcs =
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    private TaskCompletionSource<ReplyTeardownPacket> _teardownTcs =
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    private ReplyUpdatePacket? _latestUpdate;
    private int _activePort;

    private void OnServerPacket(in IpcPacket packet, in int port)
    {
        switch (packet.GetPacketType())
        {
            case IpcPacket.PacketType.Handshake:
                _logger.Info($"[ipc] Handshake from port={port}");
                _activePort = port;
                _handshakeTcs.TrySetResult(true);
                break;

            case IpcPacket.PacketType.ReplyGetSupported:
                var supported = (ReplySupportedPacket)packet;
                _logger.Info($"[ipc] ReplyGetSupported eye={supported.eyeAvailable} expr={supported.expressionAvailable}");
                _supportedTcs.TrySetResult(supported);
                break;

            case IpcPacket.PacketType.ReplyInit:
                var init = (ReplyInitPacket)packet;
                _logger.Info($"[ipc] ReplyInit eye={init.eyeSuccess} expr={init.expressionSuccess} name={init.ModuleInformationName}");
                _initTcs.TrySetResult(init);
                break;

            case IpcPacket.PacketType.ReplyUpdate:
                _latestUpdate = (ReplyUpdatePacket)packet;
                break;

            case IpcPacket.PacketType.ReplyTeardown:
                _logger.Info("[ipc] ReplyTeardown received");
                _teardownTcs.TrySetResult((ReplyTeardownPacket)packet);
                break;

            case IpcPacket.PacketType.EventLog:
                var log = (EventLogPacket)packet;
                _logger.Info($"[subprocess] {log.Message}");
                break;
        }
    }

    private async Task SpawnAndRunAsync(
        DiscoveredModule module,
        FrameWriter writer,
        CancellationToken ct)
    {
        // Reset per-run TCS objects.
        _handshakeTcs  = new(TaskCreationOptions.RunContinuationsAsynchronously);
        _supportedTcs  = new(TaskCreationOptions.RunContinuationsAsynchronously);
        _initTcs       = new(TaskCreationOptions.RunContinuationsAsynchronously);
        _teardownTcs   = new(TaskCreationOptions.RunContinuationsAsynchronously);
        _latestUpdate  = null;

        int serverPort = _server.Port;

        _logger.Info($"[spawn] starting {Path.GetFileName(_subprocessExePath)} " +
                     $"--port {serverPort} --module-path {module.ModuleDllPath} --parent-pid {Environment.ProcessId}");

        var psi = new ProcessStartInfo(_subprocessExePath)
        {
            UseShellExecute   = false,
            CreateNoWindow    = true,
            Arguments         = $"--port {serverPort} --module-path \"{module.ModuleDllPath}\" --parent-pid {Environment.ProcessId}",
        };

        Process proc;
        try
        {
            proc = Process.Start(psi)
                ?? throw new InvalidOperationException("Process.Start returned null.");
        }
        catch (Exception ex)
        {
            _logger.Error($"[spawn] failed to start subprocess: {ex.Message}");
            return;
        }

        _logger.Info($"[spawn] subprocess PID={proc.Id}");

        var spawnTime = DateTime.UtcNow;
        var active = new ActiveSubprocess { Process = proc, Port = serverPort, Module = module };
        _activeProcess = active;

        proc.EnableRaisingEvents = true;
        proc.Exited += (_, _) =>
        {
            active.MarkExited();
            _logger.Info($"[spawn] subprocess PID={proc.Id} exited code={TryGetExitCode(proc)}");
        };

        try
        {
            using var cts2 = CancellationTokenSource.CreateLinkedTokenSource(ct);
            cts2.CancelAfter(TimeSpan.FromSeconds(60)); // handshake timeout

            await _handshakeTcs.Task.WaitAsync(cts2.Token);

            // GetSupported
            SendToSubprocess(new EventInitGetSupported());
            await _supportedTcs.Task.WaitAsync(cts2.Token);

            // Init
            SendToSubprocess(new EventInitPacket { eyeAvailable = true, expressionAvailable = true });
            cts2.CancelAfter(TimeSpan.FromSeconds(60));
            var initReply = await _initTcs.Task.WaitAsync(cts2.Token);

            // Tell module to start sampling hardware.
            SendToSubprocess(new EventStatusUpdatePacket { ModuleState = ModuleState.Active });
            _logger.Info("[ipc] sent EventStatusUpdate(Active)");

            // Pull loop at 120 Hz.
            var exprSink = new ExpressionFrameSink();
            var eyeSink  = new EyeFrameSink();
            long frameNum    = 0;
            int  noDataRun   = 0;

            while (!ct.IsCancellationRequested && !active.Exited &&
                   ReferenceEquals(_activeModule, module))
            {
                SendToSubprocess(new EventUpdatePacket());

                // Brief yield to let the server's async receive thread deliver the reply.
                await Task.Delay(8, ct); // ~120 Hz

                var snap = _latestUpdate;
                if (snap is null) continue;

                var decoded = snap.DecodedData;
                float[] shapes = decoded.GetExpressionShapes();
                if (shapes is not null)
                    UpstreamExpressionMap.CopyShapes(shapes, exprSink);

                // Eye data.
                const float kInvalid = unchecked((float)0xFFFFFFFF);
                float leftOpen = decoded.GetEyeLeftOpenness();
                if (leftOpen != kInvalid) eyeSink.LeftOpenness  = leftOpen;
                float rightOpen = decoded.GetEyeRightOpenness();
                if (rightOpen != kInvalid) eyeSink.RightOpenness = rightOpen;

                frameNum++;
                ReadOnlySpan<float> s = exprSink.Values;
                int nonZero = 0;
                foreach (float v in s) if (v != 0f) nonZero++;
                float jawOpen2 = s.Length > 0 ? s[0] : 0f;

                if (nonZero == 0 && leftOpen is 0f or kInvalid)
                {
                    noDataRun++;
                    if (noDataRun % 60 == 0)
                        _logger.Warn($"[host] module={module.Manifest.Name} no-data for {noDataRun} frames");
                }
                else
                {
                    noDataRun = 0;
                }

                if (frameNum % 60 == 0)
                {
                    _logger.Info($"[host] module={module.Manifest.Name} frame={frameNum} " +
                                 $"jawOpen={jawOpen2:F3} leftEyeLid={eyeSink.LeftOpenness:F3} nonZeroShapes={nonZero}/{s.Length}");
                }

                // Head data. The sentinel 0xFFFFFFFF (reinterpreted as float) means "not provided".
                const uint kSentinelBits = 0xFFFFFFFF;
                float headYaw   = decoded.GetHeadYaw();
                float headPitch = decoded.GetHeadPitch();
                float headRoll  = decoded.GetHeadRoll();
                float headPosX  = decoded.GetHeadPosX();
                float headPosY  = decoded.GetHeadPosY();
                float headPosZ  = decoded.GetHeadPosZ();
                // Set head_flags bit 0 when head values are not all sentinel.
                bool headValid =
                    BitConverter.SingleToUInt32Bits(headYaw)   != kSentinelBits ||
                    BitConverter.SingleToUInt32Bits(headPitch) != kSentinelBits ||
                    BitConverter.SingleToUInt32Bits(headRoll)  != kSentinelBits;
                // Replace sentinel values with 0f before writing to shmem.
                if (BitConverter.SingleToUInt32Bits(headYaw)   == kSentinelBits) headYaw   = 0f;
                if (BitConverter.SingleToUInt32Bits(headPitch) == kSentinelBits) headPitch = 0f;
                if (BitConverter.SingleToUInt32Bits(headRoll)  == kSentinelBits) headRoll  = 0f;
                if (BitConverter.SingleToUInt32Bits(headPosX)  == kSentinelBits) headPosX  = 0f;
                if (BitConverter.SingleToUInt32Bits(headPosY)  == kSentinelBits) headPosY  = 0f;
                if (BitConverter.SingleToUInt32Bits(headPosZ)  == kSentinelBits) headPosZ  = 0f;

                await writer.PublishAsync(
                    eyeSink, exprSink,
                    initReply.eyeSuccess, initReply.expressionSuccess,
                    module.UuidHash, ct,
                    headYaw, headPitch, headRoll,
                    headPosX, headPosY, headPosZ,
                    headValid ? 1u : 0u);
            }
        }
        catch (OperationCanceledException)
        {
            // Normal shutdown or timeout.
        }
        catch (Exception ex)
        {
            _logger.Error($"[SubprocessManager] error in run loop: {ex.GetType().Name} {ex.Message}");
        }
        finally
        {
            await TeardownActiveAsync(ct: CancellationToken.None);

            // Circuit-breaker logic.
            var lifetime = DateTime.UtcNow - spawnTime;
            if (lifetime < FastExitThreshold)
            {
                if (_breakerUuid == module.Uuid)
                    _breakerCount++;
                else
                {
                    _breakerUuid  = module.Uuid;
                    _breakerCount = 1;
                }
                _logger.Warn($"[spawn] subprocess crashed fast (lifetime={lifetime.TotalSeconds:F1}s); " +
                             $"attempt {_breakerCount}/{CrashHaltThreshold} for {module.Uuid}");
            }
            else
            {
                // Reset breaker on healthy lifetime.
                if (_breakerUuid == module.Uuid) { _breakerUuid = null; _breakerCount = 0; }
            }
        }
    }

    private async Task TeardownActiveAsync(CancellationToken ct)
    {
        var proc = _activeProcess;
        _activeProcess = null;
        if (proc is null) return;

        if (!proc.Exited && !proc.Process.HasExited)
        {
            _logger.Info($"[teardown] sending EventTeardown to PID={proc.Process.Id}");
            SendToSubprocess(new EventTeardownPacket());

            try
            {
                await _teardownTcs.Task.WaitAsync(TimeSpan.FromSeconds(5), ct);
                _logger.Info("[teardown] ReplyTeardown received");
            }
            catch { _logger.Warn("[teardown] timed out waiting for ReplyTeardown; killing"); }
        }

        try { await proc.Process.WaitForExitAsync(CancellationToken.None).WaitAsync(TimeSpan.FromSeconds(3)); }
        catch { }
        _logger.Info($"[teardown] process exited code={TryGetExitCode(proc.Process)}");
        proc.Dispose();
    }

    private void SendToSubprocess(IpcPacket packet)
    {
        if (_activePort == 0) return;
        _server.SendData(packet, _activePort);
    }

    private static int TryGetExitCode(Process p)
    {
        try { return p.ExitCode; } catch { return -1; }
    }

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
}
