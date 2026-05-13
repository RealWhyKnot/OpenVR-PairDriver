#include "IPCClient.h"

#include <cstdio>
#include <stdexcept>
#include <string>

namespace {

class BrokenPipeException : public std::runtime_error
{
public:
    BrokenPipeException(const std::string &msg, DWORD code)
        : std::runtime_error(msg), errorCode(code) {}
    DWORD errorCode;
};

bool IsBrokenPipeError(DWORD code)
{
    return code == ERROR_BROKEN_PIPE
        || code == ERROR_PIPE_NOT_CONNECTED
        || code == ERROR_NO_DATA;
}

std::string LastErrorString(DWORD err)
{
    LPWSTR buf = nullptr;
    size_t sz = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&buf, 0, nullptr);
    if (!buf) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, buf, (int)sz, nullptr, 0, nullptr, nullptr);
    std::string out(needed, '\0');
    if (needed > 0) WideCharToMultiByte(CP_UTF8, 0, buf, (int)sz, out.data(), needed, nullptr, nullptr);
    LocalFree(buf);
    return out;
}

} // namespace

FtIPCClient::~FtIPCClient()
{
    Close();
}

void FtIPCClient::Close()
{
    if (pipe_ && pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

void FtIPCClient::Connect()
{
    Close();
    LPCSTR name = OPENVR_PAIRDRIVER_FACETRACKING_PIPE_NAME;
    WaitNamedPipeA(name, 1000);
    pipe_ = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);

    if (pipe_ == INVALID_HANDLE_VALUE) {
        throw std::runtime_error(
            "FaceTracking driver unavailable. SteamVR is not running, "
            "the WKOpenVR shared driver is not installed, or "
            "the FaceTracking feature is not enabled "
            "(enable_facetracking.flag missing from the driver's resources folder).");
    }

    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe_, &mode, nullptr, nullptr)) {
        DWORD err = GetLastError();
        Close();
        throw std::runtime_error(
            "Could not set FaceTracking pipe mode. Error " +
            std::to_string(err) + ": " + LastErrorString(err));
    }

    auto resp = SendBlocking(protocol::Request(protocol::RequestHandshake));
    if (resp.type != protocol::ResponseHandshake || resp.protocol.version != protocol::Version) {
        Close();
        throw std::runtime_error(
            "FaceTracking driver protocol version mismatch. "
            "Reinstall WKOpenVR so the overlay and driver are paired. "
            "(Overlay: " + std::to_string(protocol::Version) +
            ", driver: "  + std::to_string(resp.protocol.version) + ")");
    }
    ++connection_generation_;
}

protocol::Response FtIPCClient::SendBlocking(const protocol::Request &request)
{
    try {
        Send(request);
        return Receive();
    } catch (const BrokenPipeException &e) {
        if (in_reconnect_) throw;

        fprintf(stderr,
            "[FaceTracking IPC] broken pipe (err=%lu); attempting reconnect\n",
            (unsigned long)e.errorCode);
        Close();

        in_reconnect_ = true;
        try {
            Connect();
        } catch (const std::exception &re) {
            in_reconnect_ = false;
            throw std::runtime_error(
                std::string("FaceTracking IPC reconnect failed: ") + re.what());
        }
        in_reconnect_ = false;

        Send(request);
        return Receive();
    }
}

void FtIPCClient::Send(const protocol::Request &request)
{
    DWORD written = 0;
    BOOL ok = WriteFile(pipe_, &request, sizeof(request), &written, nullptr);
    if (!ok) {
        DWORD err = GetLastError();
        std::string msg = "FaceTracking IPC write error " +
                          std::to_string(err) + ": " + LastErrorString(err);
        if (IsBrokenPipeError(err)) throw BrokenPipeException(msg, err);
        throw std::runtime_error(msg);
    }
}

protocol::Response FtIPCClient::Receive()
{
    protocol::Response resp(protocol::ResponseInvalid);
    DWORD bytesRead = 0;

    BOOL ok = ReadFile(pipe_, &resp, sizeof(resp), &bytesRead, nullptr);
    if (!ok) {
        DWORD err = GetLastError();
        if (err != ERROR_MORE_DATA) {
            std::string msg = "FaceTracking IPC read error " +
                              std::to_string(err) + ": " + LastErrorString(err);
            if (IsBrokenPipeError(err)) throw BrokenPipeException(msg, err);
            throw std::runtime_error(msg);
        }
        // Drain an oversized message so the next call starts on a clean boundary.
        char drain[1024];
        while (true) {
            DWORD drained = 0;
            BOOL drainOk = ReadFile(pipe_, drain, sizeof(drain), &drained, nullptr);
            if (drainOk) break;
            DWORD de = GetLastError();
            if (de == ERROR_MORE_DATA) continue;
            if (IsBrokenPipeError(de)) throw BrokenPipeException("Pipe broken draining oversized response", de);
            break;
        }
        throw std::runtime_error(
            "Invalid FaceTracking IPC response: message exceeds expected size " +
            std::to_string(sizeof(resp)));
    }

    if (bytesRead != sizeof(resp)) {
        throw std::runtime_error(
            "Invalid FaceTracking IPC response: got " + std::to_string(bytesRead) +
            " bytes, expected " + std::to_string(sizeof(resp)));
    }
    return resp;
}
