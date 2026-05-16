#define _CRT_SECURE_NO_DEPRECATE
#include "HostSupervisorBase.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <utility>

namespace openvr_pair::common {

HostSupervisorBase::HostSupervisorBase(std::string host_exe_path)
    : host_exe_path_(std::move(host_exe_path))
{
    job_handle_ = CreateJobObjectW(nullptr, nullptr);
    if (job_handle_) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job_handle_,
                JobObjectExtendedLimitInformation, &info, sizeof(info))) {
            Log("[host-supervisor] SetInformationJobObject failed err=%lu; "
                "host will not be auto-killed on driver crash",
                GetLastError());
            CloseHandle(job_handle_);
            job_handle_ = nullptr;
        }
    } else {
        Log("[host-supervisor] CreateJobObjectW failed err=%lu; "
            "host will not be auto-killed on driver crash",
            GetLastError());
    }
}

HostSupervisorBase::~HostSupervisorBase()
{
    Stop();
    if (job_handle_) {
        CloseHandle(job_handle_);
        job_handle_ = nullptr;
    }
}

bool HostSupervisorBase::Start()
{
    if (host_exe_path_.empty()) {
        Log("[host] host exe path is empty -- not starting");
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

void HostSupervisorBase::Stop()
{
    stop_requested_.store(true, std::memory_order_release);
    Kill();
    if (monitor_thread_.joinable()) monitor_thread_.join();
    std::lock_guard<std::mutex> lk(process_mutex_);
    halted_                 = false;
    consecutive_fast_exits_ = 0;
}

void HostSupervisorBase::Restart()
{
    Log("[host] Restart() requested");
    {
        std::lock_guard<std::mutex> lk(process_mutex_);
        halted_                 = false;
        consecutive_fast_exits_ = 0;
    }
    OnHostExited();
    Kill();
    if (!stop_requested_.load(std::memory_order_acquire)) {
        Spawn();
    }
}

bool HostSupervisorBase::IsRunning() const
{
    return running_.load(std::memory_order_acquire);
}

bool HostSupervisorBase::IsHalted() const
{
    std::lock_guard<std::mutex> lk(process_mutex_);
    return halted_;
}

uint32_t HostSupervisorBase::LastExitCode() const
{
    std::lock_guard<std::mutex> lk(process_mutex_);
    return last_exit_code_;
}

std::string HostSupervisorBase::LastExitDescription() const
{
    std::lock_guard<std::mutex> lk(process_mutex_);
    return last_exit_description_;
}

void HostSupervisorBase::Log(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    LogV(fmt, args);
    va_end(args);
}

void HostSupervisorBase::BuildCommandLine(std::wstring& /*commandLine*/,
                                          const std::wstring& /*exe_path*/) const
{
    // Default: nothing appended after argv[0].
}

bool HostSupervisorBase::CanConnectToHost(int timeout_ms) const
{
    const std::string pipe = ControlPipeName();
    if (!WaitNamedPipeA(pipe.c_str(), static_cast<DWORD>(timeout_ms)))
        return false;
    HANDLE h = CreateFileA(pipe.c_str(),
                           GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

bool HostSupervisorBase::SendBytesOverControlPipe(const void* data, size_t len)
{
    const std::string pipe = ControlPipeName();
    HANDLE h = CreateFileA(pipe.c_str(),
                           GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    BOOL ok = WriteFile(h, data, static_cast<DWORD>(len), &written, nullptr);
    CloseHandle(h);

    return ok && written == static_cast<DWORD>(len);
}

bool HostSupervisorBase::IsCleanSingletonExit(DWORD code)
{
    return code == 3 || code == 4;
}

bool HostSupervisorBase::Spawn()
{
    bool already_running = false;
    bool attached        = false;
    bool spawned         = false;

    {
        std::lock_guard<std::mutex> lk(process_mutex_);

        if (process_handle_ != INVALID_HANDLE_VALUE &&
            WaitForSingleObject(process_handle_, 0) == WAIT_TIMEOUT) {
            Log("[host-supervisor] host already tracked; skipping spawn");
            already_running = true;
        } else if (CanConnectToHost(200)) {
            Log("[host-supervisor] existing host responsive on pipe; "
                "attaching without spawn");
            attached_to_existing_ = true;
            running_.store(true, std::memory_order_release);
            attached = true;
        } else {
            int spawn_attempt = consecutive_fast_exits_;
            Log("[host-supervisor] spawn attempt #%d (consecutive_fast_exits=%d)",
                spawn_attempt + 1, spawn_attempt);

            int wlen = MultiByteToWideChar(CP_UTF8, 0,
                host_exe_path_.c_str(), -1, nullptr, 0);
            if (wlen > 0) {
                std::wstring wpath(wlen, L'\0');
                MultiByteToWideChar(CP_UTF8, 0,
                    host_exe_path_.c_str(), -1, wpath.data(), wlen);
                if (!wpath.empty() && wpath.back() == L'\0') wpath.pop_back();

                std::wstring commandLine = L"\"" + wpath + L"\"";
                BuildCommandLine(commandLine, wpath);

                STARTUPINFOW si{};
                si.cb = sizeof(si);
                PROCESS_INFORMATION pi{};

                if (!CreateProcessW(wpath.c_str(), commandLine.data(),
                        nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                    DWORD cpErr = GetLastError();
                    char errMsg[256] = {};
                    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM |
                                   FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, cpErr, 0, errMsg,
                                   sizeof(errMsg) - 1, nullptr);
                    size_t mlen = strlen(errMsg);
                    while (mlen > 0 &&
                           (errMsg[mlen - 1] == '\r' || errMsg[mlen - 1] == '\n'))
                        errMsg[--mlen] = '\0';
                    Log("[host-supervisor] CreateProcessW FAILED: "
                        "err=%lu msg=%s path='%s'",
                        cpErr, errMsg, host_exe_path_.c_str());
                } else {
                    CloseHandle(pi.hThread);
                    process_handle_       = pi.hProcess;
                    attached_to_existing_ = false;
                    running_.store(true, std::memory_order_release);

                    if (job_handle_) {
                        if (!AssignProcessToJobObject(job_handle_, pi.hProcess)) {
                            Log("[host-supervisor] AssignProcessToJobObject failed "
                                "err=%lu pid=%lu; host can outlive an abnormal driver exit",
                                GetLastError(), pi.dwProcessId);
                        }
                    }

                    Log("[host-supervisor] CreateProcessW OK: pid=%lu path='%s'",
                        pi.dwProcessId, host_exe_path_.c_str());
                    spawned = true;
                }
            }
        }
    } // process_mutex_ released

    if (already_running) return true;
    if (!attached && !spawned) return false;

    // Subclass flushes any queued control-pipe message OUTSIDE the process
    // lock so a slow pipe write does not block the supervisor.
    OnHostReady();
    return true;
}

void HostSupervisorBase::Kill()
{
    std::lock_guard<std::mutex> lk(process_mutex_);
    if (process_handle_ == INVALID_HANDLE_VALUE) return;

    if (!attached_to_existing_) {
        TerminateProcess(process_handle_, 0);
        DWORD wait_result = WaitForSingleObject(process_handle_, 5000);
        if (wait_result == WAIT_TIMEOUT) {
            Log("[host-supervisor] Kill: host did not exit within 5s of "
                "TerminateProcess; retrying");
            TerminateProcess(process_handle_, 1);
            WaitForSingleObject(process_handle_, 1000);
        } else if (wait_result == WAIT_FAILED) {
            Log("[host-supervisor] Kill: WaitForSingleObject failed err=%lu",
                GetLastError());
        }
    }
    CloseHandle(process_handle_);
    process_handle_       = INVALID_HANDLE_VALUE;
    attached_to_existing_ = false;
    running_.store(false, std::memory_order_release);
}

void HostSupervisorBase::MonitorLoop()
{
    int backoff_ms = kBackoffStartMs;

    while (!stop_requested_.load(std::memory_order_acquire)) {
        HANDLE cur_handle = INVALID_HANDLE_VALUE;
        bool   is_halted  = false;
        {
            std::lock_guard<std::mutex> lk(process_mutex_);
            cur_handle = process_handle_;
            is_halted  = halted_;
        }

        if (is_halted) {
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
                    Log("[host-supervisor] attached host pipe gone; "
                        "triggering respawn");
                    running_.store(false, std::memory_order_release);
                    {
                        std::lock_guard<std::mutex> lk(process_mutex_);
                        attached_to_existing_ = false;
                    }
                    OnHostExited();
                    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
                    if (!stop_requested_.load(std::memory_order_acquire)) {
                        if (Spawn()) backoff_ms = kBackoffStartMs;
                        else         backoff_ms = std::min(backoff_ms * 2, kBackoffMaxMs);
                    }
                } else {
                    OnHostReady();
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            continue;
        }

        auto spawn_time = std::chrono::steady_clock::now();

        OnHostReady();

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
                // Kill() beat us to it; just reset running state and loop.
                running_.store(false, std::memory_order_release);
                continue;
            }
            running_.store(false, std::memory_order_release);
            OnHostExited();

            auto now = std::chrono::steady_clock::now();
            long long uptime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - spawn_time).count();

            std::string description = DescribeExitCode(code);
            if (description.empty()) {
                Log("[host-supervisor] host process exited "
                    "code=0x%08lx uptime_ms=%lld",
                    static_cast<unsigned long>(code), uptime_ms);
            } else {
                Log("[host-supervisor] host process exited "
                    "code=0x%08lx uptime_ms=%lld -- %s",
                    static_cast<unsigned long>(code), uptime_ms,
                    description.c_str());
            }
            {
                std::lock_guard<std::mutex> lk(process_mutex_);
                last_exit_code_        = code;
                last_exit_description_ = description;
            }

            bool should_halt = false;
            if (!IsCleanSingletonExit(code)) {
                std::lock_guard<std::mutex> lk(process_mutex_);
                if (uptime_ms < kFastExitThresholdMs) {
                    consecutive_fast_exits_++;
                    Log("[host-supervisor] fast exit count: %d/%d",
                        consecutive_fast_exits_, kCircuitBreakerThreshold);
                } else {
                    consecutive_fast_exits_ = 0;
                }
                if (consecutive_fast_exits_ >= kCircuitBreakerThreshold) {
                    halted_     = true;
                    should_halt = true;
                }
            } else {
                Log("[host-supervisor] clean singleton exit (code=%lu); "
                    "skipping fast-exit counter",
                    static_cast<unsigned long>(code));
            }

            if (should_halt) {
                if (description.empty()) {
                    Log("[host-supervisor] CIRCUIT BREAKER: %d consecutive fast "
                        "exits, halting respawn. Last exit code: 0x%08lx",
                        kCircuitBreakerThreshold,
                        static_cast<unsigned long>(code));
                } else {
                    Log("[host-supervisor] CIRCUIT BREAKER: %d consecutive fast "
                        "exits, halting respawn. Last exit code 0x%08lx -- %s",
                        kCircuitBreakerThreshold,
                        static_cast<unsigned long>(code),
                        description.c_str());
                }
                continue;
            }

            int delay_ms = IsCleanSingletonExit(code) ? 0 : backoff_ms;
            if (delay_ms > 0) {
                Log("[host-supervisor] process exited (code=%lu); "
                    "restarting in %d ms",
                    static_cast<unsigned long>(code), delay_ms);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            }

            if (stop_requested_.load(std::memory_order_acquire)) break;

            if (Spawn()) {
                backoff_ms = kBackoffStartMs;
            } else {
                backoff_ms = std::min(backoff_ms * 2, kBackoffMaxMs);
            }
        }
        // else WAIT_TIMEOUT: process still alive, loop again and let
        // OnHostReady() retry any queued message.
    }

    Log("[host-supervisor] monitor thread exiting");
}

} // namespace openvr_pair::common
