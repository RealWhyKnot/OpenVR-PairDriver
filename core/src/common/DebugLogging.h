#pragma once

#include <string>
#include <string_view>

namespace openvr_pair::common {

bool BuildChannelForcesDebugLogging(std::string_view channel);
bool IsDebugLoggingForcedOn();
bool IsDebugLoggingEnabled();
bool SetDebugLoggingEnabled(bool enabled);
std::wstring DebugLoggingFlagPath(bool createRoot = true);

} // namespace openvr_pair::common
