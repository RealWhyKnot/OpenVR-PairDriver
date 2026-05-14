// Periodically writes a host_status.json file the overlay polls to
// populate the Settings/Modules/Advanced tabs with live state.
//
// The host and the overlay are different processes (the driver spawns
// the host; the overlay talks to the driver). Plumbing real-time
// status all the way from host -> driver -> overlay would need a new
// IPC channel and a Protocol.h version bump. For values that update
// at human-readable rates (active module name, installed module list),
// a file the overlay re-reads once a second is plenty: zero protocol
// changes, zero driver-side C++ changes, and the file format can grow
// without breaking on-wire compatibility.
//
// Atomicity: writes to a sibling .tmp file then File.Move with
// overwrite. Readers that race with a write see either the previous
// or the new file, never a partial one. Same trick the host's calib
// flush uses.

using System.Diagnostics;
using System.Text.Json;
using System.Text.Json.Serialization;
using OpenVRPair.FaceModuleHost.Logging;

namespace OpenVRPair.FaceModuleHost.Workers;

public sealed class HostStatusWriter(
    string statusFilePath,
    ModuleLoader loader,
    HostLogger logger,
    HostOptions options)
{
    private readonly Stopwatch _uptime = Stopwatch.StartNew();
    private readonly DateTime  _startedAt = DateTime.UtcNow;
    private readonly int       _pid = Environment.ProcessId;

    public async Task RunAsync(CancellationToken ct)
    {
        // Ensure parent directory exists; the driver creates it on first install
        // but a freshly-extracted zip might not have hit that path yet.
        string? dir = Path.GetDirectoryName(statusFilePath);
        if (!string.IsNullOrEmpty(dir))
        {
            try { Directory.CreateDirectory(dir); }
            catch (Exception ex) { logger.Warn($"HostStatusWriter: could not ensure {dir}: {ex.Message}"); }
        }

        logger.Info($"HostStatusWriter: publishing to {statusFilePath} every 1 s.");

        while (!ct.IsCancellationRequested)
        {
            try
            {
                WriteOnce();
            }
            catch (Exception ex)
            {
                // Don't kill the worker; a transient FS error shouldn't take the host down.
                logger.Warn($"HostStatusWriter: write failed: {ex.Message}");
            }

            try { await Task.Delay(TimeSpan.FromSeconds(1), ct); }
            catch (OperationCanceledException) { break; }
        }

        // Final write so the overlay sees a clean "host shutting down" state if it polls again.
        try
        {
            WriteOnce(shuttingDown: true);
        }
        catch { /* shutdown noise */ }
    }

    private void WriteOnce(bool shuttingDown = false)
    {
        var status = new HostStatus
        {
            SchemaVersion       = 1,
            HostPid             = _pid,
            HostStartedAt       = _startedAt,
            HostUptimeSeconds   = (int)_uptime.Elapsed.TotalSeconds,
            HostShuttingDown    = shuttingDown,
            ActiveModule        = BuildActiveModule(),
            InstalledModules    = ScanInstalledModules(),
        };

        string json = JsonSerializer.Serialize(
            status,
            HostStatusJsonContext.Default.HostStatus);

        string tmp = statusFilePath + ".tmp";
        File.WriteAllText(tmp, json);
        // File.Move with overwrite is atomic on NTFS for replace-with-same-volume.
        File.Move(tmp, statusFilePath, overwrite: true);
    }

    private ActiveModuleStatus? BuildActiveModule()
    {
        var m = loader.Active;
        if (m is null) return null;
        return new ActiveModuleStatus
        {
            Uuid    = m.Manifest.Uuid,
            Name    = m.Manifest.Name,
            Vendor  = m.Manifest.Vendor,
            Version = m.Manifest.Version?.ToString() ?? "",
        };
    }

    private List<InstalledModule> ScanInstalledModules()
    {
        var result = new List<InstalledModule>();
        if (!Directory.Exists(options.ModulesInstallDir)) return result;

        foreach (var uuidDir in Directory.EnumerateDirectories(options.ModulesInstallDir))
        {
            string uuid = Path.GetFileName(uuidDir);
            string? newestVersion = null;
            DateTime newestMtime = DateTime.MinValue;

            foreach (var versionDir in Directory.EnumerateDirectories(uuidDir))
            {
                string version = Path.GetFileName(versionDir);
                DateTime mtime = Directory.GetLastWriteTimeUtc(versionDir);
                if (mtime > newestMtime)
                {
                    newestMtime    = mtime;
                    newestVersion  = version;
                }
            }

            if (newestVersion is null) continue;

            // Pull manifest fields from the per-version manifest.json if present.
            string manifestPath = Path.Combine(uuidDir, newestVersion, "manifest.json");
            string? name = null, vendor = null;
            if (File.Exists(manifestPath))
            {
                try
                {
                    using var doc = JsonDocument.Parse(File.ReadAllText(manifestPath));
                    if (doc.RootElement.TryGetProperty("name", out var n))   name   = n.GetString();
                    if (doc.RootElement.TryGetProperty("vendor", out var v)) vendor = v.GetString();
                }
                catch { /* malformed manifest -- skip the extras, keep the listing */ }
            }

            result.Add(new InstalledModule
            {
                Uuid    = uuid,
                Name    = name   ?? uuid,
                Vendor  = vendor ?? "",
                Version = newestVersion,
            });
        }

        result.Sort(static (a, b) => string.CompareOrdinal(a.Name, b.Name));
        return result;
    }
}

public sealed class HostStatus
{
    [JsonPropertyName("schema_version")]    public int       SchemaVersion     { get; init; }
    [JsonPropertyName("host_pid")]          public int       HostPid           { get; init; }
    [JsonPropertyName("host_started_at")]   public DateTime  HostStartedAt     { get; init; }
    [JsonPropertyName("host_uptime_s")]     public int       HostUptimeSeconds { get; init; }
    [JsonPropertyName("host_shutting_down")] public bool     HostShuttingDown  { get; init; }
    [JsonPropertyName("active_module")]     public ActiveModuleStatus? ActiveModule { get; init; }
    [JsonPropertyName("installed_modules")] public IList<InstalledModule> InstalledModules { get; init; } = [];
}

public sealed class ActiveModuleStatus
{
    [JsonPropertyName("uuid")]    public string Uuid    { get; init; } = "";
    [JsonPropertyName("name")]    public string Name    { get; init; } = "";
    [JsonPropertyName("vendor")]  public string Vendor  { get; init; } = "";
    [JsonPropertyName("version")] public string Version { get; init; } = "";
}

public sealed class InstalledModule
{
    [JsonPropertyName("uuid")]    public string Uuid    { get; init; } = "";
    [JsonPropertyName("name")]    public string Name    { get; init; } = "";
    [JsonPropertyName("vendor")]  public string Vendor  { get; init; } = "";
    [JsonPropertyName("version")] public string Version { get; init; } = "";
}

// Source generator context for AOT-safe JSON serialization. Even though the host
// is currently JIT-only, declaring this up front lets a future AOT publish reuse
// it without surprises.
[JsonSourceGenerationOptions(
    WriteIndented = true,
    DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull)]
[JsonSerializable(typeof(HostStatus))]
internal partial class HostStatusJsonContext : JsonSerializerContext { }
