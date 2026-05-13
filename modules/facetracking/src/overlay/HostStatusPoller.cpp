#define _CRT_SECURE_NO_DEPRECATE
#include "HostStatusPoller.h"

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

// File-stat freshness threshold: if the host hasn't refreshed within this
// window, the snapshot is flagged stale. The host writes once per second
// in steady state, so 10 s leaves plenty of slack for transient FS lag.
constexpr auto kStaleAfter = std::chrono::seconds(10);

// Minimum interval between disk reads. The overlay ticks at ~60 Hz; we
// don't want to stat() the file that often.
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

std::wstring ResolveHostStatusPath()
{
    PWSTR raw = nullptr;
    if (S_OK != SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, nullptr, &raw)) {
        if (raw) CoTaskMemFree(raw);
        return {};
    }
    std::wstring root(raw);
    CoTaskMemFree(raw);
    return root + L"\\OpenVR-Pair\\facetracking\\host_status.json";
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

// picojson helpers -- pull a typed value with a default fallback.
std::string Str(const picojson::value &v, const char *key, std::string fallback = {})
{
    if (!v.is<picojson::object>()) return fallback;
    const auto &o = v.get<picojson::object>();
    auto it = o.find(key);
    if (it == o.end() || !it->second.is<std::string>()) return fallback;
    return it->second.get<std::string>();
}

bool Bool(const picojson::value &v, const char *key, bool fallback = false)
{
    if (!v.is<picojson::object>()) return fallback;
    const auto &o = v.get<picojson::object>();
    auto it = o.find(key);
    if (it == o.end() || !it->second.is<bool>()) return fallback;
    return it->second.get<bool>();
}

double Num(const picojson::value &v, const char *key, double fallback = 0.0)
{
    if (!v.is<picojson::object>()) return fallback;
    const auto &o = v.get<picojson::object>();
    auto it = o.find(key);
    if (it == o.end() || !it->second.is<double>()) return fallback;
    return it->second.get<double>();
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

HostStatusPoller::HostStatusPoller()
{
    ResolvePath();
}

void HostStatusPoller::ResolvePath()
{
    std::wstring wpath = ResolveHostStatusPath();
    path_utf8_ = Wide2Utf8(wpath);
}

void HostStatusPoller::Tick()
{
    auto now = std::chrono::steady_clock::now();

    if (now - last_read_attempt_ < kReadInterval)
    {
        // Not yet time for another disk read; just refresh staleness flag.
        if (snapshot_.valid)
        {
            snapshot_.stale = (now - last_successful_read_) > kStaleAfter;
        }
        return;
    }
    last_read_attempt_ = now;

    if (path_utf8_.empty())
    {
        // Path resolution failed at construction; nothing to do.
        return;
    }

    std::wstring wpath = Utf8ToWide(path_utf8_);
    int64_t mtime = FileMtime(wpath);
    if (mtime == 0)
    {
        // File doesn't exist (host not running yet). Keep prior snapshot if
        // we ever had one, but flag it stale.
        if (snapshot_.valid)
        {
            snapshot_.stale = (now - last_successful_read_) > kStaleAfter;
        }
        return;
    }

    if (mtime == last_observed_mtime_)
    {
        // File unchanged since last successful read. Refresh staleness.
        snapshot_.stale = (now - last_successful_read_) > kStaleAfter;
        return;
    }

    ReadFile();
    if (snapshot_.valid)
    {
        last_observed_mtime_  = mtime;
        last_successful_read_ = now;
        snapshot_.stale       = false;
    }
}

void HostStatusPoller::ReadFile()
{
    std::ifstream is(Utf8ToWide(path_utf8_));
    if (!is) return;

    std::stringstream ss;
    ss << is.rdbuf();
    std::string body = ss.str();

    picojson::value root;
    std::string err = picojson::parse(root, body);
    if (!err.empty() || !root.is<picojson::object>())
    {
        // Could happen briefly if we caught the file between .tmp write
        // and rename. Skip this read silently; the next tick retries.
        return;
    }

    HostStatusSnapshot s;
    s.valid                = true;
    s.host_pid             = static_cast<int>(Num(root, "host_pid"));
    s.host_started_at      = Str(root, "host_started_at");
    s.host_uptime_seconds  = static_cast<int>(Num(root, "host_uptime_s"));
    s.host_shutting_down   = Bool(root, "host_shutting_down");

    if (const auto *am = Obj(root, "active_module"); am && am->is<picojson::object>())
    {
        HostStatusActiveModule m;
        m.uuid    = Str(*am, "uuid");
        m.name    = Str(*am, "name");
        m.vendor  = Str(*am, "vendor");
        m.version = Str(*am, "version");
        if (!m.uuid.empty()) s.active_module = std::move(m);
    }

    if (const auto *im = Obj(root, "installed_modules"); im && im->is<picojson::array>())
    {
        for (const auto &el : im->get<picojson::array>())
        {
            HostStatusInstalledModule mm;
            mm.uuid    = Str(el, "uuid");
            mm.name    = Str(el, "name");
            mm.vendor  = Str(el, "vendor");
            mm.version = Str(el, "version");
            if (!mm.uuid.empty()) s.installed_modules.push_back(std::move(mm));
        }
    }

    if (const auto *o = Obj(root, "osc"); o && o->is<picojson::object>())
    {
        s.osc.enabled            = Bool(*o, "enabled");
        s.osc.target_host        = Str(*o, "target_host");
        s.osc.target_port        = static_cast<int>(Num(*o, "target_port"));
        s.osc.packets_sent       = static_cast<long long>(Num(*o, "packets_sent"));
        s.osc.packets_errored    = static_cast<long long>(Num(*o, "packets_errored"));
        s.osc.packets_per_second = static_cast<float>(Num(*o, "packets_per_second"));
        s.osc.last_error         = Str(*o, "last_error");
    }

    FT_LOG_OVL("HostStatusPoller: refreshed (pid=%d uptime=%ds osc=%lld pkt %s)",
        s.host_pid, s.host_uptime_seconds, s.osc.packets_sent,
        s.osc.enabled ? "enabled" : "disabled");

    snapshot_ = std::move(s);
}

} // namespace facetracking
