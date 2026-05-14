#define _CRT_SECURE_NO_DEPRECATE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>

#include "HostStatus.h"
#include "Logging.h"

#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

static std::string EscapeJson(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char esc[8];
                snprintf(esc, sizeof(esc), "\\u%04x", (unsigned char)c);
                out += esc;
            } else {
                out += c;
            }
        }
    }
    return out;
}

HostStatus::HostStatus()
{
    WritePath();
}

void HostStatus::WritePath()
{
    PWSTR raw = nullptr;
    if (S_OK != SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, nullptr, &raw)) {
        if (raw) CoTaskMemFree(raw);
        return;
    }
    std::wstring root(raw);
    CoTaskMemFree(raw);
    root += L"\\WKOpenVR\\translator";
    CreateDirectoryW(root.c_str(), nullptr);
    status_path_ = root + L"\\host_status.json";
}

void HostStatus::SetState(State s) noexcept { state_ = s; }
void HostStatus::SetMicName(const std::string &name) { mic_name_ = name; }
void HostStatus::SetLastTranscript(const std::string &t) { last_transcript_ = t; }
void HostStatus::SetLastTranslation(const std::string &t) { last_translation_ = t; }
void HostStatus::SetLastError(const std::string &e) { last_error_ = e; }
void HostStatus::IncrementPacketsSent() noexcept { ++packets_sent_; }

void HostStatus::MaybeFlush()
{
    const long long now = static_cast<long long>(GetTickCount64());
    if (now - last_flush_tick_ < 1000) return;
    last_flush_tick_ = now;
    DoFlush();
}

void HostStatus::Flush()
{
    last_flush_tick_ = static_cast<long long>(GetTickCount64());
    DoFlush();
}

void HostStatus::DoFlush()
{
    if (status_path_.empty()) return;

    std::ostringstream o;
    o << "{\n";
    o << "  \"schema_version\": 1,\n";
    o << "  \"host_pid\": " << (long long)GetCurrentProcessId() << ",\n";
    o << "  \"state\": " << (int)state_ << ",\n";
    o << "  \"mic_name\": \"" << EscapeJson(mic_name_) << "\",\n";
    o << "  \"last_transcript\": \"" << EscapeJson(last_transcript_) << "\",\n";
    o << "  \"last_translation\": \"" << EscapeJson(last_translation_) << "\",\n";
    o << "  \"last_error\": \"" << EscapeJson(last_error_) << "\",\n";
    o << "  \"packets_sent\": " << packets_sent_ << "\n";
    o << "}\n";
    std::string json = o.str();

    std::wstring tmp = status_path_ + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

    DWORD written = 0;
    WriteFile(h, json.data(), static_cast<DWORD>(json.size()), &written, nullptr);
    CloseHandle(h);

    if (written == static_cast<DWORD>(json.size())) {
        MoveFileExW(tmp.c_str(), status_path_.c_str(), MOVEFILE_REPLACE_EXISTING);
    } else {
        DeleteFileW(tmp.c_str());
    }
}
