#include "ShellContext.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>

#include <objbase.h>

#include <utility>
#include <string>
#include <vector>
#include <sstream>

namespace openvr_pair::overlay {
namespace {

struct PendingToggle
{
	std::string flagFileName;
	bool wantPresent;
	HANDLE process;
};

std::vector<PendingToggle> g_pendingToggles;

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

	SHELLEXECUTEINFOW sei{};
	sei.cbSize = sizeof(sei);
	sei.fMask = SEE_MASK_NOCLOSEPROCESS;
	sei.lpVerb = L"runas";
	sei.lpFile = L"powershell.exe";
	sei.lpParameters = args.c_str();
	sei.nShow = SW_HIDE;
	if (!ShellExecuteExW(&sei) || sei.hProcess == nullptr) {
		// User dismissed the consent prompt before it even ran, or the
		// shell refused to launch the helper. Either way there is no
		// process to wait on.
		DWORD err = GetLastError();
		if (err == 0) {
			SetStatus("Module change cancelled.");
		} else {
			std::ostringstream oss;
			oss << "Module change was not started (admin helper error 0x" << std::hex << std::uppercase << err << ")";
			SetStatus(oss.str());
		}
		return false;
	}

	g_pendingToggles.push_back({flagFileName, present, sei.hProcess});
	SetStatus("Module change queued. SteamVR will pick up the new state the next time it loads the driver.");
	return true;
}

bool ShellContext::IsTogglePending(const char *flagFileName) const
{
	if (!flagFileName) return false;
	for (const auto &entry : g_pendingToggles) {
		if (entry.flagFileName == flagFileName) return true;
	}
	return false;
}

void ShellContext::TickToggles()
{
	for (auto it = g_pendingToggles.begin(); it != g_pendingToggles.end();) {
		if (WaitForSingleObject(it->process, 0) != WAIT_OBJECT_0) {
			++it;
			continue;
		}
		DWORD exitCode = 0;
		if (!GetExitCodeProcess(it->process, &exitCode)) {
			exitCode = 1;
		}
		const bool present = IsFlagPresent(it->flagFileName.c_str());
		if (exitCode == 0 && present == it->wantPresent) {
			SetStatus("Module change applied. SteamVR will pick up the new state the next time it loads the driver.");
		} else if (present == it->wantPresent) {
			std::ostringstream oss;
			if (exitCode == 0) {
				oss << (it->wantPresent ? "Enable did not apply: flag file was not created." :
				                         "Disable did not apply: flag file was still present.");
			} else {
				oss << (it->wantPresent ? "Enable completed, but helper exit was non-zero (0x" :
				                         "Disable completed, but helper exit was non-zero (0x");
				oss << std::hex << std::uppercase << exitCode << ").";
			}
			oss << " Check that the helper process could write Program Files and that SteamVR is not holding"
			    << (it->wantPresent ? " the driver folder open." : " the module state files.");
			SetStatus(oss.str());
		} else {
			SetStatus(std::string(it->wantPresent
			                      ? "Enable did not apply -- helper finished but state is still disabled."
			                      : "Disable did not apply -- helper finished but state is still enabled."));
		}
		CloseHandle(it->process);
		it = g_pendingToggles.erase(it);
	}
}

void ShellContext::SetStatus(std::string message)
{
	status = std::move(message);
}

} // namespace openvr_pair::overlay
