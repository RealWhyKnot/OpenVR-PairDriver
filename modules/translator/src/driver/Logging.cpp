#define _CRT_SECURE_NO_DEPRECATE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>

#include "Logging.h"
#include "DebugLogging.h"
#include "LogPaths.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace {

std::mutex  g_logMutex;
FILE       *g_logFile = nullptr;

} // namespace

void TrDrvOpenLogFile()
{
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (!openvr_pair::common::IsDebugLoggingEnabled()) return;
    if (g_logFile) return;

    std::wstring path = openvr_pair::common::TimestampedLogPath(L"translator_drv_log");
    if (path.empty()) return;

    g_logFile = _wfopen(path.c_str(), L"w");
}

void TrLogFlushDrv()
{
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (g_logFile) fflush(g_logFile);
}

void TrDrvLog(const char *fmt, ...)
{
    if (!openvr_pair::common::IsDebugLoggingEnabled()) return;

    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    std::lock_guard<std::mutex> lk(g_logMutex);
    if (!g_logFile) {
        std::wstring path = openvr_pair::common::TimestampedLogPath(L"translator_drv_log");
        if (!path.empty()) {
            g_logFile = _wfopen(path.c_str(), L"w");
        }
    }
    if (g_logFile) {
        fputs(buf, g_logFile);
        fputs("\n", g_logFile);
        fflush(g_logFile);
    }
}
