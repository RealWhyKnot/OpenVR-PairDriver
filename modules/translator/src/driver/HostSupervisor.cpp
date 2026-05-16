#define _CRT_SECURE_NO_DEPRECATE
#include "HostSupervisor.h"
#include "Logging.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace {

// Return a human-readable label + diagnostic hint for known host exit codes.
// The returned pointer is a string literal (no allocation).
static const char *DescribeExitCode(DWORD code)
{
    // Windows loader failures.
    if (code == 0xC0000135) return "STATUS_DLL_NOT_FOUND -- a hard-import DLL is missing; "
        "check openvr_api.dll and VC runtime staging beside WKOpenVR.TranslatorHost.exe";
    if (code == 0xC0000005) return "STATUS_ACCESS_VIOLATION -- crash inside native lib; "
        "see translator_host_crash_<pid>.txt in %LocalAppDataLow%\\WKOpenVR\\Logs";
    if (code == 0xC000007B) return "STATUS_INVALID_IMAGE_FORMAT -- 32/64-bit mismatch";
    if (code == 0xC0000142) return "STATUS_DLL_INIT_FAILED -- DLL DllMain returned FALSE; "
        "a required dependency of a loaded DLL is itself missing";

    // Our delay-load failure range: 0xCEE0DC00 | (err & 0xFF).
    if ((code & 0xFFFFFF00u) == 0xCEE0DC00u) {
        DWORD winerr = code & 0xFF;
        if (winerr == 126) {
            return "delay-load failed: ERROR_MOD_NOT_FOUND (126) -- "
                "optional translator runtime DLL missing; install the relevant Translator pack; "
                "see translator_host_crash_<pid>.txt for the exact DLL name";
        }
        return "delay-load failed -- optional translator runtime DLL missing; "
            "see translator_host_crash_<pid>.txt for details";
    }

    // Clean exits from the host's singleton check.
    if (code == 3) return "clean singleton exit (another host already running)";
    if (code == 4) return "clean pipe-busy exit (another host owns the control pipe)";

    // Normal exit.
    if (code == 0) return "normal exit (code 0)";

    return "unknown";
}

} // anonymous namespace

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
    {
        std::lock_guard<std::mutex> lk(command_mutex_);
        if (has_pending_command_) command_sent_ = false;
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

void HostSupervisor::SetHostConfigCommand(const std::string &command)
{
    {
        std::lock_guard<std::mutex> lk(command_mutex_);
        pending_command_     = command;
        has_pending_command_ = !command.empty();
        command_sent_        = false;
    }
    if (!command.empty()) TrySendCommand(command);
}

bool HostSupervisor::IsHalted() const
{
    std::lock_guard<std::mutex> lk(process_mutex_);
    return halted_;
}

uint32_t HostSupervisor::LastExitCode() const
{
    std::lock_guard<std::mutex> lk(process_mutex_);
    return last_exit_code_;
}

std::string HostSupervisor::LastExitDescription() const
{
    std::lock_guard<std::mutex> lk(process_mutex_);
    return last_exit_description_;
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
                if (!wpath.empty() && wpath.back() == L'\0') wpath.pop_back();
                std::wstring commandLine = L"\"" + wpath + L"\"";

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

    if (!(already_running || attached || spawned)) return false;

    std::string command_to_send;
    bool should_send = false;
    {
        std::lock_guard<std::mutex> lk(command_mutex_);
        if (has_pending_command_ && !command_sent_) {
            command_to_send = pending_command_;
            should_send = true;
        }
    }
    if (should_send) TrySendCommand(command_to_send);
    return true;
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
                    {
                        std::lock_guard<std::mutex> lk(command_mutex_);
                        if (has_pending_command_) command_sent_ = false;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
                    if (!stop_requested_.load(std::memory_order_acquire)) {
                        if (Spawn()) backoff_ms = 1000;
                        else backoff_ms = std::min(backoff_ms * 2, kMaxBackoffMs);
                    }
                } else {
                    RetryPendingCommand();
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            continue;
        }

        // Record spawn time to detect fast exits.
        auto spawn_time = std::chrono::steady_clock::now();

        RetryPendingCommand();

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
            {
                std::lock_guard<std::mutex> lk(command_mutex_);
                if (has_pending_command_) command_sent_ = false;
            }

            auto now = std::chrono::steady_clock::now();
            long long uptime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - spawn_time).count();

            TR_LOG_DRV("[translator-supervisor] host process exited "
                "code=0x%08lx uptime_ms=%lld -- %s",
                (unsigned long)code, uptime_ms, DescribeExitCode(code));
            {
                std::lock_guard<std::mutex> lk(process_mutex_);
                last_exit_code_ = code;
                last_exit_description_ = DescribeExitCode(code);
            }

            bool should_halt = false;
            if (!IsCleanSingletonExit(code)) {
                std::lock_guard<std::mutex> lk(process_mutex_);
                if (uptime_ms < kFastExitThresholdMs) {
                    consecutive_fast_exits_++;
                    TR_LOG_DRV("[translator-supervisor] fast exit count: %d/%d",
                        consecutive_fast_exits_, kCircuitBreakerThreshold);
                } else {
                    consecutive_fast_exits_ = 0;
                }
                if (consecutive_fast_exits_ >= kCircuitBreakerThreshold) {
                    halted_     = true;
                    should_halt = true;
                }
            } else {
                TR_LOG_DRV("[translator-supervisor] clean exit (code=%lu); "
                    "skipping fast-exit counter", (unsigned long)code);
            }

            if (should_halt) {
                TR_LOG_DRV("[translator-supervisor] CIRCUIT BREAKER tripped: "
                    "%d consecutive fast exits; halting respawn. "
                    "Last exit code=0x%08lx -- %s. "
                    "Fix the missing DLLs and click Restart host in the overlay.",
                    kCircuitBreakerThreshold, (unsigned long)code,
                    DescribeExitCode(code));
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

bool HostSupervisor::TrySendCommand(const std::string &command)
{
    HANDLE h = CreateFileA(
        TR_HOST_CONTROL_PIPE_NAME,
        GENERIC_WRITE, 0, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    BOOL ok = WriteFile(h, command.data(), (DWORD)command.size(), &written, nullptr);
    CloseHandle(h);

    if (ok && written == (DWORD)command.size()) {
        std::lock_guard<std::mutex> lk(command_mutex_);
        if (pending_command_ == command) command_sent_ = true;
        TR_LOG_DRV("[host] sent translator host config (%lu bytes)", (unsigned long)written);
        return true;
    }
    return false;
}

void HostSupervisor::RetryPendingCommand()
{
    std::string command;
    bool should_send = false;
    {
        std::lock_guard<std::mutex> lk(command_mutex_);
        if (has_pending_command_ && !command_sent_) {
            command = pending_command_;
            should_send = true;
        }
    }
    if (should_send) TrySendCommand(command);
}

} // namespace translator
