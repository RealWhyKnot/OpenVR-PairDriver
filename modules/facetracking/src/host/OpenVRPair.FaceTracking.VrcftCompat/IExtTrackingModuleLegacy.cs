// Local re-declaration of the upstream VRCFaceTracking ExtTrackingModule surface.
// This mirrors the public contract of VRCFaceTracking.Core.Params.IExtTrackingModule
// as it existed at the time of the fork, without taking a NuGet dependency on
// the upstream SDK. Porting a vendor module is a matter of implementing this
// interface and wrapping it with ExtTrackingModuleAdapter.
//
// Upstream reference: https://github.com/benaclejames/VRCFaceTracking
// Interface stability: upstream does not guarantee binary compat across major
// versions; re-confirm field layout when porting a module written against a
// different upstream SDK version.

namespace OpenVRPair.FaceTracking.VrcftCompat;

/// <summary>
/// Upstream-style unified tracking data snapshot. Only the fields referenced by
/// <see cref="ExtTrackingModuleAdapter"/> are declared here; extend as needed
/// when porting specific modules.
/// </summary>
public sealed class LegacyUnifiedTrackingData
{
    public LegacyEyeData Eye { get; } = new();
    public float[] Shapes { get; } = new float[63];
}

public sealed class LegacyEyeData
{
    public LegacyEyeState Left  { get; } = new();
    public LegacyEyeState Right { get; } = new();
}

public sealed class LegacyEyeState
{
    public System.Numerics.Vector2 Gaze { get; set; }
    public float Openness   { get; set; }
    public float PupilDilation { get; set; }
}

/// <summary>
/// Minimal surface of the upstream <c>ExtTrackingModule</c> abstract class,
/// re-expressed as an interface so the adapter does not require inheritance from
/// a type that lives in an assembly we do not distribute.
/// </summary>
public interface IExtTrackingModuleLegacy
{
    /// <summary>Which capabilities this module supports: (supportsEye, supportsExpression).</summary>
    (bool SupportsEye, bool SupportsExpression) Supported { get; }

    /// <summary>Allocate hardware resources. Returns true on success.</summary>
    bool Initialize(bool eye, bool expressions);

    /// <summary>Sample the hardware; write results into <paramref name="data"/>.</summary>
    void Update(LegacyUnifiedTrackingData data);

    /// <summary>Release hardware resources.</summary>
    void Teardown();
}
