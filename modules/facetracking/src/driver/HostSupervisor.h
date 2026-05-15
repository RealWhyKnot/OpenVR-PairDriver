#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace facetracking {

// Spawns and supervises OpenVRPair.FaceModuleHost.exe.
//
// Lifecycle: Start() launches the host process; the supervisor's background
// thread monitors it and restarts on unexpected exit (with exponential backoff
// capped at 30 s).  Stop() terminates the host and joins the monitor thread.
//
// Control pipe: SetActiveModuleUuid() writes a small command to
// \\.\pipe\OpenVR-FaceTracking.host (the host's named-pipe server).
// If the pipe is not yet connected the uuid is queued and sent on the next
// successful reconnect.
class HostSupervisor
{
public:
    explicit HostSupervisor(const std::string &host_exe_path);
    ~HostSupervisor();

    // Launch the host process and start the monitor thread.
    bool Start();

    // Terminate the host process and stop the monitor thread.
    void Stop();

    // Terminate the host process and restart it. The monitor thread continues
    // running; after Stop()+Start() the supervisor will respawn the host and
    // resume its usual crash-restart loop.
    void Restart();

    // True if the host process is currently alive.
    bool IsRunning() const;

    // True if the circuit breaker has tripped (5 consecutive fast exits).
    // Cleared by Stop() / Start() so a redeployment attempt is not blocked.
    bool IsHalted() const;

    // Send the active-module uuid to the host via the control pipe.
    // If the pipe isn't up the uuid is queued for the next reconnect.
    void SetActiveModuleUuid(const char *uuid);

private:
    std::string       host_exe_path_;
    std::atomic<bool> stop_requested_{ false };
    std::atomic<bool> running_{ false };

    // process_handle_ is read/written by both MonitorLoop and Restart/Kill;
    // all accesses must hold process_mutex_.
    mutable std::mutex process_mutex_;
    HANDLE process_handle_ = INVALID_HANDLE_VALUE;
    HANDLE process_thread_ = INVALID_HANDLE_VALUE;  // not used but kept for CloseHandle

    std::thread monitor_thread_;

    // Circuit breaker: counts consecutive exits within kFastExitThresholdMs.
    static constexpr int  kFastExitThresholdMs    = 2000;
    static constexpr int  kCircuitBreakerThreshold = 5;
    int  consecutive_fast_exits_ = 0;
    bool halted_                 = false;

    // Pending module-uuid to send on next pipe connection.
    std::mutex  uuid_mutex_;
    std::string pending_uuid_;
    bool        uuid_sent_ = false;

    // True if the host's control pipe is responsive within timeout_ms.
    bool CanConnectToHost(int timeout_ms) const;

    // Classify an exit code: returns true for clean singleton/pipe exits
    // that should NOT increment the fast-exit counter.
    static bool IsCleanSingletonExit(DWORD code);

    // Spawn the exe; returns true on success.
    bool Spawn();

    // Kill the current process if it is alive.  Caller must NOT hold process_mutex_.
    void Kill();

    // Monitor-thread body.
    void MonitorLoop();

    // Try to push `uuid` over the named-pipe control channel.
    // Returns true if delivered; leaves uuid in pending on failure.
    bool TrySendUuid(const std::string &uuid);

    // True if process_handle_ was NOT spawned by this supervisor instance
    // (i.e. we attached to an existing host rather than spawning it).
    bool attached_to_existing_ = false;
};

} // namespace facetracking
