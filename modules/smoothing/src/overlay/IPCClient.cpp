#include "IPCClient.h"

#include <stdexcept>
#include <string>

namespace
{
    class BrokenPipeException : public std::runtime_error
    {
    public:
        BrokenPipeException(const std::string& msg, DWORD code)
            : std::runtime_error(msg), errorCode(code) {}
        DWORD errorCode;
    };

    bool IsBrokenPipeError(DWORD code)
    {
        return code == ERROR_BROKEN_PIPE
            || code == ERROR_PIPE_NOT_CONNECTED
            || code == ERROR_NO_DATA;
    }

    std::string LastErrorString(DWORD lastError)
    {
        LPSTR buffer = nullptr;
        size_t size = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, lastError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&buffer, 0, nullptr
        );
        std::string message(buffer ? buffer : "", size);
        if (buffer) LocalFree(buffer);
        return message;
    }
}

SmoothingIPCClient::~SmoothingIPCClient()
{
    if (pipe && pipe != INVALID_HANDLE_VALUE)
        CloseHandle(pipe);
}

void SmoothingIPCClient::Connect()
{
    LPCSTR pipeName = OPENVR_PAIRDRIVER_SMOOTHING_PIPE_NAME;

    WaitNamedPipeA(pipeName, 1000);
    pipe = CreateFileA(pipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);

    if (pipe == INVALID_HANDLE_VALUE) {
        DWORD lastError = GetLastError();
        throw std::runtime_error(
            "OpenVR-WKPairDriver smoothing pipe unavailable. Make sure SteamVR is running and the OpenVR-WKSmoothing addon is installed. Error " +
            std::to_string(lastError) + ": " + LastErrorString(lastError));
    }

    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr)) {
        DWORD lastError = GetLastError();
        throw std::runtime_error("Couldn't set pipe mode. Error " + std::to_string(lastError) + ": " + LastErrorString(lastError));
    }

    auto response = SendBlocking(protocol::Request(protocol::RequestHandshake));
    if (response.type != protocol::ResponseHandshake || response.protocol.version != protocol::Version) {
        throw std::runtime_error(
            "Driver protocol version mismatch. Reinstall OpenVR-WKSmoothing and OpenVR-WKSpaceCalibrator at compatible versions. (Client: " +
            std::to_string(protocol::Version) + ", Driver: " + std::to_string(response.protocol.version) + ")");
    }
}

protocol::Response SmoothingIPCClient::SendBlocking(const protocol::Request &request)
{
    try {
        Send(request);
        return Receive();
    } catch (const BrokenPipeException &e) {
        if (inReconnect) throw;

        if (pipe && pipe != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe);
            pipe = INVALID_HANDLE_VALUE;
        }

        inReconnect = true;
        try {
            Connect();
        } catch (const std::exception &reconnectErr) {
            inReconnect = false;
            throw std::runtime_error(std::string("IPC reconnect failed after broken pipe: ") + reconnectErr.what());
        }
        inReconnect = false;

        Send(request);
        return Receive();
    }
}

void SmoothingIPCClient::Send(const protocol::Request &request)
{
    DWORD bytesWritten;
    BOOL success = WriteFile(pipe, &request, sizeof request, &bytesWritten, nullptr);
    if (!success) {
        DWORD lastError = GetLastError();
        std::string msg = "Error writing IPC request. Error " + std::to_string(lastError) + ": " + LastErrorString(lastError);
        if (IsBrokenPipeError(lastError)) {
            throw BrokenPipeException(msg, lastError);
        }
        throw std::runtime_error(msg);
    }
}

protocol::Response SmoothingIPCClient::Receive()
{
    protocol::Response response(protocol::ResponseInvalid);
    DWORD bytesRead;

    BOOL success = ReadFile(pipe, &response, sizeof response, &bytesRead, nullptr);
    if (!success) {
        DWORD lastError = GetLastError();
        if (lastError != ERROR_MORE_DATA) {
            std::string msg = "Error reading IPC response. Error " + std::to_string(lastError) + ": " + LastErrorString(lastError);
            if (IsBrokenPipeError(lastError)) {
                throw BrokenPipeException(msg, lastError);
            }
            throw std::runtime_error(msg);
        }

        // Drain the rest of an oversized message before throwing so the next
        // Receive() starts on a clean message boundary.
        char drainBuf[1024];
        for (;;) {
            DWORD drained = 0;
            BOOL drainSuccess = ReadFile(pipe, drainBuf, sizeof drainBuf, &drained, nullptr);
            if (drainSuccess) break;
            DWORD drainErr = GetLastError();
            if (drainErr == ERROR_MORE_DATA) continue;
            if (IsBrokenPipeError(drainErr)) {
                throw BrokenPipeException("Pipe broken while draining oversized IPC response", drainErr);
            }
            break;
        }
        throw std::runtime_error("Invalid IPC response. Message larger than expected " + std::to_string(sizeof response) + " bytes.");
    }

    if (bytesRead != sizeof response) {
        throw std::runtime_error("Invalid IPC response. Got " + std::to_string(bytesRead) + " bytes.");
    }

    return response;
}
