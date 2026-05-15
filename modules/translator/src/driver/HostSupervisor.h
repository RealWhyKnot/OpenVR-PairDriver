#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace translator {

// Spawns and supervises WKOpenVR.TranslatorHost.exe.
//
// Lifecycle: Start() launches the host process; the background monitor thread
// watches for unexpected exit and restarts with exponential backoff (cap 30 s).
// Stop() terminates the host and joins the thread.
class HostSupervisor
{
public:
    explicit HostSupervisor(const std::string &host_exe_path);
    ~HostSupervisor();

    bool Start();
    void Stop();
    void Restart();
    bool IsRunning() const;

    // True if the circuit breaker has tripped (5 consecutive fast exits).
    // Cleared by Stop() / Start() so a redeployment attempt is not blocked.
    bool IsHalted() const;

private:
    std::string       host_exe_path_;
    std::atomic<bool> stop_requested_{ false };
    std::atomic<bool> running_{ false };

    // process_handle_ is read/written by both MonitorLoop and Restart/Kill;
    // all accesses must hold process_mutex_.
    mutable std::mutex process_mutex_;
    HANDLE process_handle_ = INVALID_HANDLE_VALUE;

    std::thread monitor_thread_;

    // Circuit breaker: counts consecutive exits within kFastExitThresholdMs.
    static constexpr int  kFastExitThresholdMs     = 2000;
    static constexpr int  kCircuitBreakerThreshold  = 5;
    int  consecutive_fast_exits_ = 0;
    bool halted_                 = false;

    // True if the host's control pipe is responsive within timeout_ms.
    bool CanConnectToHost(int timeout_ms) const;

    // True for clean singleton/pipe-busy exits; these must not count toward
    // the crash circuit-breaker.
    static bool IsCleanSingletonExit(DWORD code);

    bool Spawn();
    void Kill();
    void MonitorLoop();

    // True if process_handle_ was NOT spawned by this instance (attached to
    // an existing host from a prior session).
    bool attached_to_existing_ = false;
};

} // namespace translator
