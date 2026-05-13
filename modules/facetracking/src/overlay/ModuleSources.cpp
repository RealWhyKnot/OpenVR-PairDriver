#define _CRT_SECURE_NO_DEPRECATE
#include "ModuleSources.h"

#include "Logging.h"

#include "picojson.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <objbase.h>

#pragma comment(lib, "bcrypt.lib")

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace facetracking {

namespace fs = std::filesystem;

// ---- string helpers -----------------------------------------------------

static std::string Wide2Utf8(const std::wstring &w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
}

static std::wstring Utf8ToWide(const std::string &s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    return out;
}

// ---- picojson helpers ---------------------------------------------------

static std::string PjStr(const picojson::value &v, const char *key,
                          std::string fallback = {})
{
    if (!v.is<picojson::object>()) return fallback;
    const auto &o = v.get<picojson::object>();
    auto it = o.find(key);
    if (it == o.end() || !it->second.is<std::string>()) return fallback;
    return it->second.get<std::string>();
}

static bool PjBool(const picojson::value &v, const char *key, bool fallback = false)
{
    if (!v.is<picojson::object>()) return fallback;
    const auto &o = v.get<picojson::object>();
    auto it = o.find(key);
    if (it == o.end() || !it->second.is<bool>()) return fallback;
    return it->second.get<bool>();
}

// ---- public utilities ---------------------------------------------------

std::wstring FtDataDir()
{
    PWSTR raw = nullptr;
    if (S_OK != SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, nullptr, &raw)) {
        if (raw) CoTaskMemFree(raw);
        return {};
    }
    std::wstring root(raw);
    CoTaskMemFree(raw);

    std::wstring base = root + L"\\WKOpenVR\\facetracking";
    CreateDirectoryW((root + L"\\WKOpenVR").c_str(), nullptr);
    CreateDirectoryW(base.c_str(), nullptr);
    return base;
}

std::string GenerateSourceId()
{
    BYTE buf[16] = {};
    BCryptGenRandom(nullptr, buf, sizeof(buf), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    char hex[33] = {};
    for (int i = 0; i < 16; ++i)
        (void)snprintf(hex + i * 2, 3, "%02x", buf[i]);
    return std::string(hex);
}

std::string NowIso8601()
{
    SYSTEMTIME st{};
    GetSystemTime(&st);
    char buf[32];
    (void)snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);
    return std::string(buf);
}

// ---- sources.json -------------------------------------------------------

static std::wstring SourcesPath()
{
    return FtDataDir() + L"\\sources.json";
}

static SourceKind KindFromString(const std::string &s)
{
    if (s == "folder")   return SourceKind::Folder;
    if (s == "github")   return SourceKind::GitHub;
    return SourceKind::Registry;
}

static std::string KindToString(SourceKind k)
{
    switch (k) {
        case SourceKind::Folder:   return "folder";
        case SourceKind::GitHub:   return "github";
        default:                   return "registry";
    }
}

SourcesCatalogue LoadSourcesCatalogue()
{
    std::wstring path = SourcesPath();
    std::ifstream in(path);
    if (!in.is_open()) return {};

    std::stringstream ss;
    ss << in.rdbuf();
    picojson::value root;
    std::string err = picojson::parse(root, ss.str());
    if (!err.empty() || !root.is<picojson::object>()) return {};

    SourcesCatalogue cat;
    const auto &obj = root.get<picojson::object>();
    {
        auto it = obj.find("schema_version");
        if (it != obj.end() && it->second.is<double>())
            cat.schema_version = static_cast<int>(it->second.get<double>());
    }

    const auto *arr = [&]() -> const picojson::array * {
        auto it = obj.find("sources");
        if (it == obj.end() || !it->second.is<picojson::array>()) return nullptr;
        return &it->second.get<picojson::array>();
    }();

    if (arr) {
        for (const auto &el : *arr) {
            ModuleSource src;
            src.id               = PjStr(el, "id");
            src.kind             = KindFromString(PjStr(el, "kind"));
            src.url              = PjStr(el, "url");
            src.path             = PjStr(el, "path");
            src.owner_repo       = PjStr(el, "owner_repo");
            src.label            = PjStr(el, "label");
            src.auto_update      = PjBool(el, "auto_update");
            src.added_at         = PjStr(el, "added_at");
            src.last_checked_at  = PjStr(el, "last_checked_at");
            src.last_release_tag = PjStr(el, "last_release_tag");
            if (!src.id.empty())
                cat.sources.push_back(std::move(src));
        }
    }
    return cat;
}

