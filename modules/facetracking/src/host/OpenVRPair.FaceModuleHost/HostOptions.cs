namespace OpenVRPair.FaceModuleHost;

/// <summary>
/// Runtime options for the host process. Populated from command-line args and
/// environment variable overrides. All paths default to the standard
/// %LocalAppDataLow%\WKOpenVR tree.
/// </summary>
public sealed class HostOptions
{
    /// <summary>Named pipe the driver uses to send control messages to the host.</summary>
    public string DriverHandshakePipe { get; set; } =
        @"\\.\pipe\OpenVR-FaceTracking.host";

    /// <summary>Name of the shmem ring the host writes into.</summary>
    public string ShmemName { get; set; } =
        "OpenVRPairFaceTrackingFrameRingV1";

    /// <summary>Default OSC target host.</summary>
    public string OscHost { get; set; } = "127.0.0.1";

    /// <summary>Default OSC target port.</summary>
    public int OscPort { get; set; } = 9000;

    /// <summary>Directory where installed hardware modules live, one uuid/version sub-tree each.</summary>
    public string ModulesInstallDir { get; set; } = Path.Combine(
        LocalAppDataLow(), "WKOpenVR", "facetracking", "modules");

    /// <summary>Where the host writes its periodic status JSON for the overlay to poll.</summary>
    public string StatusFilePath { get; set; } = Path.Combine(
        LocalAppDataLow(), "WKOpenVR", "facetracking", "host_status.json");

    public static HostOptions FromArgs(string[] args)
    {
        var opts = new HostOptions();

        // Override from env vars first (lower priority than args).
        if (Environment.GetEnvironmentVariable("OPENVR_PAIR_FACE_PIPE") is { } envPipe)
            opts.DriverHandshakePipe = envPipe;
        if (Environment.GetEnvironmentVariable("OPENVR_PAIR_FACE_SHMEM") is { } envShmem)
            opts.ShmemName = envShmem;
        if (Environment.GetEnvironmentVariable("OPENVR_PAIR_OSC_HOST") is { } envOscHost)
            opts.OscHost = envOscHost;
        if (Environment.GetEnvironmentVariable("OPENVR_PAIR_OSC_PORT") is { } envOscPort
            && int.TryParse(envOscPort, out int parsedPort))
            opts.OscPort = parsedPort;

        // Command-line args override env vars.
        for (int i = 0; i < args.Length - 1; i++)
        {
            switch (args[i])
            {
                case "--driver-handshake-pipe": opts.DriverHandshakePipe = args[i + 1]; break;
                case "--shmem-name":            opts.ShmemName           = args[i + 1]; break;
                case "--osc-host":              opts.OscHost             = args[i + 1]; break;
                case "--osc-port":
                    if (int.TryParse(args[i + 1], out int p)) opts.OscPort = p;
                    break;
                case "--modules-dir":           opts.ModulesInstallDir   = args[i + 1]; break;
            }
        }

        return opts;
    }

    private static string LocalAppDataLow()
    {
        // SHGetKnownFolderPath(FOLDERID_LocalAppDataLow) via the CSIDL trick:
        // %LOCALAPPDATA% is ...\AppData\Local; replace with ...\AppData\LocalLow.
        string local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        return local.EndsWith("Local", StringComparison.OrdinalIgnoreCase)
            ? local[..^5] + "LocalLow"
            : local;
    }
}
