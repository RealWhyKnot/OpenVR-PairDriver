#include "FaceFrameReader.h"
#include "Logging.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <limits>

namespace facetracking {

namespace {

uint64_t QpcNow()
{
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return static_cast<uint64_t>(c.QuadPart);
}

uint64_t QpcFrequency()
{
    static uint64_t freq = []() -> uint64_t {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return static_cast<uint64_t>(f.QuadPart);
    }();
    return freq;
}

} // namespace

FaceFrameReader::FaceFrameReader() = default;
FaceFrameReader::~FaceFrameReader() { Close(); }

bool FaceFrameReader::Create(LPCSTR name)
{
    if (!shmem_.Create(name)) {
        FT_LOG_DRV("[reader] failed to create shmem segment '%s' (err=%lu)",
            name, (unsigned long)GetLastError());
        return false;
    }
    FT_LOG_DRV("[reader] created shmem segment '%s'", name);
    return true;
}

void FaceFrameReader::Open(LPCSTR name)
{
    try {
        shmem_.Open(name);
        FT_LOG_DRV("[reader] opened shmem segment '%s'", name);
    } catch (const std::exception &ex) {
        FT_LOG_DRV("[reader] failed to open shmem '%s': %s", name, ex.what());
    }
}

void FaceFrameReader::Close()
{
    shmem_.Close();
    last_index_ = 0;
}

bool FaceFrameReader::IsOpen() const
{
    return (bool)shmem_;
}

bool FaceFrameReader::TryRead(protocol::FaceTrackingFrameBody &out)
{
    if (!shmem_) return false;
    return shmem_.TryReadLatest(out);
}

uint64_t FaceFrameReader::LastPublishIndex() const
{
    return shmem_.PublishIndex();
}

uint32_t FaceFrameReader::HostState() const
{
    if (!shmem_) return protocol::HostStateLegacy;
    return shmem_.HostStateField();
}

uint64_t FaceFrameReader::HeartbeatAgeMs() const
{
    if (!shmem_) return std::numeric_limits<uint64_t>::max();
    const uint64_t hb = shmem_.HostHeartbeatQpc();
    if (hb == 0) return std::numeric_limits<uint64_t>::max();
    const uint64_t now = QpcNow();
    if (now <= hb) return 0; // QPC went backwards or clock-sync edge; treat as fresh.
    const uint64_t freq = QpcFrequency();
    if (freq == 0) return std::numeric_limits<uint64_t>::max();
    return (now - hb) * 1000ULL / freq;
}

void FaceFrameReader::ResetHostLiveness()
{
    if (!shmem_) return;
    shmem_.ResetHostLiveness();
}

} // namespace facetracking
