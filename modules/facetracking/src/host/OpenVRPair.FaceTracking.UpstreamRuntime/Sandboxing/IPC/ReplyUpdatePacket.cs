// ----------------------------------------------------------------------------
// Vendored from VRCFaceTracking (Apache-2.0).
// Original: https://github.com/benaclejames/VRCFaceTracking/blob/35857c01315c32e0e45dcde2f6f8fe495216fa0c/
//   VRCFaceTracking.Core/Sandboxing/IPC/ReplyUpdatePacket.cs
// Copyright (c) benaclejames and contributors. Licensed under Apache 2.0.
//
// DELIBERATE DIVERGENCE: UpdateDataContiguous is changed from internal to public,
// and a public DecodedData property accessor is added. Upstream's host reads decoded
// data via the UnifiedTracking.Data static singleton (populated inside ModuleProcess).
// Our host never runs modules in-process, so it reads the decoded snapshot directly
// off the packet object via DecodedData. One visibility change; no logic changes.
// ----------------------------------------------------------------------------
using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using VRCFaceTracking.Core.Params.Data;
using VRCFaceTracking.Core.Params.Expressions;

namespace VRCFaceTracking.Core.Sandboxing.IPC;
public class ReplyUpdatePacket : IpcPacket
{
    const int EXPRESSION_COUNT = (int)UnifiedExpressions.Max + 1;
    const float INVALID_FLOAT = 0xFFFFFFFF;

    [StructLayout(LayoutKind.Sequential)]
    public class UpdateDataContiguous
    {
        internal float Eye_MaxDilation;
        internal float Eye_MinDilation;

        internal float Eye_Left_GazeX;
        internal float Eye_Left_GazeY;
        internal float Eye_Left_PupilDiameter_MM;
        internal float Eye_Left_Openness;

        internal float Eye_Right_GazeX;
        internal float Eye_Right_GazeY;
        internal float Eye_Right_PupilDiameter_MM;
        internal float Eye_Right_Openness;

        internal float Head_Yaw;
        internal float Head_Pitch;
        internal float Head_Roll;

