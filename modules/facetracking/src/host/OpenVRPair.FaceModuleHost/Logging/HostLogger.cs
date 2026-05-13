using System.Text;

namespace OpenVRPair.FaceModuleHost.Logging;

/// <summary>
/// Minimal logger that appends UTF-8 lines to
/// %LocalAppDataLow%\WKOpenVR\Logs\facetracking_log.&lt;timestamp&gt;.txt.
/// Opened with FileShare.ReadWrite | FileShare.Delete so the overlay's log
/// viewer can tail the file concurrently, and the file can be rotated under us.
/// </summary>
public sealed class HostLogger : IDisposable
{
    private readonly StreamWriter _writer;
    private readonly object       _lock = new();

    public enum Level { Info, Warn, Error }

    public HostLogger(string? logFilePath = null)
    {
        string path = logFilePath ?? DefaultLogPath();
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

    public void Info(string msg)  => Write(Level.Info,  msg);
    public void Warn(string msg)  => Write(Level.Warn,  msg);
    public void Error(string msg) => Write(Level.Error, msg);

    private void Write(Level level, string msg)
    {
        string line = $"{DateTime.UtcNow:yyyy-MM-ddTHH:mm:ss.fffZ} [{level,5}] [FaceHost] {msg}";
        lock (_lock)
        {
            try   { _writer.WriteLine(line); }
            catch { /* log write failure is silently swallowed to avoid infinite recursion */ }
        }
        // Mirror to stderr so the driver's HostSupervisor can capture process output.
        Console.Error.WriteLine(line);
    }

    public static string DefaultLogPath()
    {
        string localLow = LocalAppDataLow();
        string ts       = DateTime.UtcNow.ToString("yyyyMMdd_HHmmss");
        return Path.Combine(localLow, "WKOpenVR", "Logs", $"facetracking_log.{ts}.txt");
    }

    private static string LocalAppDataLow()
    {
        string local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        return local.EndsWith("Local", StringComparison.OrdinalIgnoreCase)
            ? local[..^5] + "LocalLow"
            : local;
    }

    public void Dispose() => _writer.Dispose();
}
