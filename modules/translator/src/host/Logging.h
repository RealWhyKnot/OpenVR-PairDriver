#pragma once

#include <string>

// Opens a timestamped log file under %LocalAppDataLow%\WKOpenVR\Logs\.
// Safe to call multiple times; subsequent calls are noops.
void TranslatorHostOpenLogFile();
void TranslatorHostFlushLog();

void TranslatorHostLog(const char *fmt, ...);

#define TH_LOG(fmt, ...) TranslatorHostLog((fmt), ##__VA_ARGS__)
