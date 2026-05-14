#define _CRT_SECURE_NO_DEPRECATE
#include "Logging.h"

#include "LogPaths.h"

#include <chrono>

FILE *LogFile;

void OpenLogFile()
{
	std::wstring path = openvr_pair::common::TimestampedLogPath(L"overlay_log");
	if (!path.empty()) {
		LogFile = _wfopen(path.c_str(), L"a");
		if (LogFile) return;
	}
	LogFile = fopen("openvr_inputhealth.log", "a");
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
	fflush(LogFile);
}
