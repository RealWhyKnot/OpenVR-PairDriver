#pragma once

#include <string>

// Writes a host_status.json file that the overlay polls to show live state.
// File location: %LocalAppDataLow%\WKOpenVR\translator\host_status.json
// Written atomically via a .tmp + rename. Refreshed at most once per second.
class HostStatus
{
public:
    enum class State : int
    {
        Idle       = 0,
        Listening  = 1,
        Transcribing = 2,
        Translating  = 3,
        Sending      = 4,
        Error        = 5,
    };

    HostStatus();

    void SetState(State s) noexcept;
    void SetMicName(const std::string &name);
    void SetLastTranscript(const std::string &t);
    void SetLastTranslation(const std::string &t);
    void SetLastError(const std::string &e);
    void IncrementPacketsSent() noexcept;

    // Write the JSON file to disk if at least 1 s has elapsed since the
    // last write. Call periodically from the main loop.
    void MaybeFlush();

    // Force a flush regardless of the timer (call on shutdown).
    void Flush();

private:
    std::wstring status_path_;
    State        state_            = State::Idle;
    std::string  mic_name_;
    std::string  last_transcript_;
    std::string  last_translation_;
    std::string  last_error_;
    long long    packets_sent_     = 0;

    void WritePath();
    void DoFlush();

    // Timing.
    long long last_flush_tick_ = 0; // GetTickCount64
};
