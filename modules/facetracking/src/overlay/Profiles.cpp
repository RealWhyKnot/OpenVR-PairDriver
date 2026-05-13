#define _CRT_SECURE_NO_DEPRECATE
#include "Profiles.h"

#include "Logging.h"

#include "picojson.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace {

// %LocalAppDataLow%\OpenVR-Pair\profiles\facetracking.json
std::wstring ProfilePath()
{
    PWSTR raw = nullptr;
    if (S_OK != SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, nullptr, &raw)) {
        if (raw) CoTaskMemFree(raw);
        return {};
    }
    std::wstring root(raw);
    CoTaskMemFree(raw);

    std::wstring dir = root + L"\\OpenVR-Pair";
    CreateDirectoryW(dir.c_str(), nullptr);
    dir += L"\\profiles";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\facetracking.json";
}

std::string Wide2Utf8(const std::wstring &w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

FacetrackingProfile Decode(const picojson::value &v)
{
    FacetrackingProfile p;
    if (!v.is<picojson::object>()) return p;
    const auto &obj = v.get<picojson::object>();

    auto getBool = [&](const char *k, bool &out) {
        auto it = obj.find(k);
        if (it != obj.end() && it->second.is<bool>()) out = it->second.get<bool>();
    };
    auto getInt = [&](const char *k, int &out) {
        auto it = obj.find(k);
        if (it != obj.end() && it->second.is<double>())
            out = static_cast<int>(it->second.get<double>());
    };
    auto getStr = [&](const char *k, std::string &out) {
        auto it = obj.find(k);
        if (it != obj.end() && it->second.is<std::string>())
            out = it->second.get<std::string>();
    };

    getBool("eyelid_sync_enabled",        p.eyelid_sync_enabled);
    getBool("eyelid_sync_preserve_winks", p.eyelid_sync_preserve_winks);
    getInt ("eyelid_sync_strength",       p.eyelid_sync_strength);
    getBool("vergence_lock_enabled",      p.vergence_lock_enabled);
    getInt ("vergence_lock_strength",     p.vergence_lock_strength);
    getInt ("continuous_calib_mode",      p.continuous_calib_mode);
    getBool("output_osc_enabled",         p.output_osc_enabled);
    getStr ("osc_host",                   p.osc_host);
    getInt ("osc_port",                   p.osc_port);
    getInt ("gaze_smoothing",             p.gaze_smoothing);
    getInt ("openness_smoothing",         p.openness_smoothing);
    getStr ("active_module_uuid",         p.active_module_uuid);
    getBool("show_raw_values",            p.show_raw_values);
    getInt ("last_tab_index",             p.last_tab_index);

    return p;
}

std::string Encode(const FacetrackingProfile &p)
{
    picojson::object obj;
    obj["eyelid_sync_enabled"]        = picojson::value(p.eyelid_sync_enabled);
    obj["eyelid_sync_preserve_winks"] = picojson::value(p.eyelid_sync_preserve_winks);
    obj["eyelid_sync_strength"]       = picojson::value((double)p.eyelid_sync_strength);
    obj["vergence_lock_enabled"]      = picojson::value(p.vergence_lock_enabled);
    obj["vergence_lock_strength"]     = picojson::value((double)p.vergence_lock_strength);
    obj["continuous_calib_mode"]      = picojson::value((double)p.continuous_calib_mode);
    obj["output_osc_enabled"]         = picojson::value(p.output_osc_enabled);
    obj["osc_host"]                   = picojson::value(p.osc_host);
    obj["osc_port"]                   = picojson::value((double)p.osc_port);
    obj["gaze_smoothing"]             = picojson::value((double)p.gaze_smoothing);
    obj["openness_smoothing"]         = picojson::value((double)p.openness_smoothing);
    obj["active_module_uuid"]         = picojson::value(p.active_module_uuid);
    obj["show_raw_values"]            = picojson::value(p.show_raw_values);
    obj["last_tab_index"]             = picojson::value((double)p.last_tab_index);
    return picojson::value(obj).serialize(true);
}

} // namespace

bool FacetrackingProfileStore::Load()
{
    std::wstring path = ProfilePath();
    if (path.empty()) return false;

    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::stringstream ss;
    ss << in.rdbuf();
    picojson::value v;
    std::string err = picojson::parse(v, ss.str());
    if (!err.empty()) {
        FT_LOG_OVL("[profiles] parse error in '%s': %s",
            Wide2Utf8(path).c_str(), err.c_str());
        return false;
    }
    current = Decode(v);
    FT_LOG_OVL("[profiles] loaded facetracking.json");
    return true;
}

bool FacetrackingProfileStore::Save() const
{
    std::wstring path = ProfilePath();
    if (path.empty()) return false;

    std::wstring tmp = path + L".tmp";
    HANDLE hFile = CreateFileW(
        tmp.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        FT_LOG_OVL("[profiles] failed to open tmp for write (err=%lu)", GetLastError());
        return false;
    }

    std::string body = Encode(current);
    DWORD written = 0;
    BOOL ok = WriteFile(hFile, body.data(), (DWORD)body.size(), &written, nullptr);
    if (ok) ok = FlushFileBuffers(hFile);
    CloseHandle(hFile);

    if (!ok || written != (DWORD)body.size()) {
        FT_LOG_OVL("[profiles] write/flush failed for facetracking.json.tmp (err=%lu)", GetLastError());
        DeleteFileW(tmp.c_str());
        return false;
    }

    if (!MoveFileExW(tmp.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        FT_LOG_OVL("[profiles] atomic rename failed (err=%lu)", GetLastError());
        DeleteFileW(tmp.c_str());
        return false;
    }

    FT_LOG_OVL("[profiles] saved facetracking.json");
    return true;
}
