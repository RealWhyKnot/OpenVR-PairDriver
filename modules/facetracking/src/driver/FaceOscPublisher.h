#pragma once

#include "Protocol.h"

#include <cstdint>

namespace facetracking {

struct FaceOscPublishCounts
{
    uint32_t sent = 0;
    uint32_t dropped = 0;

    void Add(const FaceOscPublishCounts &other)
    {
        sent += other.sent;
        dropped += other.dropped;
    }
};

// Publishes one already-filtered face frame through the OSC router. Eye data
// and expression data are gated by FaceTrackingFrameBody::flags.
FaceOscPublishCounts PublishFaceFrameOsc(
    const protocol::FaceTrackingFrameBody &frame);

const char *FaceExpressionOscName(uint32_t index);

} // namespace facetracking
