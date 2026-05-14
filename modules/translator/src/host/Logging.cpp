#define _CRT_SECURE_NO_DEPRECATE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>

#include "Logging.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace {

std::mutex  g_logMutex;
FILE       *g_logFile = nullptr;

// Resolve %LocalAppDataLow%\WKOpenVR\Logs\
std::wstring ResolveLogDir()
{
    PWSTR raw = nullptr;
    if (S_OK != SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, nullptr, &raw)) {
        if (raw) CoTaskMemFree(raw);
        return {};
    }
    std::wstring root(raw);
    CoTaskMemFree(raw);
    root += L"\\WKOpenVR\\Logs";
    CreateDirectoryW(root.c_str(), nullptr);
    return root;
}

} // namespace

void TranslatorHostOpenLogFile()
{
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (g_logFile) return;

    std::wstring dir = ResolveLogDir();
    if (dir.empty()) return;

    SYSTEMTIME st{};
    GetSystemTime(&st);
    wchar_t namebuf[128];
    swprintf_s(namebuf, L"translator_host_log.%04d%02d%02d_%02d%02d%02d.txt",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);

    std::wstring path = dir + L"\\" + namebuf;
    g_logFile = _wfopen(path.c_str(), L"w");
}

void TranslatorHostFlushLog()
{
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (g_logFile) fflush(g_logFile);
}

void TranslatorHostLog(const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    std::lock_guard<std::mutex> lk(g_logMutex);
    if (g_logFile) {
        fputs(buf, g_logFile);
        fputs("\n", g_logFile);
        fflush(g_logFile);
    }
}
