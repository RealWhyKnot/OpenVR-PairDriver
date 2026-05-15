// Vendored from VRCFaceTracking (Apache-2.0) -- namespace VRCFaceTracking.Core.Library preserved verbatim.
// REFERENCE ONLY: SubprocessManager replicates this; do not instantiate.
using System.Diagnostics;
using System.Runtime.Loader;
using VRCFaceTracking.Core.Sandboxing;
using VRCFaceTracking.Core.Sandboxing.IPC;

namespace VRCFaceTracking.Core.Library;

public class ModuleRuntimeInfo
{
    public ExtTrackingModule Module;
    public AssemblyLoadContext AssemblyLoadContext;
    public CancellationTokenSource UpdateCancellationToken;
    public Thread UpdateThread;

    public bool IsActive;
    public int SandboxProcessPort;
    public int SandboxProcessPID;
    public string SandboxModulePath;
    public Process Process;
    public ModuleMetadataInternal ModuleInformation;
    public ModuleState Status = ModuleState.Uninitialized;
    public string ModuleClassName;

    public bool SupportsEyeTracking;
    public bool SupportsExpressionTracking;

    public Queue<QueuedPacket> EventBus;
}

public struct QueuedPacket
{
    public IpcPacket packet;
    public int destinationPort;
}
