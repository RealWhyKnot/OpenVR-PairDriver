#define _CRT_SECURE_NO_DEPRECATE
#include "HostSupervisor.h"
#include "DebugLogging.h"
#include "LogPaths.h"
#include "Logging.h"
#include "Win32Paths.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>

// Named pipe the host exposes for control messages from the driver.
#define FT_HOST_CONTROL_PIPE_NAME  "\\\\.\\pipe\\OpenVR-FaceTracking.host"

namespace facetracking {
namespace {

std::wstring QuoteArg(const std::wstring &value)
{
    std::wstring out = L"\"";
    for (wchar_t ch : value) {
        if (ch == L'"') out += L"\\\"";
        else out.push_back(ch);
    }
    out += L"\"";
    return out;
}

void AppendPathArg(std::wstring &commandLine, const wchar_t *name, const std::wstring &value)
{
    if (value.empty()) return;
    commandLine += L" ";
    commandLine += name;
    commandLine += L" ";
    commandLine += QuoteArg(value);
}

} // namespace

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
        attached_to_existing_   = false;
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
    {
        std::lock_guard<std::mutex> lk(uuid_mutex_);
        if (has_pending_uuid_) uuid_sent_ = false;
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
    std::string s = uuid ? std::string(uuid) : std::string();
    {
        std::lock_guard<std::mutex> lk(uuid_mutex_);
        pending_uuid_     = s;
        has_pending_uuid_ = true;
        uuid_sent_        = false;
    }
    // Try to deliver immediately; if the pipe is down, MonitorLoop will retry.
    TrySendUuid(s);
}

// -----------------------------------------------------------------------

// Returns true if the host's control pipe answers within timeout_ms.
// Used to detect a responsive host from a prior session before spawning.
bool HostSupervisor::CanConnectToHost(int timeout_ms) const
{
    // WaitNamedPipeA probes whether a server is listening on the pipe.
    if (!WaitNamedPipeA(FT_HOST_CONTROL_PIPE_NAME, static_cast<DWORD>(timeout_ms))) {
        return false;
    }
    // Briefly open for write to confirm the server will accept connections.
    HANDLE h = CreateFileA(
        FT_HOST_CONTROL_PIPE_NAME,
        GENERIC_WRITE, 0, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

// Exit codes 3 (singleton-owned) and 4 (pipe-busy) are clean handoff exits
// that must not count toward the crash circuit-breaker.
bool HostSupervisor::IsCleanSingletonExit(DWORD code)
{
    return code == 3 || code == 4;
}

bool HostSupervisor::Spawn()
{
    // Phase 1: all state mutation under the lock.
    bool already_running   = false;
    bool attached          = false;
    bool spawned           = false;

    {
        std::lock_guard<std::mutex> lk(process_mutex_);

        if (process_handle_ != INVALID_HANDLE_VALUE &&
            WaitForSingleObject(process_handle_, 0) == WAIT_TIMEOUT) {
            FT_LOG_DRV("[host-supervisor] host already tracked; skipping spawn", 0);
            already_running = true;
        } else if (CanConnectToHost(200)) {
            // Connect-first: an existing host from a prior session is responsive.
            FT_LOG_DRV("[host-supervisor] existing host responsive on pipe; attaching without spawn", 0);
            attached_to_existing_ = true;
            running_.store(true, std::memory_order_release);
            attached = true;
        } else {
            int spawn_attempt = consecutive_fast_exits_;
            FT_LOG_DRV("[host-supervisor] spawn attempt #%d (consecutive_fast_exits=%d)",
                spawn_attempt + 1, spawn_attempt);

            int wlen = MultiByteToWideChar(CP_UTF8, 0,
                host_exe_path_.c_str(), -1, nullptr, 0);
            if (wlen > 0) {
                std::wstring wpath(wlen, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, host_exe_path_.c_str(), -1, wpath.data(), wlen);
                if (!wpath.empty() && wpath.back() == L'\0') wpath.pop_back();
                std::wstring commandLine = L"\"" + wpath + L"\"";
                const bool debugLogging = openvr_pair::common::IsDebugLoggingEnabled();
                if (debugLogging) {
                    commandLine += L" --debug-logging 1";
                }
                std::wstring faceDir = openvr_pair::common::WkOpenVrSubdirectoryPath(
                    L"facetracking", true);
                AppendPathArg(commandLine, L"--status-file",
                    faceDir.empty() ? std::wstring{} : faceDir + L"\\host_status.json");
                AppendPathArg(commandLine, L"--modules-dir",
                    faceDir.empty() ? std::wstring{} : faceDir + L"\\modules");
                if (debugLogging) {
                    AppendPathArg(commandLine, L"--log-file",
                        openvr_pair::common::TimestampedLogPath(L"facetracking_host_log"));
                }
                FT_LOG_DRV("[host-supervisor] command line prepared debug=%d status_dir_resolved=%d",
                    debugLogging ? 1 : 0, faceDir.empty() ? 0 : 1);

                STARTUPINFOW si{};
                si.cb = sizeof(si);
                PROCESS_INFORMATION pi{};

                if (!CreateProcessW(wpath.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                    DWORD cpErr = GetLastError();
                    char errMsg[256] = {};
                    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                        nullptr, cpErr, 0, errMsg, sizeof(errMsg) - 1, nullptr);
                    size_t mlen = strlen(errMsg);
                    while (mlen > 0 && (errMsg[mlen-1] == '\r' || errMsg[mlen-1] == '\n'))
                        errMsg[--mlen] = '\0';
                    FT_LOG_DRV("[facetracking] CreateProcessW FAILED: err=%lu msg=%s path='%s'",
                        cpErr, errMsg, host_exe_path_.c_str());
                } else {
                    CloseHandle(pi.hThread);
                    process_handle_       = pi.hProcess;
                    attached_to_existing_ = false;
                    running_.store(true, std::memory_order_release);
                    FT_LOG_DRV("[facetracking] CreateProcessW OK: pid=%lu path='%s'",
                        pi.dwProcessId, host_exe_path_.c_str());
                    spawned = true;
                }
            }
        }
    } // process_mutex_ released

    if (already_running) return true;
    if (!attached && !spawned) return false;

    // Phase 2: deliver any queued uuid OUTSIDE the process lock to avoid
    // blocking WriteFile from inside a mutex.
    std::string uuid_to_send;
    bool should_send_uuid = false;
    {
        std::lock_guard<std::mutex> ulk(uuid_mutex_);
        if (has_pending_uuid_ && !uuid_sent_) {
            uuid_to_send = pending_uuid_;
            should_send_uuid = true;
        }
    }
    if (should_send_uuid) TrySendUuid(uuid_to_send);
    return true;
}

void HostSupervisor::Kill()
{
    std::lock_guard<std::mutex> lk(process_mutex_);
    if (process_handle_ == INVALID_HANDLE_VALUE) return;
    // Only terminate processes we actually spawned; if we attached to an
    // existing host from a prior session, just close our handle and let
    // that process manage its own lifetime.
    if (!attached_to_existing_) {
        TerminateProcess(process_handle_, 0);
        WaitForSingleObject(process_handle_, 5000);
    }
    CloseHandle(process_handle_);
    process_handle_       = INVALID_HANDLE_VALUE;
    attached_to_existing_ = false;
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
            // Attached to an existing host (no process handle). Poll the pipe
            // to detect when that host dies, then respawn.
            bool is_attached = false;
            {
                std::lock_guard<std::mutex> lk(process_mutex_);
                is_attached = attached_to_existing_;
            }
            if (is_attached) {
                if (!CanConnectToHost(0)) {
                    FT_LOG_DRV("[host-supervisor] attached host pipe gone; triggering respawn", 0);
                    running_.store(false, std::memory_order_release);
                    {
                        std::lock_guard<std::mutex> lk(process_mutex_);
                        attached_to_existing_ = false;
                    }
                    {
                        std::lock_guard<std::mutex> lk(uuid_mutex_);
                        if (has_pending_uuid_) uuid_sent_ = false;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
                    if (!stop_requested_.load(std::memory_order_acquire)) {
                        if (Spawn()) backoff_ms = 1000;
                        else backoff_ms = std::min(backoff_ms * 2, kMaxBackoffMs);
                    }
                } else {
                    RetryPendingUuid();
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            continue;
        }

        // Record spawn time to detect fast exits.
        auto spawn_time = std::chrono::steady_clock::now();

        RetryPendingUuid();

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
            {
                std::lock_guard<std::mutex> lk(uuid_mutex_);
                if (has_pending_uuid_) uuid_sent_ = false;
            }

            auto now = std::chrono::steady_clock::now();
            long long uptime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - spawn_time).count();

            FT_LOG_DRV("[host-supervisor] host process exited code=0x%08lx uptime_ms=%lld",
                (unsigned long)code, uptime_ms);

            // Circuit breaker: singleton/pipe-busy exits are clean handoffs;
            // only real crashes (DLL missing, unhandled exception, etc.) count.
            bool should_halt = false;
            if (!IsCleanSingletonExit(code)) {
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
            } else {
                FT_LOG_DRV("[host-supervisor] clean singleton exit (code=%lu); "
                    "skipping fast-exit counter", (unsigned long)code);
            }

            if (should_halt) {
                FT_LOG_DRV("[host-supervisor] CIRCUIT BREAKER: 5 consecutive fast exits, "
                    "halting respawn for facetracking. Last exit code: 0x%08lx",
                    (unsigned long)code);
                continue;
            }

            // For clean singleton exits, the connect-first path in Spawn() will
            // find the existing host; no delay needed.
            int delay_ms = IsCleanSingletonExit(code) ? 0 : backoff_ms;
            if (delay_ms > 0) {
                FT_LOG_DRV("[host] process exited (code=%lu); restarting in %d ms",
                    (unsigned long)code, delay_ms);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            }

            if (stop_requested_.load(std::memory_order_acquire)) break;

            if (Spawn()) {
                backoff_ms = 1000;
            } else {
                backoff_ms = std::min(backoff_ms * 2, kMaxBackoffMs);
            }
        }
        // else WAIT_TIMEOUT: process still alive, loop again and retry any
        // active-module selection that arrived before the host pipe opened.
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

void HostSupervisor::RetryPendingUuid()
{
    std::string uuid;
    bool should_send_uuid = false;
    {
        std::lock_guard<std::mutex> lk(uuid_mutex_);
        if (has_pending_uuid_ && !uuid_sent_) {
            uuid = pending_uuid_;
            should_send_uuid = true;
        }
    }
    if (should_send_uuid) {
        TrySendUuid(uuid);
    }
}

} // namespace facetracking
