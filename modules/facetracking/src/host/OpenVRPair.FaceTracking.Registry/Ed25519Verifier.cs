// NSec.Cryptography wraps libsodium for Ed25519 sign/verify.
// This dependency can be replaced with a built-in BCL API once
// System.Security.Cryptography.Ed25519 reaches a stable public surface in a
// future .NET SDK release. Track: https://github.com/dotnet/runtime/issues/14741

using NSec.Cryptography;

namespace OpenVRPair.FaceTracking.Registry;

/// <summary>
/// Verifies that a module payload's SHA-256 digest was signed by a known publisher key.
/// </summary>
public static class Ed25519Verifier
{
    private static readonly SignatureAlgorithm Algorithm = SignatureAlgorithm.Ed25519;

    /// <summary>
    /// Returns true when <paramref name="signature"/> is a valid Ed25519 signature
    /// over <paramref name="message"/> using <paramref name="publicKeyBytes"/>.
    /// </summary>
    public static bool Verify(byte[] publicKeyBytes, ReadOnlySpan<byte> message, ReadOnlySpan<byte> signature)
    {
        if (publicKeyBytes.Length != 32)
            throw new ArgumentException("Ed25519 public key must be 32 bytes.", nameof(publicKeyBytes));
        if (signature.Length != 64)
            return false;

        try
        {
            var key = NSec.Cryptography.PublicKey.Import(
                Algorithm,
                publicKeyBytes,
                KeyBlobFormat.RawPublicKey);
            return Algorithm.Verify(key, message, signature);
        }
        catch
        {
            return false;
        }
    }

    /// <summary>
    /// Convenience overload: verifies that <paramref name="payloadSha256Hex"/> (the hex-encoded
    /// SHA-256 of the downloaded payload) matches the signed digest.
    /// </summary>
    public static bool VerifyPayloadHash(
        byte[] publicKeyBytes, string payloadSha256Hex, ReadOnlySpan<byte> signature)
    {
        byte[] digest = Convert.FromHexString(payloadSha256Hex);
        return Verify(publicKeyBytes, digest, signature);
    }

    /// <summary>
    /// Verifies a module payload hash using the key identified in the manifest's
    /// <c>signed_by</c> field, looked up from <paramref name="trustStore"/>.
    /// Returns false if the key ID is unknown or the signature is invalid.
    /// </summary>
    public static bool VerifyPayloadHash(
        Manifest manifest, ReadOnlySpan<byte> signature, TrustStore trustStore)
    {
        if (!trustStore.TryGetPublicKey(manifest.SignedBy, out byte[] publicKey))
            return false;
        return VerifyPayloadHash(publicKey, manifest.PayloadSha256, signature);
    }
}
