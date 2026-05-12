using System.Text.Json;
using System.Text.Json.Serialization;

namespace OpenVRPair.FaceTracking.Registry;

/// <summary>
/// Persisted list of trusted Ed25519 publisher public keys.
/// Loaded from <c>%LocalAppDataLow%\OpenVR-Pair\facetracking\trust.json</c>.
/// </summary>
public sealed class TrustStore
{
    private static readonly JsonSerializerOptions JsonOpts = new(JsonSerializerDefaults.Web)
        { WriteIndented = true };

    private readonly string _filePath;
    private readonly Dictionary<string, byte[]> _keys = new(StringComparer.OrdinalIgnoreCase);

    public TrustStore(string? filePath = null)
    {
        _filePath = filePath ?? DefaultPath();
    }

    public static string DefaultPath()
    {
        string root = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData)
            .Replace("Local", "LocalLow", StringComparison.OrdinalIgnoreCase);
        return Path.Combine(root, "OpenVR-Pair", "facetracking", "trust.json");
    }

    public void Load()
    {
        if (!File.Exists(_filePath)) return;
        try
        {
            using var f  = File.OpenRead(_filePath);
            var doc = JsonSerializer.Deserialize<TrustDocument>(f, JsonOpts);
            if (doc?.Keys is null) return;
            foreach (var entry in doc.Keys)
            {
                if (entry.KeyId is null || entry.Ed25519Pub is null) continue;
                _keys[entry.KeyId] = Convert.FromBase64String(entry.Ed25519Pub);
            }
        }
        catch (Exception ex)
        {
            // Trust-store load failure is surfaced to the caller; modules from untrusted
            // publishers will be blocked until the problem is resolved.
            throw new InvalidOperationException(
                $"Failed to load trust store from {_filePath}: {ex.Message}", ex);
        }
    }

    public bool TryGetPublicKey(string keyId, out byte[] publicKey)
    {
        if (_keys.TryGetValue(keyId, out publicKey!))
            return true;
        publicKey = [];
        return false;
    }

    public void AddTrust(string keyId, byte[] publicKey, bool persist)
    {
        _keys[keyId] = publicKey;
        if (persist) Persist();
    }

    private void Persist()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(_filePath)!);
        var doc = new TrustDocument
        {
            Keys = _keys.Select(kv => new TrustEntry
            {
                KeyId       = kv.Key,
                Ed25519Pub  = Convert.ToBase64String(kv.Value),
            }).ToArray(),
        };
        using var f = File.Open(_filePath, FileMode.Create, FileAccess.Write, FileShare.None);
        JsonSerializer.Serialize(f, doc, JsonOpts);
    }

    private sealed class TrustDocument
    {
        [JsonPropertyName("keys")] public TrustEntry[]? Keys { get; set; }
    }

    private sealed class TrustEntry
    {
        [JsonPropertyName("key_id")]      public string? KeyId { get; set; }
        [JsonPropertyName("ed25519_pub")] public string? Ed25519Pub { get; set; }
    }
}
