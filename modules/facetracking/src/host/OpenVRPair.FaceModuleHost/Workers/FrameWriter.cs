using System.IO.MemoryMappedFiles;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using OpenVRPair.FaceModuleHost.Logging;
using OpenVRPair.FaceTracking.ModuleSdk;
using UnsafeHelper = System.Runtime.CompilerServices.Unsafe;

namespace OpenVRPair.FaceModuleHost.Workers;

// ---------------------------------------------------------------------------
// Shmem layout mirrors Protocol.h FaceTrackingFrameShmem::ShmemData exactly.
// Pack=8 matches MSVC default for x64. Any structural change here must be
// reflected in Protocol.h (and vice versa) or the driver will read garbage.
// ---------------------------------------------------------------------------

[InlineArray(3)]
internal struct Float3 { private float _e0; }

[InlineArray(63)]
internal struct Float63 { private float _e0; }

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal struct FaceTrackingFrameBodyNative
{
    public ulong   qpc_sample_time;
    public ulong   source_module_uuid_hash;
    public Float3  eye_origin_l;
    public Float3  eye_origin_r;
    public Float3  eye_gaze_l;
    public Float3  eye_gaze_r;
    public float   eye_openness_l;
    public float   eye_openness_r;
    public float   pupil_dilation_l;
    public float   pupil_dilation_r;
    public float   eye_confidence_l;
    public float   eye_confidence_r;
    public Float63 expressions;
    public uint    flags;

    // v2 head pose fields. Written as zero until the host can supply head data
    // (SubprocessManager will populate these when head tracking is wired).
    // head_flags bit 0: head pose fields are valid this frame.
    public float   head_yaw;
    public float   head_pitch;
    public float   head_roll;
    public float   head_pos_x;
    public float   head_pos_y;
    public float   head_pos_z;
    public uint    head_flags;
}

// Per-slot seqlock layout. generation precedes body; offset validated by static_assert below.
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal struct FaceTrackingFrameSlotNative
{
    public ulong generation;    // seqlock: odd = mid-write, even = stable
    public FaceTrackingFrameBodyNative body;
}

/// <summary>
/// Publishes face/eye frames into the named shmem ring created by the driver.
/// The driver creates the segment; the host opens it for write. The seqlock
/// discipline matches the C++ Publish() helper in Protocol.h.
/// </summary>
public sealed class FrameWriter(string shmemName, HostLogger logger) : IDisposable
{
    private const uint  Magic        = 0x46544652u; // 'FTFR'
    private const uint  ShmemVersion = 2; // v2: added head_yaw/pitch/roll/pos_x/y/z/head_flags
    private const int   RingSize     = 32;

    // Byte offset of publish_index within ShmemData.
    // C++ ShmemData layout (x64 MSVC defaults, Pack=8):
    //   uint32_t magic            @ 0
    //   uint32_t shmem_version    @ 4
    //   uint32_t ring_size        @ 8
    //   uint32_t _reserved_header[5] @ 12..31  (5 * 4 = 20 bytes)
    //   std::atomic<uint64_t> publish_index @ 32  (8-byte aligned, after 32 bytes)
    private const int   PublishIndexOffset = 32;

    // Byte offset of the first FaceTrackingFrameSlot.
    private const int   SlotsOffset        = PublishIndexOffset + 8; // 40

    private static readonly int SlotSize = UnsafeHelper.SizeOf<FaceTrackingFrameSlotNative>();
    private static readonly int BodyOffsetInSlot =
        (int)Marshal.OffsetOf<FaceTrackingFrameSlotNative>(nameof(FaceTrackingFrameSlotNative.body));

    private MemoryMappedFile?         _mmf;
    private MemoryMappedViewAccessor? _view;
    private long                      _localPublishIndex;

    // Pinned base pointer acquired once at open, valid until Dispose.
    // All seqlock generation and publish_index reads/writes go through this
    // pointer so Volatile.Read/Write can give us acquire/release semantics on
    // x64 without the overhead of _view.ReadUInt64 / _view.Write.
    private unsafe byte* _basePtr;
    private bool         _ptrAcquired;

