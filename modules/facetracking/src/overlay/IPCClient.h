#pragma once

#include "Protocol.h"

// IPC client for the FaceTracking overlay. Talks to the OpenVR-WKPairDriver
// shared driver over OPENVR_PAIRDRIVER_FACETRACKING_PIPE_NAME. Same
// Request / Response protocol as the other feature pipes; this client sends
// RequestHandshake, RequestSetFaceTrackingConfig,
// RequestSetFaceCalibrationCommand, and RequestSetFaceActiveModule.

class FtIPCClient
{
public:
    ~FtIPCClient();

    // Open the pipe, set message mode, handshake, and verify protocol version.
    // Throws std::runtime_error on failure with a UI-ready message.
    void Connect();

    // Send a request and read the response, with one transparent reconnect on
    // broken-pipe errors (covers vrserver restart mid-request).
    protocol::Response SendBlocking(const protocol::Request &request);

    void Send(const protocol::Request &request);
    protocol::Response Receive();

    void Close();

    bool     IsConnected()        const { return pipe_ != INVALID_HANDLE_VALUE; }
    uint64_t ConnectionGeneration() const { return connection_generation_; }

private:
    HANDLE   pipe_                 = INVALID_HANDLE_VALUE;
    bool     in_reconnect_         = false;
    uint64_t connection_generation_ = 0;
};
