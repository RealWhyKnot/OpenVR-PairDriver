#include "ShellContext.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>

#include <objbase.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
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
	if (path.empty()) return;
	if (!CreateDirectoryW(path.c_str(), nullptr)) {
		DWORD err = GetLastError();
		if (err != ERROR_ALREADY_EXISTS) {
			// Log to stderr -- subsequent file writes into this directory will
			// fail with opaque errors; surfacing the root cause here makes
			// diagnosis much faster.
			fprintf(stderr,
				"[openvr-pair] EnsureDir: CreateDirectoryW failed (error %lu) for path that may be needed for profile/log writes\n",
				(unsigned long)err);
		}
	}
}

// Returns the directory containing the running exe, without a trailing slash.
// Falls back to an empty string on failure.
std::wstring ExeDir()
{
	wchar_t buf[MAX_PATH + 1] = {};
	DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
	if (len == 0 || len >= MAX_PATH) return {};
	std::wstring path(buf, len);
	auto sep = path.find_last_of(L"\\/");
	return (sep != std::wstring::npos) ? path.substr(0, sep) : path;
}

// Read a REG_SZ value from the given hive/key/value. Returns empty on failure.
std::wstring ReadRegString(HKEY hive, const wchar_t *subkey, const wchar_t *valueName)
{
	HKEY hk = nullptr;
	if (RegOpenKeyExW(hive, subkey, 0, KEY_READ, &hk) != ERROR_SUCCESS) return {};
	DWORD type = 0, size = 0;
	if (RegQueryValueExW(hk, valueName, nullptr, &type, nullptr, &size) != ERROR_SUCCESS
		|| type != REG_SZ || size == 0) {
		RegCloseKey(hk);
		return {};
	}
	std::wstring value(size / sizeof(wchar_t), L'\0');
	if (RegQueryValueExW(hk, valueName, nullptr, nullptr,
		reinterpret_cast<LPBYTE>(value.data()), &size) != ERROR_SUCCESS) {
		RegCloseKey(hk);
		return {};
	}
	RegCloseKey(hk);
	// Strip trailing NUL that RegQueryValueEx includes in `size`
	while (!value.empty() && value.back() == L'\0') value.pop_back();
	return value;
}

