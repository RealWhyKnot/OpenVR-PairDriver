using System.Text;

namespace OpenVRPair.FaceModuleHost.Logging;

/// <summary>
/// Minimal logger that appends UTF-8 lines to
/// %LocalAppDataLow%\WKOpenVR\Logs\facetracking_log.&lt;timestamp&gt;.txt.
/// Opened with FileShare.ReadWrite | FileShare.Delete so the overlay's log
/// viewer can tail the file concurrently, and the file can be rotated under us.
/// The file is opened lazily only while shared debug logging is enabled.
/// </summary>
public sealed class HostLogger : IDisposable
{
    private readonly string? _explicitLogFilePath;
    private readonly bool    _forceEnabled;
    private readonly object  _lock = new();

    private StreamWriter? _writer;
    private DateTime      _lastEnabledCheckUtc = DateTime.MinValue;
    private bool          _cachedEnabled;

    public enum Level { Info, Warn, Error }

    public HostLogger(string? logFilePath = null, bool forceEnabled = false)
    {
        _explicitLogFilePath = logFilePath;
        _forceEnabled = forceEnabled || logFilePath is not null;
    }

    public void Info(string msg)  => Write(Level.Info,  msg);
    public void Warn(string msg)  => Write(Level.Warn,  msg);
    public void Error(string msg) => Write(Level.Error, msg);

    public void Flush()
    {
        lock (_lock)
        {
            try { _writer?.Flush(); } catch { }
        }
    }

    private void Write(Level level, string msg)
    {
        if (!IsEnabled()) return;

        string line = $"{DateTime.UtcNow:yyyy-MM-ddTHH:mm:ss.fffZ} [{level,5}] [FaceHost] {msg}";
        lock (_lock)
        {
            try
            {
                EnsureOpen();
                _writer?.WriteLine(line);
            }
            catch { /* log write failure is silently swallowed to avoid infinite recursion */ }
        }
        // Mirror to stderr so the driver's HostSupervisor can capture process output.
        Console.Error.WriteLine(line);
    }

    private bool IsEnabled()
    {
        if (_forceEnabled) return true;

        DateTime now = DateTime.UtcNow;
        if ((now - _lastEnabledCheckUtc).TotalSeconds < 1)
            return _cachedEnabled;

        _cachedEnabled = File.Exists(DebugFlagPath());
        _lastEnabledCheckUtc = now;
        return _cachedEnabled;
    }

    private void EnsureOpen()
    {
        if (_writer is not null) return;

        string path = _explicitLogFilePath ?? DefaultLogPath();
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var fs = new FileStream(
            path,
            FileMode.Append,
            FileAccess.Write,
            FileShare.ReadWrite | FileShare.Delete,
            bufferSize: 4096,
            useAsync: false);
        _writer = new StreamWriter(fs, Encoding.UTF8, leaveOpen: false) { AutoFlush = true };
    }

    public static string DefaultLogPath()
    {
        string localLow = LocalAppDataLow();
        string ts       = DateTime.UtcNow.ToString("yyyyMMdd_HHmmss");
        return Path.Combine(localLow, "WKOpenVR", "Logs", $"facetracking_log.{ts}.txt");
    }

    private static string DebugFlagPath() =>
        Path.Combine(LocalAppDataLow(), "WKOpenVR", "debug_logging.enabled");

    private static string LocalAppDataLow()
    {
        string local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        return local.EndsWith("Local", StringComparison.OrdinalIgnoreCase)
            ? local[..^5] + "LocalLow"
            : local;
    }

    public void Dispose() => _writer?.Dispose();
}
