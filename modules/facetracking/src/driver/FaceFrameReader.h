#pragma once

#include "Protocol.h"

#include <cstdint>

namespace facetracking {

// Thin wrapper around FaceTrackingFrameShmem.
// The driver opens the segment on Init() and polls TryRead() on its worker
// thread.  Thread-safe: Open/Close are called only from the Init/Shutdown
// path while the worker is not running; TryRead is called only from the
// worker thread.
class FaceFrameReader
{
public:
    FaceFrameReader();
    ~FaceFrameReader();

    // Create-or-attach the named shmem segment as the writer-side owner.
    // The driver calls this at Init so the segment exists before the C# host
    // opens it; the host process uses OpenExisting and writes into the same
    // backing store. Returns true on success. Logs on failure.
    bool Create(LPCSTR name);

    // Open an existing shmem segment as a read-only consumer. Used for test
    // harnesses and any future first-party reader; the driver itself uses
    // Create above so it owns segment lifetime.
    void Open(LPCSTR name);

    // Close the shmem segment.
    void Close();

    // True if Open() succeeded and the segment is currently mapped.
    bool IsOpen() const;

    // Copy the most recently published frame into `out`.
    // Returns false if no frame is available or if the seqlock read fails.
    bool TryRead(protocol::FaceTrackingFrameBody &out);

    // Monotonically increasing publish counter from the shmem header.
    // The worker uses this to detect new frames without repeated seqlock reads.
    uint64_t LastPublishIndex() const;

private:
    protocol::FaceTrackingFrameShmem shmem_;
    uint64_t last_index_ = 0;
};

} // namespace facetracking
