#include "ShellContext.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>

#include <objbase.h>

#include <utility>

namespace openvr_pair::overlay {
namespace {

std::wstring LocalAppDataLow()
{
	PWSTR root = nullptr;
	if (S_OK != SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, nullptr, &root)) {
		if (root) CoTaskMemFree(root);
		return {};
	}
	std::wstring value(root);
	CoTaskMemFree(root);
	return value;
}

void EnsureDir(const std::wstring &path)
{
	if (!path.empty()) CreateDirectoryW(path.c_str(), nullptr);
}

std::wstring QuotePowerShellString(const std::wstring &value)
{
	std::wstring out = L"'";
	for (wchar_t ch : value) {
		if (ch == L'\'') out += L"''";
		else out += ch;
	}
	out += L"'";
	return out;
}

} // namespace

std::string Narrow(const std::wstring &value)
{
	if (value.empty()) return {};
	int needed = WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), nullptr, 0, nullptr, nullptr);
	std::string out(needed, '\0');
	WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), out.data(), needed, nullptr, nullptr);
	return out;
}

ShellContext CreateShellContext()
{
	ShellContext ctx;
	ctx.installDir = L"C:\\Program Files\\OpenVR-Pair";

	const std::wstring root = LocalAppDataLow();
	if (!root.empty()) {
		std::wstring pairRoot = root + L"\\OpenVR-Pair";
		EnsureDir(pairRoot);
		ctx.profileRoot = pairRoot + L"\\profiles";
		ctx.logRoot = pairRoot + L"\\Logs";
		EnsureDir(ctx.profileRoot);
		EnsureDir(ctx.logRoot);
	}

	std::wstring resources =
		L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\drivers\\01openvrpair\\resources";
	ctx.driverResourceDirs.push_back(resources);
	return ctx;
}

std::wstring ShellContext::FlagPath(const char *flagFileName) const
{
	if (driverResourceDirs.empty() || !flagFileName) return {};
	int needed = MultiByteToWideChar(CP_UTF8, 0, flagFileName, -1, nullptr, 0);
	if (needed <= 0) return {};
	std::wstring flag((size_t)needed - 1, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, flagFileName, -1, flag.data(), needed);
	return driverResourceDirs.front() + L"\\" + flag;
}

bool ShellContext::IsFlagPresent(const char *flagFileName) const
{
	for (const auto &dir : driverResourceDirs) {
		std::wstring path = dir + L"\\";
		int needed = MultiByteToWideChar(CP_UTF8, 0, flagFileName, -1, nullptr, 0);
		if (needed <= 0) continue;
		std::wstring flag((size_t)needed - 1, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, flagFileName, -1, flag.data(), needed);
		path += flag;
		DWORD attr = GetFileAttributesW(path.c_str());
		if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) return true;
	}
	return false;
}

bool ShellContext::SetFlagPresent(const char *flagFileName, bool present)
{
	std::wstring path = FlagPath(flagFileName);
	if (path.empty()) return false;

	std::wstring parent = path.substr(0, path.find_last_of(L"\\/"));
	std::wstring command;
	if (present) {
		command =
			L"New-Item -ItemType Directory -Force -LiteralPath " + QuotePowerShellString(parent) +
			L" | Out-Null; Set-Content -LiteralPath " + QuotePowerShellString(path) +
			L" -Value enabled -NoNewline";
	} else {
		command =
			L"if (Test-Path -LiteralPath " + QuotePowerShellString(path) +
			L") { Remove-Item -LiteralPath " + QuotePowerShellString(path) + L" -Force }";
	}

	std::wstring args = L"-NoProfile -ExecutionPolicy Bypass -Command " + QuotePowerShellString(command);
	HINSTANCE result = ShellExecuteW(nullptr, L"runas", L"powershell.exe", args.c_str(), nullptr, SW_HIDE);
	if ((INT_PTR)result <= 32) {
		SetStatus("Unable to start elevated flag update.");
		return false;
	}
	SetStatus("Module change queued. SteamVR will pick up the new state the next time it loads the driver.");
	return true;
}

void ShellContext::SetStatus(std::string message)
{
	status = std::move(message);
}

} // namespace openvr_pair::overlay
