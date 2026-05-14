#include "Win32Paths.h"

#include <filesystem>

#include <objbase.h>
#include <shlobj.h>

namespace openvr_pair::common {
namespace {

std::wstring JoinPath(std::wstring_view left, std::wstring_view right)
{
	if (left.empty()) return std::wstring(right.data(), right.size());
	if (right.empty()) return std::wstring(left.data(), left.size());

	std::wstring out(left.data(), left.size());
	if (out.back() != L'\\' && out.back() != L'/') {
		out.push_back(L'\\');
	}
	out.append(right);
	return out;
}

} // namespace

std::wstring LocalAppDataLowPath()
{
	PWSTR raw = nullptr;
	if (S_OK != SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, nullptr, &raw)) {
		if (raw) CoTaskMemFree(raw);
		return {};
	}

	std::wstring path(raw);
	CoTaskMemFree(raw);
	return path;
}

bool EnsureDirectory(const std::wstring &path)
{
	if (path.empty()) return false;
	std::error_code ec;
	return std::filesystem::create_directories(path, ec)
		|| std::filesystem::is_directory(path, ec);
}

std::wstring WkOpenVrRootPath(bool create)
{
	std::wstring root = LocalAppDataLowPath();
	if (root.empty()) return {};

	root = JoinPath(root, L"WKOpenVR");
	if (create && !EnsureDirectory(root)) return {};
	return root;
}

std::wstring WkOpenVrSubdirectoryPath(std::wstring_view relative, bool create)
{
	std::wstring root = WkOpenVrRootPath(create);
	if (root.empty()) return {};

	std::wstring path = JoinPath(root, relative);
	if (create && !EnsureDirectory(path)) return {};
	return path;
}

std::wstring WkOpenVrLogsPath(bool create)
{
	return WkOpenVrSubdirectoryPath(L"Logs", create);
}

int64_t FileLastWriteTime(const std::wstring &path)
{
	WIN32_FILE_ATTRIBUTE_DATA data{};
	if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
		return 0;
	}

	ULARGE_INTEGER stamp{};
	stamp.LowPart = data.ftLastWriteTime.dwLowDateTime;
	stamp.HighPart = data.ftLastWriteTime.dwHighDateTime;
	return static_cast<int64_t>(stamp.QuadPart);
}

} // namespace openvr_pair::common
