#define _CRT_SECURE_NO_DEPRECATE
#include "Logging.h"

#include "LogPaths.h"

#include <chrono>

FILE *LogFile;

void OpenLogFile()
{
	// Prefer the LocalAppDataLow path so the user's diagnostic flow ("diff
	// overlay log + driver log side-by-side") works without having to hunt
	// for the driver log under whatever cwd vrserver happened to inherit
	// from Steam (typically the Steam install dir, sometimes non-writable).
	std::wstring path = openvr_pair::common::TimestampedLogPath(L"driver_log");
	if (!path.empty()) {
		// _wfopen takes a wide path, which lets us cope with usernames
		// containing non-ASCII characters that fopen would mangle.
		LogFile = _wfopen(path.c_str(), L"a");
		if (LogFile) return;
	}

	// Fallback: legacy behavior. Better than nothing if SHGetKnownFolderPath
	// isn't available or AppDataLow isn't writable.
	LogFile = fopen("wkopenvr_driver.log", "a");
	if (!LogFile) {
		LogFile = stderr;
	}
}

tm TimeForLog()
{
	auto now = std::chrono::system_clock::now();
	auto nowTime = std::chrono::system_clock::to_time_t(now);
	tm value;
	auto tm = localtime_s(&value, &nowTime);
	return value;
}

void LogFlush()
{
	fflush(LogFile);
}
