// ----------------------------------------------------------------------------
// Vendored from VRCFaceTracking (Apache-2.0), HEAVILY MODIFIED.
// Original: https://github.com/benaclejames/VRCFaceTracking/blob/35857c01315c32e0e45dcde2f6f8fe495216fa0c/
//   VRCFaceTracking.Core/UnifiedTracking.cs
// Copyright (c) benaclejames and contributors. Licensed under Apache 2.0.
//
// Modifications: stripped to ONLY the static UnifiedTrackingData Data field
// and EyeImageData/LipImageData stubs. The host process does not need
// AllParameters_v1/_v2, Mutator, OSC machinery, or UpdateData() -- modules
// write Data via module.Update() inside the subprocess, and the subprocess
// snapshots it into ReplyUpdatePacket.
// ----------------------------------------------------------------------------
using VRCFaceTracking.Core.Params.Data;
using VRCFaceTracking.Core.Types;

namespace VRCFaceTracking;

public class UnifiedTracking
{
    /// <summary>
    /// Latest Expression Data accessible and sent by all VRCFaceTracking modules.
    /// </summary>
    public static UnifiedTrackingData Data = new();

    // Stubs for things upstream modules might reference but we don't use:
    public static object EyeImageData = new();
    public static object LipImageData = new();
}