        internal float Head_PosX;
        internal float Head_PosY;
        internal float Head_PosZ;

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = EXPRESSION_COUNT)]
        internal float[] Expression_Shapes;

        // Accessors used by SubprocessManager to read eye/head/expression data without
        // populating the UnifiedTracking.Data static (which only exists inside the subprocess).
        public float GetEyeLeftGazeX() => Eye_Left_GazeX;
        public float GetEyeLeftGazeY() => Eye_Left_GazeY;
        public float GetEyeLeftOpenness() => Eye_Left_Openness;
        public float GetEyeLeftPupilMM() => Eye_Left_PupilDiameter_MM;
        public float GetEyeRightGazeX() => Eye_Right_GazeX;
        public float GetEyeRightGazeY() => Eye_Right_GazeY;
        public float GetEyeRightOpenness() => Eye_Right_Openness;
        public float GetEyeRightPupilMM() => Eye_Right_PupilDiameter_MM;
        public float GetEyeMaxDilation() => Eye_MaxDilation;
        public float GetEyeMinDilation() => Eye_MinDilation;
        public float GetHeadYaw() => Head_Yaw;
        public float GetHeadPitch() => Head_Pitch;
        public float GetHeadRoll() => Head_Roll;
        public float GetHeadPosX() => Head_PosX;
        public float GetHeadPosY() => Head_PosY;
        public float GetHeadPosZ() => Head_PosZ;
        public float[] GetExpressionShapes() => Expression_Shapes;
    }

    private UpdateDataContiguous _contiguousUnifiedData = new ()
    {
        Expression_Shapes = new float[EXPRESSION_COUNT]
    };

    /// <summary>
    /// Decoded snapshot of the subprocess's UnifiedTracking.Data at update time.
    /// Used by SubprocessManager on the host side to read data without a local static.
    /// </summary>
    public UpdateDataContiguous DecodedData => _contiguousUnifiedData;

    public override PacketType GetPacketType() => PacketType.ReplyUpdate;

    public override byte[] GetBytes()
    {
        byte[] packetTypeBytes = BitConverter.GetBytes((uint)GetPacketType());

        int packetSize = SIZE_PACKET_MAGIC + SIZE_PACKET_TYPE;

        _contiguousUnifiedData.Eye_Left_GazeX               = UnifiedTracking.Data.Eye.Left.Gaze.x;
        _contiguousUnifiedData.Eye_Left_GazeY               = UnifiedTracking.Data.Eye.Left.Gaze.y;
        _contiguousUnifiedData.Eye_Left_PupilDiameter_MM    = UnifiedTracking.Data.Eye.Left.PupilDiameter_MM;
        _contiguousUnifiedData.Eye_Left_Openness            = UnifiedTracking.Data.Eye.Left.Openness;

        _contiguousUnifiedData.Eye_Right_GazeX              = UnifiedTracking.Data.Eye.Right.Gaze.x;
        _contiguousUnifiedData.Eye_Right_GazeY              = UnifiedTracking.Data.Eye.Right.Gaze.y;
        _contiguousUnifiedData.Eye_Right_PupilDiameter_MM   = UnifiedTracking.Data.Eye.Right.PupilDiameter_MM;
        _contiguousUnifiedData.Eye_Right_Openness           = UnifiedTracking.Data.Eye.Right.Openness;

        _contiguousUnifiedData.Eye_MaxDilation              = UnifiedTracking.Data.Eye._maxDilation;
        _contiguousUnifiedData.Eye_MinDilation              = UnifiedTracking.Data.Eye._minDilation;

        _contiguousUnifiedData.Head_Yaw                     = UnifiedTracking.Data.Head.HeadYaw;
        _contiguousUnifiedData.Head_Pitch                   = UnifiedTracking.Data.Head.HeadPitch;
        _contiguousUnifiedData.Head_Roll                    = UnifiedTracking.Data.Head.HeadRoll;

        _contiguousUnifiedData.Head_PosX                    = UnifiedTracking.Data.Head.HeadPosX;
        _contiguousUnifiedData.Head_PosY                    = UnifiedTracking.Data.Head.HeadPosY;
        _contiguousUnifiedData.Head_PosZ                    = UnifiedTracking.Data.Head.HeadPosZ;

        for ( int i = 0; i < _contiguousUnifiedData.Expression_Shapes.Length; i++ )
        {
            _contiguousUnifiedData.Expression_Shapes[i] = UnifiedTracking.Data.Shapes[i].Weight;
        }

        int sizeStruct = Marshal.SizeOf<UpdateDataContiguous>();
        byte[] sizeStructBytes = BitConverter.GetBytes(sizeStruct);
        byte[] arr = new byte[sizeStruct];

        var ptr = IntPtr.Zero;
        try
        {
            ptr = Marshal.AllocHGlobal(sizeStruct);
            Marshal.StructureToPtr(_contiguousUnifiedData, ptr, false);
            Marshal.Copy(ptr, arr, 0, sizeStruct);
        }
        finally
        {
            Marshal.FreeHGlobal(ptr);
        }

        packetSize = packetSize + sizeof(int) + sizeStruct;

        byte[] finalDataStream = new byte[packetSize];
        Buffer.BlockCopy(HANDSHAKE_MAGIC,   0, finalDataStream, 0,  SIZE_PACKET_MAGIC);
        Buffer.BlockCopy(packetTypeBytes,   0, finalDataStream, 4,  SIZE_PACKET_TYPE);
        Buffer.BlockCopy(sizeStructBytes,   0, finalDataStream, 8,  sizeof(int));
        Buffer.BlockCopy(arr,               0, finalDataStream, 12, sizeStruct);

        return finalDataStream;
    }

    public override void Decode(in byte[] data)
    {
        int structSize = BitConverter.ToInt32(data, 8);

        var ptr = IntPtr.Zero;
        try
        {
            ptr = Marshal.AllocHGlobal(structSize);
            Marshal.Copy(data, 12, ptr, structSize);
            Marshal.PtrToStructure<UpdateDataContiguous>(ptr, _contiguousUnifiedData);
        }
        finally
        {
            Marshal.FreeHGlobal(ptr);
        }
    }

    public void UpdateGlobalEyeState()
    {
        if ( _contiguousUnifiedData.Eye_MaxDilation != INVALID_FLOAT &&
            _contiguousUnifiedData.Eye_MinDilation != INVALID_FLOAT &&
            _contiguousUnifiedData.Eye_MaxDilation < _contiguousUnifiedData.Eye_MinDilation )
        {
            return;
        }

        if ( _contiguousUnifiedData.Eye_Left_GazeX != INVALID_FLOAT )
            UnifiedTracking.Data.Eye.Left.Gaze.x = _contiguousUnifiedData.Eye_Left_GazeX;
        if ( _contiguousUnifiedData.Eye_Left_GazeY != INVALID_FLOAT )
            UnifiedTracking.Data.Eye.Left.Gaze.y = _contiguousUnifiedData.Eye_Left_GazeY;
        if ( _contiguousUnifiedData.Eye_Left_PupilDiameter_MM != INVALID_FLOAT )
            UnifiedTracking.Data.Eye.Left.PupilDiameter_MM = _contiguousUnifiedData.Eye_Left_PupilDiameter_MM;
        if ( _contiguousUnifiedData.Eye_Left_Openness != INVALID_FLOAT )
            UnifiedTracking.Data.Eye.Left.Openness = _contiguousUnifiedData.Eye_Left_Openness;

        if ( _contiguousUnifiedData.Eye_Right_GazeX != INVALID_FLOAT )
            UnifiedTracking.Data.Eye.Right.Gaze.x = _contiguousUnifiedData.Eye_Right_GazeX;
        if ( _contiguousUnifiedData.Eye_Right_GazeY != INVALID_FLOAT )
            UnifiedTracking.Data.Eye.Right.Gaze.y = _contiguousUnifiedData.Eye_Right_GazeY;
        if ( _contiguousUnifiedData.Eye_Right_PupilDiameter_MM != INVALID_FLOAT )
            UnifiedTracking.Data.Eye.Right.PupilDiameter_MM = _contiguousUnifiedData.Eye_Right_PupilDiameter_MM;
        if ( _contiguousUnifiedData.Eye_Right_Openness != INVALID_FLOAT )
            UnifiedTracking.Data.Eye.Right.Openness = _contiguousUnifiedData.Eye_Right_Openness;

        if ( _contiguousUnifiedData.Eye_MaxDilation != INVALID_FLOAT )
            UnifiedTracking.Data.Eye._maxDilation = _contiguousUnifiedData.Eye_MaxDilation;
        if ( _contiguousUnifiedData.Eye_MinDilation != INVALID_FLOAT )
            UnifiedTracking.Data.Eye._minDilation = _contiguousUnifiedData.Eye_MinDilation;

        for ( int i = (int)UnifiedExpressions.EyeSquintRight; i <= (int)UnifiedExpressions.BrowOuterUpLeft; i++ )
        {
            if ( _contiguousUnifiedData.Expression_Shapes[i] != INVALID_FLOAT )
                UnifiedTracking.Data.Shapes[i].Weight = _contiguousUnifiedData.Expression_Shapes[i];
        }
    }

    public void UpdateHeadState()
    {
        if ( _contiguousUnifiedData.Head_Yaw != INVALID_FLOAT )
            UnifiedTracking.Data.Head.HeadYaw = _contiguousUnifiedData.Head_Yaw;
        if ( _contiguousUnifiedData.Head_Pitch != INVALID_FLOAT )
            UnifiedTracking.Data.Head.HeadPitch = _contiguousUnifiedData.Head_Pitch;
        if ( _contiguousUnifiedData.Head_Roll != INVALID_FLOAT )
            UnifiedTracking.Data.Head.HeadRoll = _contiguousUnifiedData.Head_Roll;

        if ( _contiguousUnifiedData.Head_PosX != INVALID_FLOAT )
            UnifiedTracking.Data.Head.HeadPosX = _contiguousUnifiedData.Head_PosX;
        if ( _contiguousUnifiedData.Head_PosY != INVALID_FLOAT )
            UnifiedTracking.Data.Head.HeadPosY = _contiguousUnifiedData.Head_PosY;
        if ( _contiguousUnifiedData.Head_PosZ != INVALID_FLOAT )
            UnifiedTracking.Data.Head.HeadPosZ = _contiguousUnifiedData.Head_PosZ;
    }

    public void UpdateGlobalExpressionState()
    {
        for ( int i = (int)UnifiedExpressions.BrowOuterUpLeft; i < _contiguousUnifiedData.Expression_Shapes.Length; i++ )
        {
            if ( _contiguousUnifiedData.Expression_Shapes[i] != INVALID_FLOAT)
                UnifiedTracking.Data.Shapes[i].Weight = _contiguousUnifiedData.Expression_Shapes[i];
        }
    }
}
