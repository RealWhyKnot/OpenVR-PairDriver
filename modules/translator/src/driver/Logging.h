#pragma once

#include <cstdio>

// Open the per-session log file (called once in Init). Subsequent TR_LOG_DRV
// calls append to it. Noop if already open.
void TrDrvOpenLogFile();
void TrLogFlushDrv();

// printf-style logging macro. The format string is a literal; arguments are
// forwarded to snprintf and written to the driver log. The trailing (0) is a
// sentinel so the compiler sees at least one non-format argument and can apply
// the format-string attribute on MSVC without warning.
#define TR_LOG_DRV(fmt, ...) TrDrvLog((fmt), ##__VA_ARGS__)

void TrDrvLog(const char *fmt, ...);
