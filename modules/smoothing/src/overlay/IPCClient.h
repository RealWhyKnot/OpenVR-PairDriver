#pragma once

#include "Protocol.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Named-pipe client for the WKOpenVR smoothing pipe.
// Connects to OPENVR_PAIRDRIVER_SMOOTHING_PIPE_NAME, performs the protocol
// handshake, and sends RequestSetFingerSmoothing payloads. Mirrors the SC
// SmoothingIPCClient pattern but only has to deal with the smoothing-feature subset
// of the protocol -- everything else is rejected by the driver's per-pipe
// feature mask.
class SmoothingIPCClient
{
public:
    ~SmoothingIPCClient();

    void Connect();
    protocol::Response SendBlocking(const protocol::Request &request);

    void Send(const protocol::Request &request);
    protocol::Response Receive();

    bool IsConnected() const { return pipe != INVALID_HANDLE_VALUE; }

private:
    HANDLE pipe = INVALID_HANDLE_VALUE;
    bool inReconnect = false;
};