bool SaveSourcesCatalogue(const SourcesCatalogue &cat)
{
    std::wstring path = SourcesPath();
    std::wstring tmp  = path + L".tmp";

    picojson::array arr;
    for (const auto &src : cat.sources) {
        picojson::object o;
        o["id"]               = picojson::value(src.id);
        o["kind"]             = picojson::value(KindToString(src.kind));
        o["label"]            = picojson::value(src.label);
        o["auto_update"]      = picojson::value(src.auto_update);
        if (!src.url.empty())          o["url"]              = picojson::value(src.url);
        if (!src.path.empty())         o["path"]             = picojson::value(src.path);
        if (!src.owner_repo.empty())   o["owner_repo"]       = picojson::value(src.owner_repo);
        if (!src.added_at.empty())     o["added_at"]         = picojson::value(src.added_at);
        if (!src.last_checked_at.empty())
            o["last_checked_at"] = picojson::value(src.last_checked_at);
        if (!src.last_release_tag.empty())
            o["last_release_tag"] = picojson::value(src.last_release_tag);
        arr.push_back(picojson::value(o));
    }
    picojson::object root;
    root["schema_version"] = picojson::value((double)cat.schema_version);
    root["sources"]        = picojson::value(arr);
    std::string body = picojson::value(root).serialize(true);

    HANDLE hf = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) {
        FT_LOG_OVL("[sources] failed to open sources.json.tmp for write (err=%lu)",
                   GetLastError());
        return false;
    }
    DWORD written = 0;
    BOOL ok = WriteFile(hf, body.data(), (DWORD)body.size(), &written, nullptr);
    if (ok) ok = FlushFileBuffers(hf);
    CloseHandle(hf);

    if (!ok || written != (DWORD)body.size()) {
        FT_LOG_OVL("[sources] write failed for sources.json.tmp (err=%lu)", GetLastError());
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        FT_LOG_OVL("[sources] atomic rename failed (err=%lu)", GetLastError());
        DeleteFileW(tmp.c_str());
        return false;
    }
    FT_LOG_OVL("[sources] saved sources.json (%zu sources)", cat.sources.size());
    return true;
}

// Built-in registry source ID (stable across installs).
static const char kRegistrySourceId[] = "00000000000000000000000000000001";
static const char kRegistryUrl[]      = "https://legacy-registry.whyknot.dev";

SourcesCatalogue EnsureSourcesCatalogue()
{
    SourcesCatalogue cat = LoadSourcesCatalogue();

    // Check if registry entry already exists.
    for (const auto &s : cat.sources)
        if (s.id == kRegistrySourceId) return cat;

    // Seed with registry.
    ModuleSource reg;
    reg.id          = kRegistrySourceId;
    reg.kind        = SourceKind::Registry;
    reg.url         = kRegistryUrl;
    reg.label       = "Legacy registry (curated)";
    reg.auto_update = true;
    cat.sources.insert(cat.sources.begin(), std::move(reg));
    cat.schema_version = 1;
    SaveSourcesCatalogue(cat);
    return cat;
}

std::string SourceLabel(const SourcesCatalogue &cat, const std::string &source_id)
{
    for (const auto &src : cat.sources)
        if (src.id == source_id) return src.label;
    return source_id.empty() ? "Unknown" : source_id;
}

// ---- disk scan ----------------------------------------------------------

