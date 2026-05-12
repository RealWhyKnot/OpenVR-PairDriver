namespace OpenVRPair.FaceTracking.ModuleSdk;

[Flags]
public enum Capabilities
{
    None       = 0,
    Eye        = 1 << 0,
    Expression = 1 << 1,
}

/// <summary>
/// Static identity and capability declaration for a hardware tracking module.
/// Populated from the module's manifest.json at load time.
/// </summary>
public sealed class ModuleManifest
{
    public required string Uuid { get; init; }
    public required string Name { get; init; }
    public required string Vendor { get; init; }
    public required Version Version { get; init; }
    public required string[] SupportedHmds { get; init; }
    public required Capabilities Capabilities { get; init; }
}