// Finds the Steam install root by trying three registry locations in order.
// Returns empty if none are found.
std::wstring FindSteamInstallPath()
{
	// 32-bit view first (most common install location on 64-bit Windows)
	std::wstring p = ReadRegString(HKEY_LOCAL_MACHINE,
		L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
	if (!p.empty()) return p;
	p = ReadRegString(HKEY_LOCAL_MACHINE,
		L"SOFTWARE\\Valve\\Steam", L"InstallPath");
	if (!p.empty()) return p;
	// HKCU fallback (per-user install)
	p = ReadRegString(HKEY_CURRENT_USER,
		L"Software\\Valve\\Steam", L"SteamPath");
	if (!p.empty()) {
		// SteamPath uses forward slashes on some installs; normalise.
		for (wchar_t &ch : p) if (ch == L'/') ch = L'\\';
	}
	return p;
}

// Checks whether `candidate` is the SteamVR library root by testing for the
// SteamVR common directory inside it.
bool IsSteamVRRoot(const std::wstring &candidate)
{
	if (candidate.empty()) return false;
	DWORD attr = GetFileAttributesW((candidate + L"\\steamapps\\common\\SteamVR").c_str());
	return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

// Minimal libraryfolders.vdf scanner. Reads the file line by line and grabs
// values from lines that look like:  "path"   "C:\\some\\path"
// Tests each path for the presence of steamapps\common\SteamVR.
// Returns the SteamVR install root, or empty if not found.
std::wstring FindSteamVRRootFromVDF(const std::wstring &steamPath)
{
	if (steamPath.empty()) return {};

	// The Steam install itself is always a library root.
	if (IsSteamVRRoot(steamPath)) return steamPath;

	std::wstring vdfPath = steamPath + L"\\config\\libraryfolders.vdf";
	std::ifstream vdf(vdfPath);
	if (!vdf.is_open()) return {};

	std::string line;
	while (std::getline(vdf, line)) {
		// Find a line containing the key "path" (case-insensitive enough for VDF)
		auto kpos = line.find("\"path\"");
		if (kpos == std::string::npos) {
			// Also accept "Path" with capital P just in case
			kpos = line.find("\"Path\"");
		}
		if (kpos == std::string::npos) continue;
		// Find the value: the next quoted string after the key
		auto q1 = line.find('"', kpos + 6);
		if (q1 == std::string::npos) continue;
		auto q2 = line.find('"', q1 + 1);
		if (q2 == std::string::npos) continue;
		std::string raw = line.substr(q1 + 1, q2 - q1 - 1);
		// VDF uses \\ escape; convert to single backslash
		std::string unescaped;
		unescaped.reserve(raw.size());
		for (size_t i = 0; i < raw.size(); ++i) {
			if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == '\\') {
				unescaped += '\\'; ++i;
			} else {
				unescaped += raw[i];
			}
		}
		// Widen to wstring for the filesystem check
		int needed = MultiByteToWideChar(CP_UTF8, 0, unescaped.c_str(), -1, nullptr, 0);
		if (needed <= 1) continue;
		std::wstring candidate(needed - 1, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, unescaped.c_str(), -1, candidate.data(), needed);
		if (IsSteamVRRoot(candidate)) return candidate;
	}
	return {};
}

// Returns the SteamVR install root (the directory that contains
// steamapps\common\SteamVR), or empty if discovery fails entirely.
std::wstring DiscoverSteamVRRoot()
{
	std::wstring steamPath = FindSteamInstallPath();
	std::wstring root = FindSteamVRRootFromVDF(steamPath);
	return root; // may be empty
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

// Encode a PowerShell script body as the value of powershell.exe's
// -EncodedCommand argument: base64 of UTF-16 LE bytes. Sidesteps every quoting
// pitfall in ShellExecuteEx + runas re-parsing, which previously caused
// elevated PowerShell to drop to an interactive prompt with no -Command
// because UAC's launcher chewed off the single-quoted command body.
std::wstring EncodePowerShellCommand(const std::wstring &script)
{
	// Build UTF-16 LE bytes. On Windows wchar_t is 16-bit and the host is
	// little-endian, so the bytes are already in the right order.
	std::vector<unsigned char> bytes;
	bytes.reserve(script.size() * 2);
	for (wchar_t ch : script) {
		bytes.push_back(static_cast<unsigned char>(ch & 0xFF));
		bytes.push_back(static_cast<unsigned char>((ch >> 8) & 0xFF));
	}

	static const char *kBase64 =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::wstring out;
	out.reserve(((bytes.size() + 2) / 3) * 4);
	size_t i = 0;
	while (i + 3 <= bytes.size()) {
		uint32_t v = (uint32_t(bytes[i]) << 16) | (uint32_t(bytes[i + 1]) << 8) | uint32_t(bytes[i + 2]);
		out += (wchar_t)kBase64[(v >> 18) & 0x3F];
		out += (wchar_t)kBase64[(v >> 12) & 0x3F];
		out += (wchar_t)kBase64[(v >> 6)  & 0x3F];
		out += (wchar_t)kBase64[ v        & 0x3F];
		i += 3;
	}
	if (i < bytes.size()) {
		uint32_t v = uint32_t(bytes[i]) << 16;
		size_t rem = bytes.size() - i;
		if (rem == 2) v |= uint32_t(bytes[i + 1]) << 8;
		out += (wchar_t)kBase64[(v >> 18) & 0x3F];
		out += (wchar_t)kBase64[(v >> 12) & 0x3F];
		out += (wchar_t)(rem == 2 ? kBase64[(v >> 6) & 0x3F] : L'=');
		out += L'=';
	}
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

	// --- Install dir: prefer exe's own directory over the hard-coded fallback.
	std::wstring exeDir = ExeDir();
	ctx.installDir = exeDir.empty()
		? L"C:\\Program Files\\WKOpenVR"
		: exeDir;

	const std::wstring root = LocalAppDataLow();
	if (!root.empty()) {
		std::wstring pairRoot = root + L"\\WKOpenVR";
		EnsureDir(pairRoot);
		ctx.profileRoot = pairRoot + L"\\profiles";
		ctx.logRoot = pairRoot + L"\\Logs";
		EnsureDir(ctx.profileRoot);
		EnsureDir(ctx.logRoot);
	}

	// --- Driver resources dir: discover SteamVR via registry + libraryfolders.vdf.
	// Fall back to the hard-coded path if any step fails so that known-good
	// installs continue to work even if the discovery logic hits an edge case.
	static const std::wstring kFallbackResources =
		L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\drivers\\01wkopenvr\\resources";
	std::wstring resources;
	std::wstring steamvrRoot = DiscoverSteamVRRoot();
	if (!steamvrRoot.empty()) {
		resources = steamvrRoot + L"\\steamapps\\common\\SteamVR\\drivers\\01wkopenvr\\resources";
	}
	if (resources.empty()) {
		resources = kFallbackResources;
	}
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
		// New-Item does NOT accept -LiteralPath in Windows PowerShell 5.1 (the
		// shipping host). It only has -Path, which is fine here because driver
		// resources paths never contain wildcards. Set-Content does accept
		// -LiteralPath; we keep that to defeat any accidental wildcard parse on
		// the destination filename.
		command =
			L"New-Item -ItemType Directory -Force -Path " + QuotePowerShellString(parent) +
			L" | Out-Null; Set-Content -LiteralPath " + QuotePowerShellString(path) +
			L" -Value enabled -NoNewline";
	} else {
		command =
			L"if (Test-Path -LiteralPath " + QuotePowerShellString(path) +
			L") { Remove-Item -LiteralPath " + QuotePowerShellString(path) + L" -Force }";
	}

	// -EncodedCommand expects base64-encoded UTF-16 LE. We use it instead of
	// -Command '<script>' because ShellExecuteEx with the runas verb routes
	// through UAC's launcher, which re-parses lpParameters and silently strips
	// single-quoted blocks. The symptom was an elevated powershell.exe that
	// dropped to an interactive prompt with no -Command -- the helper would
	// hang indefinitely while the Modules tab showed "Enabling..." forever.
	// EncodedCommand has no special characters in the cmd line so re-parsing
	// is a no-op.
	std::wstring args = L"-NoProfile -ExecutionPolicy Bypass -EncodedCommand " + EncodePowerShellCommand(command);

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
