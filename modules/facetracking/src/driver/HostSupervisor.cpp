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
// Each control message is a simple NUL-terminated ASCII string, e.g.
// "module:<uuid>" -- small enough to write in one synchronous call.

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
    stop_requested_.store(false, std::memory_order_release);
    if (!Spawn()) return false;
    monitor_thread_ = std::thread([this]{ MonitorLoop(); });
    return true;
}

void HostSupervisor::Stop()
{
    stop_requested_.store(true, std::memory_order_release);
    Kill();
    if (monitor_thread_.joinable()) monitor_thread_.join();
}

bool HostSupervisor::IsRunning() const
{
    return running_.load(std::memory_order_acquire);
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

    if (!CreateProcessW(wpath.c_str(), nullptr, nullptr, nullptr, FALSE,
            0, nullptr, nullptr, &si, &pi)) {
        FT_LOG_DRV("[host] CreateProcessW failed (err=%lu) for '%s'",
            GetLastError(), host_exe_path_.c_str());
        return false;
    }
    // Close the thread handle; we only need the process handle.
    CloseHandle(pi.hThread);
    process_handle_ = pi.hProcess;
    running_.store(true, std::memory_order_release);
    FT_LOG_DRV("[host] spawned pid=%lu '%s'", pi.dwProcessId, host_exe_path_.c_str());

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
        if (process_handle_ == INVALID_HANDLE_VALUE) {
            // Not spawned yet; this shouldn't happen but guard anyway.
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        // Wait for the process to exit or for a stop request.
        DWORD wait = WaitForSingleObject(process_handle_,
            static_cast<DWORD>(backoff_ms > 1000 ? backoff_ms : 1000));

        if (stop_requested_.load(std::memory_order_acquire)) break;

        if (wait == WAIT_OBJECT_0) {
            // Process exited.
            DWORD code = 0;
            GetExitCodeProcess(process_handle_, &code);
            CloseHandle(process_handle_);
            process_handle_ = INVALID_HANDLE_VALUE;
            running_.store(false, std::memory_order_release);

            FT_LOG_DRV("[host] process exited (code=%lu); restarting in %d ms",
                code, backoff_ms);
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

bool HostSupervisor::TrySendUuid(const std::string &uuid)
{
    // Build the message: "module:<uuid>\n"
    std::string msg = "module:" + uuid + "\n";

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
        FT_LOG_DRV("[host] sent module uuid '%s'", uuid.c_str());
        return true;
    }
    return false;
}

} // namespace facetracking
