// Vendored from VRCFaceTracking (Apache-2.0) -- namespace VRCFaceTracking.Core preserved verbatim.
using System.Runtime.InteropServices;
using System.Security;

namespace VRCFaceTracking.Core;

/// <summary>
/// Windows-centric utilities class
/// </summary>
public static class Utils
{
    [SuppressUnmanagedCodeSecurity]
    [DllImport("winmm.dll", EntryPoint = "timeBeginPeriod", SetLastError = true)]
    public static extern uint TimeBeginPeriod(uint uMilliseconds);

    [SuppressUnmanagedCodeSecurity]
    [DllImport("winmm.dll", EntryPoint = "timeEndPeriod", SetLastError = true)]
    public static extern uint TimeEndPeriod(uint uMilliseconds);
}