std::vector<InstalledModule> ScanInstalledModules()
{
    std::wstring base = FtDataDir();
    if (base.empty()) return {};

    std::wstring modsDir = base + L"\\modules";
    CreateDirectoryW(modsDir.c_str(), nullptr);

    std::vector<InstalledModule> result;

    std::error_code ec;
    for (const auto &uuidEntry : fs::directory_iterator(modsDir, ec)) {
        if (!uuidEntry.is_directory()) continue;
        std::string uuid = Wide2Utf8(uuidEntry.path().filename().wstring());

        for (const auto &verEntry : fs::directory_iterator(uuidEntry.path(), ec)) {
            if (!verEntry.is_directory()) continue;
            std::string version = Wide2Utf8(verEntry.path().filename().wstring());

            fs::path manifestPath = verEntry.path() / L"manifest.json";
            if (!fs::exists(manifestPath, ec)) continue;

            InstalledModule mod;
            mod.uuid     = uuid;
            mod.version  = version;
            mod.manifest_path = Wide2Utf8(manifestPath.wstring());

            // Read manifest.json for name + vendor.
            {
                std::ifstream mf(manifestPath.wstring());
                if (mf.is_open()) {
                    std::stringstream ss;
                    ss << mf.rdbuf();
                    picojson::value root;
                    if (picojson::parse(root, ss.str()).empty()) {
                        mod.name   = PjStr(root, "name");
                        mod.vendor = PjStr(root, "vendor");
                    }
                }
            }
            if (mod.name.empty())   mod.name   = uuid;
            if (mod.vendor.empty()) mod.vendor  = "Unknown";

            // Read optional source.json sidecar.
            fs::path sourcePath = verEntry.path() / L"source.json";
            if (fs::exists(sourcePath, ec)) {
                std::ifstream sf(sourcePath.wstring());
                if (sf.is_open()) {
                    std::stringstream ss;
                    ss << sf.rdbuf();
                    picojson::value root;
                    if (picojson::parse(root, ss.str()).empty()) {
                        mod.source_id       = PjStr(root, "source_id");
                        mod.source_kind_str = PjStr(root, "source_kind");
                        mod.sha_verified    = PjBool(root, "verified_sha256");
                        mod.release_tag     = PjStr(root, "release_tag");
                    }
                }
            }

            result.push_back(std::move(mod));
        }
    }
    return result;
}

// ---- sync runner --------------------------------------------------------

ModuleSyncRunner::ModuleSyncRunner() = default;

ModuleSyncRunner::~ModuleSyncRunner()
{
    if (proc_ != INVALID_HANDLE_VALUE) {
        TerminateProcess(proc_, 1);
        CloseHandle(proc_);
    }
}

/*static*/
std::wstring ModuleSyncRunner::ScriptPath()
{
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    fs::path p(exePath);
    return (p.parent_path() / L"resources" / L"face-module-sync.ps1").wstring();
}

void ModuleSyncRunner::StartAdd(SourceKind kind, const std::string &source_data_json,
                                const std::string &source_id)
{
    if (queue_.size() >= 8) return;
    PendingOp op;
    op.action      = "add";
    op.kind        = KindToString(kind);
    op.source_data = source_data_json;
    op.source_id   = source_id;
    queue_.push_back(std::move(op));
    if (!IsRunning()) LaunchNext();
}

void ModuleSyncRunner::StartUpdate(const std::string &source_id,
                                   const std::string &source_data_json)
{
    if (queue_.size() >= 8) return;
    PendingOp op;
    op.action      = "update";
    op.source_data = source_data_json;
    op.source_id   = source_id;
    queue_.push_back(std::move(op));
    if (!IsRunning()) LaunchNext();
}

void ModuleSyncRunner::StartRemove(const std::string &source_id)
{
    if (queue_.size() >= 8) return;
    PendingOp op;
    op.action    = "remove";
    op.source_id = source_id;
    queue_.push_back(std::move(op));
    if (!IsRunning()) LaunchNext();
}

bool ModuleSyncRunner::IsRunning() const
{
    return proc_ != INVALID_HANDLE_VALUE;
}

