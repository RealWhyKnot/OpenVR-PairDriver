#define _CRT_SECURE_NO_DEPRECATE
#include "Logging.h"

#include <atomic>
#include <chrono>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>

// Initialise to stderr so any FT_LOG_OVL call before FtOpenLogFile() runs
// degrades to a stderr write rather than dereferencing a null FILE*. The
// real log destination is set inside FtOpenLogFile() once the rotation
// directory is ready.
FILE *FtLogFile = stderr;

// Verbose flag. Default false; set true by the Logs tab checkbox and forced
// true on dev-channel builds (see LogsSection.cpp).
std::atomic<bool> FtOverlayVerbose{ false };

namespace {

// Remove any facetracking_log.*.txt older than 24 hours.
void ClearOldLogs(const std::wstring &dir)
{
    std::wstring search = dir + L"\\facetracking_log.*.txt";
    WIN32_FIND_DATAW fd{};

    SYSTEMTIME stNow{};
    FILETIME   ftNow{};
    GetSystemTime(&stNow);
    SystemTimeToFileTime(&stNow, &ftNow);

    ULARGE_INTEGER nowQ;
    nowQ.HighPart = ftNow.dwHighDateTime;
    nowQ.LowPart  = ftNow.dwLowDateTime;
    const uint64_t cutoff = nowQ.QuadPart - (uint64_t)(24LL * 3600LL * 10LL * 1000LL * 1000LL);

    HANDLE h = FindFirstFileW(search.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        ULARGE_INTEGER fq;
        fq.HighPart = fd.ftLastWriteTime.dwHighDateTime;
        fq.LowPart  = fd.ftLastWriteTime.dwLowDateTime;
        if (fq.QuadPart < cutoff) {
            std::wstring path = dir + L"\\" + fd.cFileName;
            DeleteFileW(path.c_str());
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// %LocalAppDataLow%\OpenVR-Pair\Logs\facetracking_log.<date>T<time>.txt
std::wstring BuildLogPath()
{
    PWSTR rootRaw = nullptr;
    if (S_OK != SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, nullptr, &rootRaw)) {
        if (rootRaw) CoTaskMemFree(rootRaw);
        return {};
    }
    std::wstring root(rootRaw);
    CoTaskMemFree(rootRaw);

    std::wstring dir = root + L"\\OpenVR-Pair";
    if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return {};
    dir += L"\\Logs";
    if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return {};

    ClearOldLogs(dir);

    SYSTEMTIME now{};
    GetSystemTime(&now);

    const int dateLen = GetDateFormatEx(LOCALE_NAME_INVARIANT, 0, &now, L"yyyy-MM-dd", nullptr, 0, nullptr);
    const int timeLen = GetTimeFormatEx(LOCALE_NAME_INVARIANT, 0, &now, L"HH-mm-ss", nullptr, 0);
    if (dateLen <= 0 || timeLen <= 0) return {};

    std::vector<wchar_t> dateBuf(dateLen);
    std::vector<wchar_t> timeBuf(timeLen);
    if (!GetDateFormatEx(LOCALE_NAME_INVARIANT, 0, &now, L"yyyy-MM-dd", dateBuf.data(), dateLen, nullptr)) return {};
    if (!GetTimeFormatEx(LOCALE_NAME_INVARIANT, 0, &now, L"HH-mm-ss", timeBuf.data(), timeLen)) return {};

    return dir + L"\\facetracking_log." + dateBuf.data() + L"T" + timeBuf.data() + L".txt";
}

} // namespace

void FtOpenLogFile()
{
    std::wstring path = BuildLogPath();
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
