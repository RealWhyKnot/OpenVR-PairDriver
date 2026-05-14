#define _CRT_SECURE_NO_DEPRECATE
#include "Logging.h"

#include "LogPaths.h"

#include <chrono>

FILE *OrDrvLogFile = stderr;

void OrDrvOpenLogFile()
{
    std::wstring path = openvr_pair::common::TimestampedLogPath(L"oscrouter_log");
    if (!path.empty()) {
        FILE *f = _wfopen(path.c_str(), L"a");
        if (f) { OrDrvLogFile = f; return; }
    }
    FILE *f = fopen("oscrouter_drv.log", "a");
    OrDrvLogFile = f ? f : stderr;
}

tm OrDrvTimeForLog()
{
    auto now     = std::chrono::system_clock::now();
    auto nowTime = std::chrono::system_clock::to_time_t(now);
    tm value{};
    localtime_s(&value, &nowTime);
    return value;
}

void OrDrvLogFlush()
{
    fflush(OrDrvLogFile);
}
