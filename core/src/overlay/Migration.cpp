#include "Migration.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace openvr_pair::overlay {

namespace {

// Returns %LocalAppDataLow% as a wide string, or empty on failure.
static std::wstring GetLocalAppDataLow()
{
    PWSTR raw = nullptr;
    if (S_OK != SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, nullptr, &raw)) {
        if (raw) CoTaskMemFree(raw);
        return {};
    }
    std::wstring result(raw);
    CoTaskMemFree(raw);
    return result;
}

// Step 1: copy %LocalAppDataLow%\OpenVR-Pair\ -> %LocalAppDataLow%\WKOpenVR\
// if the new dir does not exist.
static void MigrateAppData()
{
    std::wstring root = GetLocalAppDataLow();
    if (root.empty()) {
        fprintf(stderr, "[Migration] Could not resolve %%LocalAppDataLow%%; skipping AppData migration\n");
        return;
    }

    namespace fs = std::filesystem;
    fs::path newDir = root + L"\\WKOpenVR";
    fs::path oldDir = root + L"\\OpenVR-Pair";

    // Short-circuit: new dir already exists (migration already done, or fresh install).
    if (fs::exists(newDir)) return;
    // Nothing to migrate if old dir also absent (first install).
    if (!fs::exists(oldDir)) return;

    fprintf(stderr, "[Migration] Migrating AppData from OpenVR-Pair to WKOpenVR...\n");

    std::error_code ec;
    fs::copy(oldDir, newDir,
        fs::copy_options::recursive | fs::copy_options::skip_existing,
        ec);

    if (ec) {
        fprintf(stderr, "[Migration] AppData copy failed: %s\n", ec.message().c_str());
        return;
    }

    // Count migrated files + total bytes for the log line.
    uintmax_t fileCount = 0;
    uintmax_t totalBytes = 0;
    for (const auto &entry : fs::recursive_directory_iterator(newDir, ec)) {
        if (!ec && entry.is_regular_file()) {
            ++fileCount;
            std::error_code szEc;
            uintmax_t sz = fs::file_size(entry.path(), szEc);
            if (!szEc) totalBytes += sz;
        }
    }

    fprintf(stderr,
        "[Migration] Migrated AppData from OpenVR-Pair to WKOpenVR "
        "(%llu files, %llu bytes)\n",
        (unsigned long long)fileCount,
        (unsigned long long)totalBytes);
}

// Step 2: copy HKCU\Software\OpenVR-WKSpaceCalibrator ->
//         HKCU\Software\WKOpenVR-SpaceCalibrator if the new key does not exist.
static void MigrateScRegistryKey()
{
    const wchar_t *kOldKey = L"Software\\OpenVR-WKSpaceCalibrator";
    const wchar_t *kNewKey = L"Software\\WKOpenVR-SpaceCalibrator";

    // Check if new key already exists -- if so, nothing to do.
    HKEY hNew = nullptr;
    LSTATUS st = RegOpenKeyExW(HKEY_CURRENT_USER, kNewKey, 0, KEY_READ, &hNew);
    if (st == ERROR_SUCCESS) {
        RegCloseKey(hNew);
        return; // already migrated
    }

    // Check if old key exists -- if not, nothing to migrate.
    HKEY hOld = nullptr;
    st = RegOpenKeyExW(HKEY_CURRENT_USER, kOldKey, 0, KEY_READ, &hOld);
    if (st != ERROR_SUCCESS) {
        return; // old key absent, first install
    }
    RegCloseKey(hOld);

    fprintf(stderr,
        "[Migration] Copying registry key HKCU\\%ls -> HKCU\\%ls\n",
        kOldKey, kNewKey);

    // Create the destination key then copy the tree.
    HKEY hDst = nullptr;
    DWORD disp = 0;
    st = RegCreateKeyExW(HKEY_CURRENT_USER, kNewKey, 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &hDst, &disp);
    if (st != ERROR_SUCCESS) {
        fprintf(stderr, "[Migration] RegCreateKeyExW failed: %ld\n", (long)st);
        return;
    }

    // Open old key with full read access for the copy.
    HKEY hSrc = nullptr;
    st = RegOpenKeyExW(HKEY_CURRENT_USER, kOldKey, 0, KEY_READ, &hSrc);
    if (st != ERROR_SUCCESS) {
        RegCloseKey(hDst);
        fprintf(stderr, "[Migration] Could not open old registry key for copy: %ld\n", (long)st);
        return;
    }

    st = RegCopyTreeW(hSrc, nullptr, hDst);
    RegCloseKey(hSrc);
    RegCloseKey(hDst);

    if (st == ERROR_SUCCESS) {
        fprintf(stderr,
            "[Migration] SC registry key copied from OpenVR-WKSpaceCalibrator to WKOpenVR-SpaceCalibrator\n");
    } else {
        fprintf(stderr,
            "[Migration] RegCopyTreeW failed: %ld (new key created but may be empty)\n",
            (long)st);
    }
}

} // namespace

void RunFirstLaunchMigration()
{
    MigrateAppData();
    MigrateScRegistryKey();
}

} // namespace openvr_pair::overlay