    public async Task OpenAsync(CancellationToken ct)
    {
        await Task.Run(() =>
        {
            int totalSize = SlotsOffset + RingSize * SlotSize; // 40 + 32 * sizeof(slot)
            _mmf  = MemoryMappedFile.OpenExisting(
                shmemName,
                MemoryMappedFileRights.ReadWrite);
            _view = _mmf.CreateViewAccessor(0, totalSize, MemoryMappedFileAccess.ReadWrite);

            // Validate header using the managed accessor (fine for one-time reads).
            uint magic   = _view.ReadUInt32(0);
            uint version = _view.ReadUInt32(4);
            if (magic != Magic)
                throw new InvalidOperationException(
                    $"Shmem magic mismatch: got 0x{magic:X8}, expected 0x{Magic:X8}");
            if (version != ShmemVersion)
                throw new InvalidOperationException(
                    $"Shmem version mismatch: got {version}, expected {ShmemVersion}");

            // Acquire the raw base pointer for seqlock generation / publish_index fields.
            // AcquirePointer increments the SafeHandle refcount; released in Dispose.
            unsafe
            {
                byte* p = null;
                _view.SafeMemoryMappedViewHandle.AcquirePointer(ref p);
                _basePtr     = p;
                _ptrAcquired = true;
            }

            logger.Info($"FrameWriter: opened '{shmemName}' ({totalSize} bytes). Magic OK.");
        }, ct);
    }

    /// <summary>
    /// Assembles a frame from the active module's sinks and publishes it under seqlock.
    /// The <paramref name="moduleUuidHash"/> must be pre-computed (FNV-1a-64 of the UUID)
    /// by the caller and cached -- do not recompute per frame.
    /// Must be called from a single producer thread.
    /// </summary>
    public ValueTask PublishAsync(
        EyeFrameSink eye,
        ExpressionFrameSink expr,
        bool eyeValid,
        bool exprValid,
        ulong moduleUuidHash,
        CancellationToken ct)
    {
        if (_view is null) return ValueTask.CompletedTask;

        ulong qpc = (ulong)System.Diagnostics.Stopwatch.GetTimestamp();

        var body = new FaceTrackingFrameBodyNative
        {
            qpc_sample_time         = qpc,
            source_module_uuid_hash = moduleUuidHash,
            eye_openness_l          = eye.LeftOpenness,
            eye_openness_r          = eye.RightOpenness,
            pupil_dilation_l        = eye.PupilDilationLeft,
            pupil_dilation_r        = eye.PupilDilationRight,
            eye_confidence_l        = eye.Left.Confidence,
            eye_confidence_r        = eye.Right.Confidence,
            flags                   = (eyeValid ? 1u : 0u) | (exprValid ? 2u : 0u),
        };

        body.eye_origin_l[0] = eye.Left.OriginHmd.X;
        body.eye_origin_l[1] = eye.Left.OriginHmd.Y;
        body.eye_origin_l[2] = eye.Left.OriginHmd.Z;
        body.eye_origin_r[0] = eye.Right.OriginHmd.X;
        body.eye_origin_r[1] = eye.Right.OriginHmd.Y;
        body.eye_origin_r[2] = eye.Right.OriginHmd.Z;
        body.eye_gaze_l[0]   = eye.Left.DirHmd.X;
        body.eye_gaze_l[1]   = eye.Left.DirHmd.Y;
        body.eye_gaze_l[2]   = eye.Left.DirHmd.Z;
        body.eye_gaze_r[0]   = eye.Right.DirHmd.X;
        body.eye_gaze_r[1]   = eye.Right.DirHmd.Y;
        body.eye_gaze_r[2]   = eye.Right.DirHmd.Z;

        ReadOnlySpan<float> shapes = expr.Values;
        int count = Math.Min(shapes.Length, 63);
        for (int i = 0; i < count; i++)
            body.expressions[i] = shapes[i];

        WriteUnderSeqlock(ref body);
        return ValueTask.CompletedTask;
    }

    private unsafe void WriteUnderSeqlock(ref FaceTrackingFrameBodyNative body)
    {
        long next    = Interlocked.Increment(ref _localPublishIndex);
        int  slotIdx = (int)((next - 1) % RingSize);

        int  slotOffset = SlotsOffset + slotIdx * SlotSize;
        long genOffset  = slotOffset;
        int  bodyOffset = slotOffset + BodyOffsetInSlot;

        long* genPtr = (long*)(_basePtr + genOffset);

        // Read generation, mark odd (mid-write), copy body, mark even (complete).
        // Volatile.Read/Write give acquire/release semantics matching C++ atomic<uint64_t>.
        long prevGen = Volatile.Read(ref *genPtr);

        Volatile.Write(ref *genPtr, prevGen + 1L);

        _view!.Write(bodyOffset, ref body);
        Thread.MemoryBarrier();

        Volatile.Write(ref *genPtr, prevGen + 2L);

        // Publish the new slot index so the driver's reader picks it up.
        long* publishPtr = (long*)(_basePtr + PublishIndexOffset);
        Volatile.Write(ref *publishPtr, Interlocked.Read(ref _localPublishIndex));
    }

    public unsafe void Dispose()
    {
        if (_ptrAcquired)
        {
            _view?.SafeMemoryMappedViewHandle.ReleasePointer();
            _ptrAcquired = false;
            _basePtr     = null;
        }
        _view?.Dispose();
        _mmf?.Dispose();
        _view = null;
        _mmf  = null;
    }
}
