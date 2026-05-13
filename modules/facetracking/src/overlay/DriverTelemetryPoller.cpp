#define _CRT_SECURE_NO_DEPRECATE
#include "DriverTelemetryPoller.h"

#include "Logging.h"

#include "picojson.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>

#include <chrono>
#include <fstream>
#include <sstream>
#include <string>

namespace facetracking {

namespace {

// If the driver hasn't refreshed within this window the snapshot is flagged
// stale. The driver writes every ~500 ms; 5 s leaves ample slack for transient
// FS lag and short driver pauses.
constexpr auto kStaleAfter   = std::chrono::seconds(5);
constexpr auto kReadInterval = std::chrono::milliseconds(500);

std::string Wide2Utf8(const std::wstring &w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string &s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    return out;
}

std::wstring ResolveTelemetryPath()
{
    PWSTR raw = nullptr;
    if (S_OK != SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, nullptr, &raw)) {
        if (raw) CoTaskMemFree(raw);
        return {};
    }
    std::wstring root(raw);
    CoTaskMemFree(raw);
    return root + L"\\OpenVR-Pair\\facetracking\\driver_telemetry.json";
}

int64_t FileMtime(const std::wstring &path)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
    ULARGE_INTEGER u{};
    u.LowPart  = data.ftLastWriteTime.dwLowDateTime;
    u.HighPart = data.ftLastWriteTime.dwHighDateTime;
    return static_cast<int64_t>(u.QuadPart);
}

// picojson helpers.
double Num(const picojson::value &v, const char *key, double fallback = 0.0)
{
    if (!v.is<picojson::object>()) return fallback;
    const auto &o = v.get<picojson::object>();
    auto it = o.find(key);
    if (it == o.end() || !it->second.is<double>()) return fallback;
    return it->second.get<double>();
}

bool Bool(const picojson::value &v, const char *key, bool fallback = false)
{
    if (!v.is<picojson::object>()) return fallback;
    const auto &o = v.get<picojson::object>();
    auto it = o.find(key);
    if (it == o.end() || !it->second.is<bool>()) return fallback;
    return it->second.get<bool>();
}

const picojson::value *Obj(const picojson::value &v, const char *key)
{
    if (!v.is<picojson::object>()) return nullptr;
    const auto &o = v.get<picojson::object>();
    auto it = o.find(key);
    if (it == o.end()) return nullptr;
    return &it->second;
}

} // namespace

DriverTelemetryPoller::DriverTelemetryPoller()
{
    ResolvePath();
}

void DriverTelemetryPoller::ResolvePath()
{
    std::wstring wpath = ResolveTelemetryPath();
    path_utf8_ = Wide2Utf8(wpath);
}

void DriverTelemetryPoller::Tick()
{
    auto now = std::chrono::steady_clock::now();

    if (now - last_read_attempt_ < kReadInterval) {
        if (snapshot_.valid) {
            snapshot_.stale = (now - last_successful_read_) > kStaleAfter;
        }
        return;
    }
    last_read_attempt_ = now;

    if (path_utf8_.empty()) return;

    std::wstring wpath = Utf8ToWide(path_utf8_);
    int64_t mtime = FileMtime(wpath);
    if (mtime == 0) {
        // Driver not running yet.
        if (snapshot_.valid) {
            snapshot_.stale = (now - last_successful_read_) > kStaleAfter;
        }
        return;
    }

    if (mtime == last_observed_mtime_) {
        snapshot_.stale = (now - last_successful_read_) > kStaleAfter;
        return;
    }

    ReadFile();
    if (snapshot_.valid) {
        last_observed_mtime_  = mtime;
        last_successful_read_ = now;
        snapshot_.stale       = false;
    }
}

void DriverTelemetryPoller::ReadFile()
{
    std::ifstream is(Utf8ToWide(path_utf8_));
    if (!is) return;

    std::stringstream ss;
    ss << is.rdbuf();
    std::string body = ss.str();

    picojson::value root;
    std::string err = picojson::parse(root, body);
    if (!err.empty() || !root.is<picojson::object>()) {
        // Caught mid-write; next tick retries.
        return;
    }

    DriverTelemetrySnapshot s;
    s.valid             = true;
    s.driver_pid        = static_cast<int>(Num(root, "driver_pid"));
    s.frames_processed  = static_cast<uint64_t>(Num(root, "frames_processed"));

    if (const auto *verg = Obj(root, "vergence"); verg && verg->is<picojson::object>()) {
        s.vergence_enabled = Bool(*verg, "enabled");
        s.focus_distance_m = static_cast<float>(Num(*verg, "focus_distance_m"));
        s.ipd_m            = static_cast<float>(Num(*verg, "ipd_m"));
    }

    if (const auto *sr = Obj(root, "shape_readiness"); sr && sr->is<picojson::array>()) {
        const auto &arr = sr->get<picojson::array>();
        const int n = static_cast<int>(arr.size());
        for (int i = 0; i < 65 && i < n; ++i) {
            s.shape_warm[i] = arr[i].is<bool>() && arr[i].get<bool>();
        }
    }

    FT_LOG_OVL("DriverTelemetryPoller: refreshed (pid=%d frames=%llu verg=%s focus=%.3fm)",
        s.driver_pid,
        (unsigned long long)s.frames_processed,
        s.vergence_enabled ? "on" : "off",
        s.focus_distance_m);

    snapshot_ = std::move(s);
}

} // namespace facetracking
