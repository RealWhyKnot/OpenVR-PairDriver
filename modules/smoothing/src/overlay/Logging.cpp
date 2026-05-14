#define _CRT_SECURE_NO_DEPRECATE
#include "Logging.h"

#include "LogPaths.h"

#include <chrono>

namespace smoothing::logging {

FILE *LogFile = nullptr;

void OpenLogFile()
{
	if (LogFile) return; // already open
	std::wstring path = openvr_pair::common::TimestampedLogPath(L"smoothing_log");
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
