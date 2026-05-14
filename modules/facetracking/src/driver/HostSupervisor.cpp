#define _CRT_SECURE_NO_DEPRECATE
#include "HostSupervisor.h"
#include "Logging.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>

// Named pipe the host exposes for control messages from the driver.
#define FT_HOST_CONTROL_PIPE_NAME  "\\\\.\\pipe\\OpenVR-FaceTracking.host"

namespace facetracking {

HostSupervisor::HostSupervisor(const std::string &host_exe_path)
    : host_exe_path_(host_exe_path)
{}

HostSupervisor::~HostSupervisor()
{
    Stop();
}

bool HostSupervisor::Start()
{
    if (host_exe_path_.empty()) {
        FT_LOG_DRV("[host] host exe path is empty -- not starting", 0);
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(process_mutex_);
        stop_requested_.store(false, std::memory_order_release);
        halted_                 = false;
        consecutive_fast_exits_ = 0;
    }
    if (!Spawn()) return false;
    monitor_thread_ = std::thread([this]{ MonitorLoop(); });
    return true;
}

void HostSupervisor::Stop()
{
    stop_requested_.store(true, std::memory_order_release);
    Kill();
    if (monitor_thread_.joinable()) monitor_thread_.join();
    std::lock_guard<std::mutex> lk(process_mutex_);
    halted_                 = false;
    consecutive_fast_exits_ = 0;
}

void HostSupervisor::Restart()
{
    FT_LOG_DRV("[host] Restart() requested", 0);
    {
        std::lock_guard<std::mutex> lk(process_mutex_);
        halted_                 = false;
        consecutive_fast_exits_ = 0;
    }
    Kill();
    if (!stop_requested_.load(std::memory_order_acquire)) {
        Spawn();
    }
}

bool HostSupervisor::IsRunning() const
{
    return running_.load(std::memory_order_acquire);
}

bool HostSupervisor::IsHalted() const
{
    std::lock_guard<std::mutex> lk(process_mutex_);
    return halted_;
}

void HostSupervisor::SetActiveModuleUuid(const char *uuid)
{
    if (!uuid || uuid[0] == '\0') return;
    std::string s(uuid);
    {
        std::lock_guard<std::mutex> lk(uuid_mutex_);
        pending_uuid_ = s;
        uuid_sent_    = false;
    }
    // Try to deliver immediately; if the pipe is down, MonitorLoop will retry.
    TrySendUuid(s);
}

// -----------------------------------------------------------------------

bool HostSupervisor::Spawn()
{
    // Convert path to wide for CreateProcessW.
    int wlen = MultiByteToWideChar(CP_UTF8, 0,
        host_exe_path_.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return false;
    std::wstring wpath(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, host_exe_path_.c_str(), -1, wpath.data(), wlen);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    int spawn_attempt = 0;
    {
        std::lock_guard<std::mutex> lk(process_mutex_);
        spawn_attempt = consecutive_fast_exits_;
    }
    FT_LOG_DRV("[host-supervisor] spawn attempt #%d (consecutive_fast_exits=%d)",
        spawn_attempt + 1, spawn_attempt);

    DWORD cpErr = 0;
    if (!CreateProcessW(wpath.c_str(), nullptr, nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        cpErr = GetLastError();
        char errMsg[256] = {};
        FormatMessageA(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, cpErr, 0, errMsg, sizeof(errMsg) - 1, nullptr);
        size_t mlen = strlen(errMsg);
        while (mlen > 0 && (errMsg[mlen-1] == '\r' || errMsg[mlen-1] == '\n'))
            errMsg[--mlen] = '\0';
        FT_LOG_DRV("[facetracking] CreateProcessW FAILED: err=%lu msg=%s path='%s'",
            cpErr, errMsg, host_exe_path_.c_str());
        return false;
    }
    // Close the thread handle; we only need the process handle.
    CloseHandle(pi.hThread);
    {
        std::lock_guard<std::mutex> lk(process_mutex_);
        process_handle_ = pi.hProcess;
    }
    running_.store(true, std::memory_order_release);
    FT_LOG_DRV("[facetracking] CreateProcessW OK: pid=%lu path='%s'",
        pi.dwProcessId, host_exe_path_.c_str());

    // Deliver any queued module uuid. Capture the value under the lock,
    // release the lock, THEN call TrySendUuid. The previous form held
    // uuid_mutex_ across the TrySendUuid call, which itself re-locks the
    // same non-recursive mutex on success -- a hard self-deadlock the
    // moment the host's control pipe is reachable on the first try.
    std::string uuid_to_send;
    {
        std::lock_guard<std::mutex> lk(uuid_mutex_);
        if (!pending_uuid_.empty() && !uuid_sent_) {
            uuid_to_send = pending_uuid_;
        }
    }
    if (!uuid_to_send.empty()) {
        TrySendUuid(uuid_to_send);
    }
    return true;
}

void HostSupervisor::Kill()
{
    std::lock_guard<std::mutex> lk(process_mutex_);
    if (process_handle_ == INVALID_HANDLE_VALUE) return;
    TerminateProcess(process_handle_, 0);
    WaitForSingleObject(process_handle_, 5000);
    CloseHandle(process_handle_);
    process_handle_ = INVALID_HANDLE_VALUE;
    running_.store(false, std::memory_order_release);
}

void HostSupervisor::MonitorLoop()
{
    // Exponential backoff: 1s, 2s, 4s, 8s, 16s, 30s (cap).
    int backoff_ms = 1000;
    constexpr int kMaxBackoffMs = 30000;

    while (!stop_requested_.load(std::memory_order_acquire)) {
        HANDLE cur_handle = INVALID_HANDLE_VALUE;
        bool   is_halted  = false;
        {
            std::lock_guard<std::mutex> lk(process_mutex_);
            cur_handle = process_handle_;
            is_halted  = halted_;
        }

        if (is_halted) {
            // Circuit breaker tripped: sleep and wait for a Stop()/Start() reset.
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            continue;
        }

        if (cur_handle == INVALID_HANDLE_VALUE) {
            // Not spawned yet; this shouldn't happen but guard anyway.
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        // Record spawn time to detect fast exits.
        auto spawn_time = std::chrono::steady_clock::now();

        // Wait for the process to exit or for a stop request.
        DWORD wait = WaitForSingleObject(cur_handle,
            static_cast<DWORD>(backoff_ms > 1000 ? backoff_ms : 1000));

        if (stop_requested_.load(std::memory_order_acquire)) break;

        if (wait == WAIT_OBJECT_0) {
            // Process exited.
            DWORD code = 0;
            bool handle_was_valid = false;
            {
                std::lock_guard<std::mutex> lk(process_mutex_);
                if (process_handle_ != INVALID_HANDLE_VALUE) {
                    GetExitCodeProcess(process_handle_, &code);
                    CloseHandle(process_handle_);
                    process_handle_  = INVALID_HANDLE_VALUE;
                    handle_was_valid = true;
                }
            }
            if (!handle_was_valid) {
                // Kill() beat us to it; just reset running state and loop.
                running_.store(false, std::memory_order_release);
                continue;
            }
            running_.store(false, std::memory_order_release);

            auto now = std::chrono::steady_clock::now();
            long long uptime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - spawn_time).count();

            FT_LOG_DRV("[host-supervisor] host process exited code=0x%08lx uptime_ms=%lld",
                (unsigned long)code, uptime_ms);

            // Circuit breaker: accumulate fast exits.
            bool should_halt = false;
            {
                std::lock_guard<std::mutex> lk(process_mutex_);
                if (uptime_ms < kFastExitThresholdMs) {
                    consecutive_fast_exits_++;
                } else {
                    consecutive_fast_exits_ = 0;
                }
                if (consecutive_fast_exits_ >= kCircuitBreakerThreshold) {
                    halted_      = true;
                    should_halt  = true;
                }
            }

            if (should_halt) {
                FT_LOG_DRV("[host-supervisor] CIRCUIT BREAKER: 5 consecutive fast exits, "
                    "halting respawn for facetracking. Last exit code: 0x%08lx",
                    (unsigned long)code);
                continue;
            }

            FT_LOG_DRV("[host] process exited (code=%lu); restarting in %d ms",
                (unsigned long)code, backoff_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));

            if (stop_requested_.load(std::memory_order_acquire)) break;

            if (Spawn()) {
                backoff_ms = 1000; // reset backoff on successful start
            } else {
                backoff_ms = std::min(backoff_ms * 2, kMaxBackoffMs);
            }
        }
        // else WAIT_TIMEOUT: process still alive, loop again
    }

    FT_LOG_DRV("[host] monitor thread exiting", 0);
}

// Minimal CBOR encoder for the control-pipe wire. The host's
// HostControlPipeServer reads [4-byte LE length][CBOR map]. We only need
// to emit one shape:
//   { "type": "SelectModule", "uuid": "<uuid>" }
// All keys and values are short ASCII text strings.
//
// CBOR text-string framing:
//   0..23  : single byte 0x60 + len, followed by the bytes
//   24..255: byte 0x78, one length byte, then the bytes
// All strings used here fit one of those two cases.
static void CborAppendTextString(std::string &out, const char *s, size_t len)
{
    if (len < 24) {
        out.push_back(static_cast<char>(0x60 | static_cast<unsigned char>(len)));
    } else {
        out.push_back(static_cast<char>(0x78));
        out.push_back(static_cast<char>(len & 0xff));
    }
    out.append(s, len);
}

static std::string EncodeSelectModule(const std::string &uuid)
{
    std::string body;
    body.push_back(static_cast<char>(0xA2)); // map with 2 pairs
    CborAppendTextString(body, "type", 4);
    CborAppendTextString(body, "SelectModule", 12);
    CborAppendTextString(body, "uuid", 4);
    CborAppendTextString(body, uuid.c_str(), uuid.size());

    std::string wire;
    wire.reserve(4 + body.size());
    const uint32_t len = static_cast<uint32_t>(body.size());
    wire.push_back(static_cast<char>(len        & 0xff));
    wire.push_back(static_cast<char>((len >> 8) & 0xff));
    wire.push_back(static_cast<char>((len >> 16) & 0xff));
    wire.push_back(static_cast<char>((len >> 24) & 0xff));
    wire.append(body);
    return wire;
}

bool HostSupervisor::TrySendUuid(const std::string &uuid)
{
    // The host's HostControlPipeServer reads [4-byte LE length][CBOR map].
    std::string msg = EncodeSelectModule(uuid);

    HANDLE h = CreateFileA(
        FT_HOST_CONTROL_PIPE_NAME,
        GENERIC_WRITE,
        0, nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (h == INVALID_HANDLE_VALUE) {
        // Pipe not up yet; will retry when the host connects.
        return false;
    }

    DWORD written = 0;
    BOOL ok = WriteFile(h, msg.data(), (DWORD)msg.size(), &written, nullptr);
    CloseHandle(h);

    if (ok && written == (DWORD)msg.size()) {
        std::lock_guard<std::mutex> lk(uuid_mutex_);
        uuid_sent_ = true;
        FT_LOG_DRV("[host] sent SelectModule uuid '%s'", uuid.c_str());
        return true;
    }
    return false;
}

} // namespace facetracking
