#include "FaceFrameReader.h"
#include "Logging.h"

namespace facetracking {

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

} // namespace facetracking
