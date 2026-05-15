#define _CRT_SECURE_NO_DEPRECATE
#include "HostSupervisor.h"
#include "Logging.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>

// Named pipe the translator host exposes for config messages from the driver.
#define TR_HOST_CONTROL_PIPE_NAME  "\\\\.\\pipe\\WKOpenVR-Translator.host"

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
    {
        std::lock_guard<std::mutex> lk(process_mutex_);
        halted_                 = false;
        consecutive_fast_exits_ = 0;
        attached_to_existing_   = false;
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
    std::lock_guard<std::mutex> lk(process_mutex_);
    halted_                 = false;
    consecutive_fast_exits_ = 0;
}

void HostSupervisor::Restart()
{
    TR_LOG_DRV("[host] Restart() requested");
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

// -----------------------------------------------------------------------

bool HostSupervisor::CanConnectToHost(int timeout_ms) const
{
    if (!WaitNamedPipeA(TR_HOST_CONTROL_PIPE_NAME, static_cast<DWORD>(timeout_ms)))
        return false;
    HANDLE h = CreateFileA(
        TR_HOST_CONTROL_PIPE_NAME,
        GENERIC_WRITE, 0, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

bool HostSupervisor::IsCleanSingletonExit(DWORD code)
{
    return code == 3 || code == 4;
}

bool HostSupervisor::Spawn()
{
    bool already_running = false;
    bool attached        = false;
    bool spawned         = false;

    {
        std::lock_guard<std::mutex> lk(process_mutex_);

        if (process_handle_ != INVALID_HANDLE_VALUE &&
            WaitForSingleObject(process_handle_, 0) == WAIT_TIMEOUT) {
            TR_LOG_DRV("[translator-supervisor] host already tracked; skipping spawn");
            already_running = true;
        } else if (CanConnectToHost(200)) {
            TR_LOG_DRV("[translator-supervisor] existing host responsive on pipe; attaching without spawn");
            attached_to_existing_ = true;
            running_.store(true, std::memory_order_release);
            attached = true;
        } else {
            int spawn_attempt = consecutive_fast_exits_;
            TR_LOG_DRV("[translator-supervisor] spawn attempt #%d (consecutive_fast_exits=%d)",
                spawn_attempt + 1, spawn_attempt);

            int wlen = MultiByteToWideChar(CP_UTF8, 0,
                host_exe_path_.c_str(), -1, nullptr, 0);
            if (wlen > 0) {
                std::wstring wpath(wlen, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, host_exe_path_.c_str(), -1, wpath.data(), wlen);

                STARTUPINFOW si{};
                si.cb = sizeof(si);
                PROCESS_INFORMATION pi{};

                if (!CreateProcessW(wpath.c_str(), nullptr, nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                    DWORD cpErr = GetLastError();
                    char errMsg[256] = {};
                    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                        nullptr, cpErr, 0, errMsg, sizeof(errMsg) - 1, nullptr);
                    size_t mlen = strlen(errMsg);
                    while (mlen > 0 && (errMsg[mlen-1] == '\r' || errMsg[mlen-1] == '\n'))
                        errMsg[--mlen] = '\0';
                    TR_LOG_DRV("[translator] CreateProcessW FAILED: err=%lu msg=%s path='%s'",
                        cpErr, errMsg, host_exe_path_.c_str());
                } else {
                    CloseHandle(pi.hThread);
                    process_handle_       = pi.hProcess;
                    attached_to_existing_ = false;
                    running_.store(true, std::memory_order_release);
                    TR_LOG_DRV("[translator] CreateProcessW OK: pid=%lu path='%s'",
                        pi.dwProcessId, host_exe_path_.c_str());
                    spawned = true;
                }
            }
        }
    } // process_mutex_ released

    return already_running || attached || spawned;
}

void HostSupervisor::Kill()
{
    std::lock_guard<std::mutex> lk(process_mutex_);
    if (process_handle_ == INVALID_HANDLE_VALUE) return;
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
            bool is_attached = false;
            {
                std::lock_guard<std::mutex> lk(process_mutex_);
                is_attached = attached_to_existing_;
            }
            if (is_attached) {
                if (!CanConnectToHost(0)) {
                    TR_LOG_DRV("[translator-supervisor] attached host pipe gone; triggering respawn");
                    running_.store(false, std::memory_order_release);
                    {
                        std::lock_guard<std::mutex> lk(process_mutex_);
                        attached_to_existing_ = false;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
                    if (!stop_requested_.load(std::memory_order_acquire)) {
                        if (Spawn()) backoff_ms = 1000;
                        else backoff_ms = std::min(backoff_ms * 2, kMaxBackoffMs);
                    }
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            continue;
        }

        // Record spawn time to detect fast exits.
        auto spawn_time = std::chrono::steady_clock::now();

        DWORD wait = WaitForSingleObject(cur_handle,
            static_cast<DWORD>(backoff_ms > 1000 ? backoff_ms : 1000));

        if (stop_requested_.load(std::memory_order_acquire)) break;

        if (wait == WAIT_OBJECT_0) {
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
                running_.store(false, std::memory_order_release);
                continue;
            }
            running_.store(false, std::memory_order_release);

            auto now = std::chrono::steady_clock::now();
            long long uptime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - spawn_time).count();

            TR_LOG_DRV("[translator-supervisor] host process exited code=0x%08lx uptime_ms=%lld",
                (unsigned long)code, uptime_ms);

            bool should_halt = false;
            if (!IsCleanSingletonExit(code)) {
                std::lock_guard<std::mutex> lk(process_mutex_);
                if (uptime_ms < kFastExitThresholdMs) {
                    consecutive_fast_exits_++;
                } else {
                    consecutive_fast_exits_ = 0;
                }
                if (consecutive_fast_exits_ >= kCircuitBreakerThreshold) {
                    halted_     = true;
                    should_halt = true;
                }
            } else {
                TR_LOG_DRV("[translator-supervisor] clean singleton exit (code=%lu); "
                    "skipping fast-exit counter", (unsigned long)code);
            }

            if (should_halt) {
                TR_LOG_DRV("[translator-supervisor] CIRCUIT BREAKER: 5 consecutive fast exits, "
                    "halting respawn for translator. Last exit code: 0x%08lx",
                    (unsigned long)code);
                continue;
            }

            int delay_ms = IsCleanSingletonExit(code) ? 0 : backoff_ms;
            if (delay_ms > 0) {
                TR_LOG_DRV("[host] process exited (code=%lu); restarting in %d ms",
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
    }

    TR_LOG_DRV("[host] monitor thread exiting");
}

} // namespace translator
