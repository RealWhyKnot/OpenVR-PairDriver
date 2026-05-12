// Allow the host process assembly to reach internal SDK members (EyeSinkInternal,
// ExpressionSinkInternal) without making them part of the public module API.
[assembly: System.Runtime.CompilerServices.InternalsVisibleTo("OpenVRPair.FaceModuleHost")]

namespace OpenVRPair.FaceTracking.ModuleSdk;

/// <summary>
/// Base class for all hardware face/eye tracking modules loaded by the host.
/// Subclasses implement the hardware-specific initialization and per-frame sampling loop;
/// the host calls <see cref="Update"/> on the hardware-tick thread and reads
/// <see cref="Eye"/> / <see cref="Expression"/> after each call returns.
/// </summary>
public abstract class FaceTrackingModule
{
    /// <summary>Static identity and capability declaration. Must be non-null after construction.</summary>
    public abstract ModuleManifest Manifest { get; }

    /// <summary>Eye data sink. Populated by the module during <see cref="Update"/>.</summary>
    protected EyeFrameSink Eye { get; } = new();

    /// <summary>Expression data sink. Populated by the module during <see cref="Update"/>.</summary>
    protected ExpressionFrameSink Expression { get; } = new();

    // Internal accessors used by the host without exposing write access to consumers.
    internal EyeFrameSink EyeSinkInternal => Eye;
    internal ExpressionFrameSink ExpressionSinkInternal => Expression;

    /// <summary>
    /// Called once by the host before the update loop begins. The host passes which capabilities
    /// SteamVR actually needs so the module can skip unused hardware paths.
    /// </summary>
    /// <param name="eyeAvailable">True if eye tracking output will be consumed.</param>
    /// <param name="expressionAvailable">True if expression output will be consumed.</param>
    /// <returns>A tuple indicating which capabilities this module was actually able to activate.</returns>
    public abstract (bool eye, bool expression) Initialize(bool eyeAvailable, bool expressionAvailable);

    /// <summary>
    /// Called on the hardware-tick thread (~120 Hz). Modules sample their hardware and write
    /// results into <see cref="Eye"/> and <see cref="Expression"/>. Must return promptly;
    /// blocking for more than ~50 ms will trigger a host watchdog warning.
    /// </summary>
    public abstract void Update(in FrameContext ctx);

    /// <summary>
    /// Called when the host detects a device-level event (plug/unplug/reset).
    /// Default implementation does nothing; override to handle hot-plug scenarios.
    /// </summary>
    public virtual void OnHotPlug(DeviceEvent e) { }

    /// <summary>
    /// Called when the module is being unloaded. Must release all hardware handles.
    /// The host will not call <see cref="Update"/> after this returns.
    /// </summary>
    public abstract void Teardown();
}
