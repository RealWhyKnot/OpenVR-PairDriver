#pragma once

#include <chrono>
#include <string>

namespace translator {

struct HostStatusSnapshot
{
    bool        valid            = false;
    bool        stale            = false;
    int         host_pid         = 0;
    int         state            = 0;   // HostStatus::State int value
    std::string mic_name;
    std::string last_transcript;
    std::string last_translation;
    std::string last_error;
    long long   packets_sent     = 0;
};

// Polls %LocalAppDataLow%\WKOpenVR\translator\host_status.json.
// Throttled to a stat() every 500 ms; only re-reads on mtime change.
class HostStatusPoller
{
public:
    HostStatusPoller();

    void Tick();

    const HostStatusSnapshot &Snapshot() const noexcept { return snapshot_; }
    const std::string &PathUtf8() const noexcept { return path_utf8_; }

private:
    void ResolvePath();
    void ReadFile();

    std::string path_utf8_;
    std::chrono::steady_clock::time_point last_read_attempt_{};
    std::chrono::steady_clock::time_point last_successful_read_{};
    int64_t last_observed_mtime_ = 0;
    HostStatusSnapshot snapshot_;
};

} // namespace translator
