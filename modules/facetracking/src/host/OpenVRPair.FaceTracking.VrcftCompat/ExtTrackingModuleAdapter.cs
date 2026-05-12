// Structural shim that wraps an upstream-style IExtTrackingModuleLegacy as a
// first-class FaceTrackingModule. Only the structural mapping is implemented here;
// porting a specific vendor module requires implementing IExtTrackingModuleLegacy
// for that module and instantiating an adapter around it. The adapter does NOT
// attempt to convert legacy gaze conventions (upstream uses a 2D pitch/yaw pair;
// we need a 3D unit vector) -- callers must patch the gaze conversion in their
// IExtTrackingModuleLegacy.Update() implementation or extend this adapter.

using System.Numerics;
using OpenVRPair.FaceTracking.ModuleSdk;

namespace OpenVRPair.FaceTracking.VrcftCompat;

public sealed class ExtTrackingModuleAdapter(
    IExtTrackingModuleLegacy inner,
    ModuleManifest manifest) : FaceTrackingModule
{
    private readonly LegacyUnifiedTrackingData _scratch = new();

    public override ModuleManifest Manifest { get; } = manifest;

    public override (bool eye, bool expression) Initialize(bool eyeAvailable, bool expressionAvailable)
    {
        bool ok = inner.Initialize(
            eye:         eyeAvailable && inner.Supported.SupportsEye,
            expressions: expressionAvailable && inner.Supported.SupportsExpression);
        return ok
            ? (inner.Supported.SupportsEye, inner.Supported.SupportsExpression)
            : (false, false);
    }

    public override void Update(in FrameContext ctx)
    {
        inner.Update(_scratch);

        // Eye data: upstream reports a 2D gaze (pitch/yaw in radians or normalised -1..1;
        // exact convention is module-dependent). The conversion below treats the pair as
        // normalised sin(pitch)/sin(yaw) and reconstructs a unit vector in OpenVR HMD
        // space (+X right, +Y up, -Z forward). Modules that provide a 3D direction
        // directly should override this method or bypass the adapter entirely.
        CopyEye(_scratch.Eye.Left,  Eye.Left);
        CopyEye(_scratch.Eye.Right, Eye.Right);
        Eye.LeftOpenness         = _scratch.Eye.Left.Openness;
        Eye.RightOpenness        = _scratch.Eye.Right.Openness;
        Eye.PupilDilationLeft    = _scratch.Eye.Left.PupilDilation;
        Eye.PupilDilationRight   = _scratch.Eye.Right.PupilDilation;
        Eye.TimestampHns         = ctx.TimestampHns;

        // Expression shapes: upstream indices map 1:1 to UnifiedExpression v2 if the
        // module was written against VRCFT SDK >=2.0. Modules on older SDKs may have
        // a different shape ordering; verify before shipping a module package.
        var shapes = _scratch.Shapes;
        int count  = Math.Min(shapes.Length, (int)UnifiedExpression.Count);
        for (int i = 0; i < count; i++)
            Expression[(UnifiedExpression)i] = shapes[i];
        Expression.TimestampHns = ctx.TimestampHns;
    }

    public override void Teardown() => inner.Teardown();

    private static void CopyEye(LegacyEyeState src, EyeGaze dst)
    {
        float yaw   = src.Gaze.X;
        float pitch = src.Gaze.Y;
        // Clamp to valid trig domain before producing a unit vector.
        yaw   = Math.Clamp(yaw,   -1f, 1f);
        pitch = Math.Clamp(pitch, -1f, 1f);
        float x  = yaw;
        float y  = pitch;
        float z  = -MathF.Sqrt(Math.Max(0f, 1f - x * x - y * y));
        dst.DirHmd     = Vector3.Normalize(new Vector3(x, y, z));
        dst.Confidence = src.Openness > 0.1f ? 1f : 0f;
        // OriginHmd left at default (0,0,0); the module can set it before returning.
    }
}