std::optional<SyncResult> ModuleSyncRunner::Poll()
{
    if (!IsRunning()) {
        if (!queue_.empty()) LaunchNext();
        return std::nullopt;
    }

    // Check if the script process finished.
    DWORD exitCode = STILL_ACTIVE;
    if (!GetExitCodeProcess(proc_, &exitCode) || exitCode == STILL_ACTIVE) {
        // Still running -- check for timeout (90 s).
        auto elapsed = std::chrono::steady_clock::now() - launch_time_;
        if (elapsed > std::chrono::seconds(90)) {
            FT_LOG_OVL("[sync] script timed out after 90 s, killing");
            TerminateProcess(proc_, 1);
            CloseHandle(proc_);
            proc_ = INVALID_HANDLE_VALUE;
            SyncResult r;
            r.ok      = false;
            r.message = "Sync timed out after 90 s.";
            if (!queue_.empty()) LaunchNext();
            return r;
        }
        return std::nullopt;
    }

    CloseHandle(proc_);
    proc_ = INVALID_HANDLE_VALUE;

    // Read the result JSON.
    SyncResult result;
    if (!result_path_.empty()) {
        std::ifstream rf(result_path_);
        if (rf.is_open()) {
            std::stringstream ss;
            ss << rf.rdbuf();
            picojson::value root;
            if (picojson::parse(root, ss.str()).empty()) {
                result.ok               = PjBool(root, "ok");
                result.message          = PjStr(root, "message");
                result.installed_uuid   = PjStr(root, "installed_uuid");
                result.installed_version = PjStr(root, "installed_version");
            } else {
                result.ok      = false;
                result.message = "Result JSON parse error.";
            }
            rf.close();
            DeleteFileW(result_path_.c_str());
        } else {
            result.ok      = exitCode == 0;
            result.message = exitCode == 0 ? "Done." : "Sync failed -- check log.";
        }
        result_path_.clear();
    }

    FT_LOG_OVL("[sync] script finished: ok=%d msg='%s'",
               (int)result.ok, result.message.c_str());

    if (!queue_.empty()) LaunchNext();
    return result;
}

// Encode a UTF-8 string as the base64 UTF-16 LE payload for
// powershell.exe -EncodedCommand.
static std::string EncodeForPowerShell(const std::wstring &cmd)
{
    // Base64-encode the UTF-16 LE byte sequence.
    const BYTE *bytes = reinterpret_cast<const BYTE *>(cmd.data());
    DWORD blen = static_cast<DWORD>(cmd.size() * sizeof(wchar_t));
    DWORD outlen = 0;
    CryptBinaryToStringA(bytes, blen, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                         nullptr, &outlen);
    std::string out(outlen, '\0');
    CryptBinaryToStringA(bytes, blen, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                         out.data(), &outlen);
    // outlen includes null terminator if any; trim trailing null.
    while (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

#pragma comment(lib, "crypt32.lib")

void ModuleSyncRunner::LaunchNext()
{
    if (queue_.empty()) return;
    PendingOp op = queue_.front();
    queue_.erase(queue_.begin());

    // Build a temp result path.
    wchar_t tmp[MAX_PATH], dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    GetTempFileNameW(dir, L"fts", 0, tmp);
    result_path_ = std::wstring(tmp);

    std::wstring script = ScriptPath();
    // Escape backslashes + quotes in JSON for embedding in a PS string.
    // We pass everything via -EncodedCommand so no quoting issues.
    auto escapeJs = [](std::string s) -> std::wstring {
        std::wstring r;
        for (unsigned char c : s) {
            if (c == '\\') { r += L"\\\\"; }
            else if (c == '"')  { r += L"\\\""; }
            else { r += static_cast<wchar_t>(c); }
        }
        return r;
    };

    std::wstring resultPathW = result_path_;

    // Build the PS command as a wstring, then base64 encode it.
    std::wstring psCmd = L"& '" + script + L"'";
    psCmd += L" -Action " + Utf8ToWide(op.action);
    if (!op.kind.empty())
        psCmd += L" -Kind " + Utf8ToWide(op.kind);
    if (!op.source_data.empty())
        psCmd += L" -SourceData \"" + escapeJs(op.source_data) + L"\"";
    if (!op.source_id.empty())
        psCmd += L" -SourceId '" + Utf8ToWide(op.source_id) + L"'";
    psCmd += L" -ResultPath '" + resultPathW + L"'";

    std::string encoded = EncodeForPowerShell(psCmd);

    std::wstring cmdLine = L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand ";
    cmdLine += Utf8ToWide(encoded);

    STARTUPINFOW si{};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        FT_LOG_OVL("[sync] CreateProcessW failed: err=%lu", GetLastError());
        result_path_.clear();
        SyncResult r;
        r.ok      = false;
        r.message = "Failed to launch sync script.";
        // We won't return this from Poll() directly, but the next Poll() will
        // read result_path_ empty and return an error.
        return;
    }

    CloseHandle(pi.hThread);
    proc_        = pi.hProcess;
    launch_time_ = std::chrono::steady_clock::now();
    FT_LOG_OVL("[sync] launched script: action=%s kind=%s",
               op.action.c_str(), op.kind.c_str());
}

} // namespace facetracking
