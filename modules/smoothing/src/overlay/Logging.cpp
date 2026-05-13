#define _CRT_SECURE_NO_DEPRECATE
#include "Logging.h"

#include <chrono>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>

namespace smoothing::logging {

FILE *LogFile = nullptr;

namespace {

// Drop any smoothing_log.*.txt older than 24 hours.
void ClearOldLogs(const std::wstring &dir)
{
	std::wstring search = dir + L"\\smoothing_log.*.txt";
	WIN32_FIND_DATAW find_data{};

	SYSTEMTIME stNow{};
	FILETIME   ftNow{};
	GetSystemTime(&stNow);
	SystemTimeToFileTime(&stNow, &ftNow);

	ULARGE_INTEGER nowQ;
	nowQ.HighPart = ftNow.dwHighDateTime;
	nowQ.LowPart  = ftNow.dwLowDateTime;
	const uint64_t cutoff = nowQ.QuadPart - (uint64_t)(24LL * 3600LL * 10LL * 1000LL * 1000LL);

	HANDLE h = FindFirstFileW(search.c_str(), &find_data);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		ULARGE_INTEGER fileQ;
		fileQ.HighPart = find_data.ftLastWriteTime.dwHighDateTime;
		fileQ.LowPart  = find_data.ftLastWriteTime.dwLowDateTime;
		if (fileQ.QuadPart < cutoff) {
			std::wstring path = dir + L"\\" + find_data.cFileName;
			DeleteFileW(path.c_str());
		}
	} while (FindNextFileW(h, &find_data));
	FindClose(h);
}

// %LocalAppDataLow%\WKOpenVR\Logs\smoothing_log.<date>T<time>.txt
std::wstring BuildLogPath()
{
	PWSTR rootRaw = nullptr;
	if (S_OK != SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, nullptr, &rootRaw)) {
		if (rootRaw) CoTaskMemFree(rootRaw);
		return {};
	}
	std::wstring root(rootRaw);
	CoTaskMemFree(rootRaw);

	std::wstring dir = root + L"\\WKOpenVR";
	if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return {};
	dir += L"\\Logs";
	if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return {};

	ClearOldLogs(dir);

	SYSTEMTIME now{};
	GetSystemTime(&now);

	const int dateLen = GetDateFormatEx(LOCALE_NAME_INVARIANT, 0, &now, L"yyyy-MM-dd", nullptr, 0, nullptr);
	const int timeLen = GetTimeFormatEx(LOCALE_NAME_INVARIANT, 0, &now, L"HH-mm-ss", nullptr, 0);
	if (dateLen <= 0 || timeLen <= 0) return {};

	std::vector<wchar_t> date(dateLen);
	std::vector<wchar_t> time(timeLen);
	if (!GetDateFormatEx(LOCALE_NAME_INVARIANT, 0, &now, L"yyyy-MM-dd", date.data(), dateLen, nullptr)) return {};
	if (!GetTimeFormatEx(LOCALE_NAME_INVARIANT, 0, &now, L"HH-mm-ss", time.data(), timeLen)) return {};

	std::wstring path = dir + L"\\smoothing_log." + date.data() + L"T" + time.data() + L".txt";
	return path;
}

} // namespace

void OpenLogFile()
{
	if (LogFile) return; // already open
	std::wstring path = BuildLogPath();
	if (!path.empty()) {
		LogFile = _wfopen(path.c_str(), L"a");
		if (LogFile) return;
	}
	LogFile = fopen("openvr_smoothing.log", "a");
	if (!LogFile) {
		LogFile = stderr;
	}
}

tm TimeForLog()
{
	auto now = std::chrono::system_clock::now();
	auto nowTime = std::chrono::system_clock::to_time_t(now);
	tm value;
	localtime_s(&value, &nowTime);
	return value;
}

void LogFlush()
{
	if (LogFile) fflush(LogFile);
}

} // namespace smoothing::logging
