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

private:
    std::string       host_exe_path_;
    std::atomic<bool> stop_requested_{ false };
    std::atomic<bool> running_{ false };

    HANDLE process_handle_ = INVALID_HANDLE_VALUE;

    std::thread monitor_thread_;

    bool Spawn();
    void Kill();
    void MonitorLoop();
};

} // namespace translator
