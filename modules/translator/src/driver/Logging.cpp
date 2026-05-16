#define _CRT_SECURE_NO_DEPRECATE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>

#include "Logging.h"
#include "DebugLogging.h"
#include "LogPaths.h"

#include <cerrno>
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
    int openErrno = 0;
    if (!path.empty()) {
        g_logFile = _wfopen(path.c_str(), L"w");
        if (g_logFile) return;
        openErrno = errno;
    }

    g_logFile = fopen("translator_drv.log", "a");
    if (!g_logFile) g_logFile = stderr;
    if (g_logFile) {
        fprintf(g_logFile,
            "[log-open] translator driver log using fallback path; primary_errno=%d primary_path_empty=%d\n",
            openErrno, path.empty() ? 1 : 0);
        fflush(g_logFile);
    }
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
        if (!path.empty()) g_logFile = _wfopen(path.c_str(), L"w");
        if (!g_logFile) g_logFile = fopen("translator_drv.log", "a");
        if (!g_logFile) g_logFile = stderr;
    }
    if (g_logFile) {
        fputs(buf, g_logFile);
        fputs("\n", g_logFile);
        fflush(g_logFile);
    }
}
