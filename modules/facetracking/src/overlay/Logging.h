#pragma once

#include <cstdio>
#include <ctime>

extern FILE *FtLogFile;

void FtOpenLogFile();
tm   FtTimeForLog();
void FtLogFlush();

// Overlay-side face-tracking log macro. The "OVL" suffix mirrors the
// driver-side FT_LOG_DRV so log lines identify their origin clearly.
#ifndef FT_LOG_OVL
#define FT_LOG_OVL(fmt, ...) do { \
    tm _t = FtTimeForLog(); \
    fprintf(FtLogFile, "[%02d:%02d:%02d] [ft/ovl] " fmt "\n", \
        _t.tm_hour, _t.tm_min, _t.tm_sec, ##__VA_ARGS__); \
    FtLogFlush(); \
} while (0)
#endif
