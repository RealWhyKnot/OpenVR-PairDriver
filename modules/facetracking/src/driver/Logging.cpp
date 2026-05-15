#define _CRT_SECURE_NO_DEPRECATE
#include "Logging.h"

#include "DebugLogging.h"
#include "LogPaths.h"

#include <chrono>

FILE *FtDrvLogFile = nullptr;

void FtDrvOpenLogFile()
{
    if (!openvr_pair::common::IsDebugLoggingEnabled()) return;
    if (FtDrvLogFile) return;

    std::wstring path = openvr_pair::common::TimestampedLogPath(L"facetracking_log");
    if (!path.empty()) {
        FILE *f = _wfopen(path.c_str(), L"a");
        if (f) { FtDrvLogFile = f; return; }
    }
    FILE *f = fopen("facetracking_drv.log", "a");
    FtDrvLogFile = f ? f : stderr;
}

bool FtDrvEnsureLogFileOpen()
{
    if (!openvr_pair::common::IsDebugLoggingEnabled()) return false;
    if (!FtDrvLogFile) FtDrvOpenLogFile();
    return FtDrvLogFile != nullptr;
}

tm FtDrvTimeForLog()
{
    auto now     = std::chrono::system_clock::now();
    auto nowTime = std::chrono::system_clock::to_time_t(now);
    tm value{};
    localtime_s(&value, &nowTime);
    return value;
}

void FtDrvLogFlush()
{
    if (FtDrvLogFile) fflush(FtDrvLogFile);
}
