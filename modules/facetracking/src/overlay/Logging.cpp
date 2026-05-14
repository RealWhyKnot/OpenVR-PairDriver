#define _CRT_SECURE_NO_DEPRECATE
#include "Logging.h"

#include "LogPaths.h"

#include <atomic>
#include <chrono>

// Initialise to stderr so any FT_LOG_OVL call before FtOpenLogFile() runs
// degrades to a stderr write rather than dereferencing a null FILE*. The
// real log destination is set inside FtOpenLogFile() once the rotation
// directory is ready.
FILE *FtLogFile = stderr;

// Verbose flag. Default false; set true by the Logs tab checkbox and forced
// true on dev-channel builds (see LogsSection.cpp).
std::atomic<bool> FtOverlayVerbose{ false };

void FtOpenLogFile()
{
    std::wstring path = openvr_pair::common::TimestampedLogPath(L"facetracking_log");
    if (!path.empty()) {
        FtLogFile = _wfopen(path.c_str(), L"a");
        if (FtLogFile) return;
    }
    FtLogFile = fopen("openvr_facetracking.log", "a");
    if (!FtLogFile) FtLogFile = stderr;
}

tm FtTimeForLog()
{
    auto now     = std::chrono::system_clock::now();
    auto nowTime = std::chrono::system_clock::to_time_t(now);
    tm   val{};
    localtime_s(&val, &nowTime);
    return val;
}

void FtLogFlush()
{
    fflush(FtLogFile);
}
