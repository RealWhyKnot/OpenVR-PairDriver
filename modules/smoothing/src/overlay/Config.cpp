#include "Config.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>

#include <cstdio>
#include <string>

namespace
{
    std::wstring ConfigDir()
    {
        PWSTR root = nullptr;
        if (S_OK != SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, nullptr, &root)) {
            if (root) CoTaskMemFree(root);
            return {};
        }
        std::wstring dir(root);
        CoTaskMemFree(root);
        dir += L"\\OpenVR-Pair";
        if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
            return {};
        }
        dir += L"\\profiles";
        if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
            return {};
        }
        return dir;
    }

    std::wstring ConfigPath()
    {
        std::wstring dir = ConfigDir();
        if (dir.empty()) return {};
        return dir + L"\\smoothing.txt";
    }
}

SmoothingConfig LoadConfig()
{
    SmoothingConfig cfg;
    std::wstring path = ConfigPath();
    if (path.empty()) return cfg;

    FILE *f = _wfopen(path.c_str(), L"r");
    if (!f) return cfg;

    char line[256];
    while (fgets(line, sizeof line, f)) {
        // Strip trailing CR/LF.
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        if (strcmp(key, "master_enabled") == 0) {
            cfg.master_enabled = (atoi(val) != 0);
        } else if (strcmp(key, "smoothness") == 0) {
            int n = atoi(val);
            if (n < 0) n = 0;
            if (n > 100) n = 100;
            cfg.smoothness = n;
        } else if (strcmp(key, "finger_mask") == 0) {
            unsigned n = (unsigned)strtoul(val, nullptr, 0);
            cfg.finger_mask = (uint16_t)(n & 0xFFFFu);
        } else if (strncmp(key, "per_finger_smoothness.", 22) == 0) {
            // Key format: per_finger_smoothness.<index>. Value: 0..100.
            int idx = atoi(key + 22);
            int n   = atoi(val);
            if (n < 0) n = 0;
            if (n > 100) n = 100;
            if (idx >= 0 && idx < 10) cfg.per_finger_smoothness[idx] = n;
        } else if (strncmp(key, "tracker_smoothness.", 19) == 0) {
            // Key format: tracker_smoothness.<serial>. Value: 0..100.
            // Serials are alphanumeric/short so no escaping needed in either
            // half of the key=value line.
            int n = atoi(val);
            if (n < 0) n = 0;
            if (n > 100) n = 100;
            if (n > 0) cfg.trackerSmoothness[std::string(key + 19)] = n;
        }
    }
    fclose(f);
    return cfg;
}

void SaveConfig(const SmoothingConfig &cfg)
{
    std::wstring path = ConfigPath();
    if (path.empty()) return;

    FILE *f = _wfopen(path.c_str(), L"w");
    if (!f) return;
    fprintf(f, "master_enabled=%d\n", cfg.master_enabled ? 1 : 0);
    fprintf(f, "smoothness=%d\n", cfg.smoothness);
    fprintf(f, "finger_mask=%u\n", (unsigned)cfg.finger_mask);
    for (int i = 0; i < 10; ++i) {
        // Only write non-zero entries so the on-disk file stays small and a
        // hand-edit removing a line restores the global-fallback default.
        if (cfg.per_finger_smoothness[i] > 0) {
            fprintf(f, "per_finger_smoothness.%d=%d\n", i, cfg.per_finger_smoothness[i]);
        }
    }
    for (const auto &kv : cfg.trackerSmoothness) {
        // 0 values are dropped from the map on slider release; anything
        // still here is meaningful and should round-trip.
        fprintf(f, "tracker_smoothness.%s=%d\n", kv.first.c_str(), kv.second);
    }
    fclose(f);
}
