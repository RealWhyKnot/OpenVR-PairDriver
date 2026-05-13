// Configuration record loaded from bridge.json alongside the adapter DLL.
// The upstream-import packager (wkvrcft-legacy-registry/scripts/import-upstream.py)
// emits one of these per packaged module so the reflection bridge knows
// which upstream type to instantiate and how to label the resulting
// FaceTrackingModule. Per-module C# code is not required at registration
// time -- the script + this config replace what would otherwise be a
// hand-written IExtTrackingModuleLegacy implementation.

using System.Text.Json.Serialization;

namespace OpenVRPair.FaceTracking.VrcftCompat;

internal sealed class BridgeConfig
{
    // Manifest fields surfaced to the host's overlay UI.
    [JsonPropertyName("uuid")]            public required string Uuid           { get; init; }
    [JsonPropertyName("name")]            public required string Name           { get; init; }
    [JsonPropertyName("vendor")]          public required string Vendor         { get; init; }
    [JsonPropertyName("version")]         public required string Version        { get; init; }
    [JsonPropertyName("supported_hmds")]  public required IList<string> SupportedHmds { get; init; }
    [JsonPropertyName("capabilities")]    public required IList<string> Capabilities  { get; init; }

    // Reflection targets. UpstreamAssembly is a filename relative to the
    // adapter DLL's directory; UpstreamType is the fully-qualified class
    // name to instantiate inside that assembly.
    [JsonPropertyName("upstream_assembly")] public required string UpstreamAssembly { get; init; }
    [JsonPropertyName("upstream_type")]     public required string UpstreamType     { get; init; }
}
