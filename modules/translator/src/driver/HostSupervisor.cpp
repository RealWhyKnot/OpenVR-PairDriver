#define _CRT_SECURE_NO_DEPRECATE
#include "HostSupervisor.h"
#include "Logging.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <chrono>

namespace translator {

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
        TR_LOG_DRV("[host] host exe path is empty -- not starting");
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

void HostSupervisor::Restart()
{
    TR_LOG_DRV("[host] Restart() requested");
    Kill();
    if (!stop_requested_.load(std::memory_order_acquire)) {
        Spawn();
    }
}

bool HostSupervisor::IsRunning() const
{
    return running_.load(std::memory_order_acquire);
}

// -----------------------------------------------------------------------

bool HostSupervisor::Spawn()
{
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
        TR_LOG_DRV("[host] CreateProcessW failed (err=%lu) for '%s'",
            GetLastError(), host_exe_path_.c_str());
        return false;
    }
    CloseHandle(pi.hThread);
    process_handle_ = pi.hProcess;
    running_.store(true, std::memory_order_release);
    TR_LOG_DRV("[host] spawned pid=%lu '%s'", pi.dwProcessId, host_exe_path_.c_str());
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
    int backoff_ms = 1000;
    constexpr int kMaxBackoffMs = 30000;

    while (!stop_requested_.load(std::memory_order_acquire)) {
        if (process_handle_ == INVALID_HANDLE_VALUE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        DWORD wait = WaitForSingleObject(process_handle_,
            static_cast<DWORD>(backoff_ms > 1000 ? backoff_ms : 1000));

        if (stop_requested_.load(std::memory_order_acquire)) break;

        if (wait == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(process_handle_, &code);
            CloseHandle(process_handle_);
            process_handle_ = INVALID_HANDLE_VALUE;
            running_.store(false, std::memory_order_release);

            TR_LOG_DRV("[host] process exited (code=%lu); restarting in %d ms",
                code, backoff_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));

            if (stop_requested_.load(std::memory_order_acquire)) break;

            if (Spawn()) {
                backoff_ms = 1000;
            } else {
                backoff_ms = std::min(backoff_ms * 2, kMaxBackoffMs);
            }
        }
    }

    TR_LOG_DRV("[host] monitor thread exiting");
}

} // namespace translator
