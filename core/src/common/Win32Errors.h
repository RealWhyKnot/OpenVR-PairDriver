#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace openvr_pair::common {

std::string FormatWin32Error(DWORD error);

} // namespace openvr_pair::common
