#pragma once

#include <string>
#include <string_view>

namespace openvr_pair::common {

std::string WideToUtf8(std::wstring_view value);
std::wstring Utf8ToWide(std::string_view value);

} // namespace openvr_pair::common
