#define _CRT_SECURE_NO_DEPRECATE
#include "Profiles.h"

#include "Logging.h"

#include "inputhealth/SerialHash.h"
#include "picojson.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::wstring ProfilesDir()
{
	PWSTR rootRaw = nullptr;
	if (S_OK != SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, nullptr, &rootRaw)) {
		if (rootRaw) CoTaskMemFree(rootRaw);
		return {};
	}
	std::wstring root(rootRaw);
	CoTaskMemFree(rootRaw);

	std::wstring dir = root + L"\\OpenVR-Pair";
	CreateDirectoryW(dir.c_str(), nullptr);
	dir += L"\\profiles";
	CreateDirectoryW(dir.c_str(), nullptr);
	return dir;
}

std::string Wide2Utf8(const std::wstring &w)
{
	if (w.empty()) return {};
	int needed = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string out(needed, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), needed, nullptr, nullptr);
	return out;
}

std::wstring Utf82Wide(const std::string &s)
{
	if (s.empty()) return {};
	int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
	std::wstring out(needed, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), needed);
	return out;
}

// Serial strings can contain characters that aren't filename-safe (slashes,
// colons on some vendors). Hash the serial and use the hex hash as the
// filename instead -- collisions are vanishingly unlikely on real hardware
// and the filename is stable across reboots.
std::string FilenameForHash(uint64_t hash)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%016llx.json", (unsigned long long)hash);
	return buf;
}

DeviceProfile Decode(const picojson::value &v)
{
	DeviceProfile p;
	if (!v.is<picojson::object>()) return p;
	const auto &obj = v.get<picojson::object>();
	auto getStr = [&](const char *k, std::string &out) {
		auto it = obj.find(k);
		if (it != obj.end() && it->second.is<std::string>()) out = it->second.get<std::string>();
	};
	auto getBool = [&](const char *k, bool &out) {
		auto it = obj.find(k);
		if (it != obj.end() && it->second.is<bool>()) out = it->second.get<bool>();
	};
	auto getU64 = [&](const char *k, uint64_t &out) {
		auto it = obj.find(k);
		if (it != obj.end() && it->second.is<std::string>()) {
			out = strtoull(it->second.get<std::string>().c_str(), nullptr, 16);
		} else if (it != obj.end() && it->second.is<double>()) {
			double d = it->second.get<double>();
			if (d >= 0.0) out = static_cast<uint64_t>(d);
		}
	};
	auto getU32From = [](const picojson::object &src, const char *k, uint32_t &out) {
		auto it = src.find(k);
		if (it != src.end() && it->second.is<double>()) {
			double d = it->second.get<double>();
			if (d >= 0.0 && d <= 4294967295.0) out = static_cast<uint32_t>(d);
		}
	};
	auto getU64From = [](const picojson::object &src, const char *k, uint64_t &out) {
		auto it = src.find(k);
		if (it != src.end() && it->second.is<double>()) {
			double d = it->second.get<double>();
			if (d >= 0.0) out = static_cast<uint64_t>(d);
		}
	};
	auto getDoubleFrom = [](const picojson::object &src, const char *k, double &out) {
		auto it = src.find(k);
		if (it != src.end() && it->second.is<double>()) out = it->second.get<double>();
	};
	auto getBoolFrom = [](const picojson::object &src, const char *k, bool &out) {
		auto it = src.find(k);
		if (it != src.end() && it->second.is<bool>()) out = it->second.get<bool>();
	};
	auto getStrFrom = [](const picojson::object &src, const char *k, std::string &out) {
		auto it = src.find(k);
		if (it != src.end() && it->second.is<std::string>()) out = it->second.get<std::string>();
	};

	getStr ("serial",                   p.serial);
	getU64 ("serial_hash_hex",          p.serial_hash);
	getStr ("display_name",             p.display_name);
	getBool("enable_diagnostics_only",  p.enable_diagnostics_only);
	getBool("enable_rest_recenter",     p.enable_rest_recenter);
	getBool("enable_trigger_remap",     p.enable_trigger_remap);
	getBool("corrections_enabled",      p.corrections_enabled);

	auto learnedIt = obj.find("learned_paths");
	if (learnedIt != obj.end() && learnedIt->second.is<picojson::array>()) {
		for (const auto &item : learnedIt->second.get<picojson::array>()) {
			if (!item.is<picojson::object>()) continue;
			const auto &lpObj = item.get<picojson::object>();
			LearnedPathRecord r;
			getStrFrom(lpObj, "path", r.path);
			getStrFrom(lpObj, "kind", r.kind);
			getU64From(lpObj, "sample_count", r.sample_count);
			getBoolFrom(lpObj, "ready", r.ready);
			getDoubleFrom(lpObj, "learned_rest_offset", r.learned_rest_offset);
			getDoubleFrom(lpObj, "learned_stddev", r.learned_stddev);
			getDoubleFrom(lpObj, "learned_trigger_min", r.learned_trigger_min);
			getDoubleFrom(lpObj, "learned_trigger_max", r.learned_trigger_max);
			getDoubleFrom(lpObj, "learned_deadzone_radius", r.learned_deadzone_radius);
			getU32From(lpObj, "learned_debounce_us", r.learned_debounce_us);
			getU64From(lpObj, "last_updated_unix", r.last_updated_unix);
			getU32From(lpObj, "drift_shift_resets", r.drift_shift_resets);
			if (!r.path.empty()) p.learned_paths.push_back(std::move(r));
		}
	}

	if (p.serial_hash == 0 && !p.serial.empty()) {
		p.serial_hash = inputhealth::Fnv1a64(p.serial);
	}
	return p;
}

std::string Encode(const DeviceProfile &p)
{
	char hashHex[32];
	snprintf(hashHex, sizeof(hashHex), "%016llx", (unsigned long long)p.serial_hash);

	picojson::object obj;
	obj["serial"]                  = picojson::value(p.serial);
	obj["serial_hash_hex"]         = picojson::value(std::string(hashHex));
	obj["display_name"]            = picojson::value(p.display_name);
	obj["enable_diagnostics_only"] = picojson::value(p.enable_diagnostics_only);
	obj["enable_rest_recenter"]    = picojson::value(p.enable_rest_recenter);
	obj["enable_trigger_remap"]    = picojson::value(p.enable_trigger_remap);
	obj["corrections_enabled"]     = picojson::value(p.corrections_enabled);

	picojson::array learned;
	learned.reserve(p.learned_paths.size());
	for (const auto &r : p.learned_paths) {
		picojson::object item;
		item["path"] = picojson::value(r.path);
		item["kind"] = picojson::value(r.kind);
		item["sample_count"] = picojson::value(static_cast<double>(r.sample_count));
		item["ready"] = picojson::value(r.ready);
		item["learned_rest_offset"] = picojson::value(r.learned_rest_offset);
		item["learned_stddev"] = picojson::value(r.learned_stddev);
		item["learned_trigger_min"] = picojson::value(r.learned_trigger_min);
		item["learned_trigger_max"] = picojson::value(r.learned_trigger_max);
		item["learned_deadzone_radius"] = picojson::value(r.learned_deadzone_radius);
		item["learned_debounce_us"] = picojson::value(static_cast<double>(r.learned_debounce_us));
		item["last_updated_unix"] = picojson::value(static_cast<double>(r.last_updated_unix));
		item["drift_shift_resets"] = picojson::value(static_cast<double>(r.drift_shift_resets));
		learned.push_back(picojson::value(item));
	}
	obj["learned_paths"] = picojson::value(learned);
	return picojson::value(obj).serialize(true);
}

} // namespace

void ProfileStore::LoadAll()
{
	std::wstring dir = ProfilesDir();
	if (dir.empty()) return;

	std::wstring search = dir + L"\\*.json";
	WIN32_FIND_DATAW find_data{};
	HANDLE h = FindFirstFileW(search.c_str(), &find_data);
	if (h == INVALID_HANDLE_VALUE) return;

	do {
		std::wstring path = dir + L"\\" + find_data.cFileName;
		std::ifstream in(path);
		if (!in.is_open()) continue;
		std::stringstream ss;
		ss << in.rdbuf();
		picojson::value v;
		std::string err = picojson::parse(v, ss.str());
		if (!err.empty()) {
			LOG("[profiles] parse error in '%s': %s", Wide2Utf8(path).c_str(), err.c_str());
			continue;
		}
		DeviceProfile p = Decode(v);
		if (p.serial_hash == 0) continue;
		profiles_[p.serial_hash] = std::move(p);
	} while (FindNextFileW(h, &find_data));
	FindClose(h);

	LOG("[profiles] loaded %zu profile(s) from disk", profiles_.size());
}

bool ProfileStore::Save(const DeviceProfile &profile)
{
	if (profile.serial_hash == 0) return false;
	std::wstring dir = ProfilesDir();
	if (dir.empty()) return false;

	std::wstring path    = dir + L"\\" + Utf82Wide(FilenameForHash(profile.serial_hash));
	std::wstring tmpPath = path + L".tmp";

	// Write to a temp file first so a crash mid-write never corrupts the
	// existing profile. MoveFileExW with WRITE_THROUGH makes the rename
	// durable before we return success.
	HANDLE hFile = CreateFileW(
		tmpPath.c_str(),
		GENERIC_WRITE,
		0,
		nullptr,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (hFile == INVALID_HANDLE_VALUE) {
		LOG("[profiles] failed to open tmp '%s' for write (err=%lu)",
			Wide2Utf8(tmpPath).c_str(), GetLastError());
		return false;
	}

	std::string body = Encode(profile);
	DWORD written = 0;
	BOOL ok = WriteFile(hFile, body.data(), static_cast<DWORD>(body.size()), &written, nullptr);
	if (ok) ok = FlushFileBuffers(hFile);
	CloseHandle(hFile);

	if (!ok || written != static_cast<DWORD>(body.size())) {
		LOG("[profiles] write/flush failed for tmp '%s' (err=%lu)",
			Wide2Utf8(tmpPath).c_str(), GetLastError());
		DeleteFileW(tmpPath.c_str());
		return false;
	}

	if (!MoveFileExW(tmpPath.c_str(), path.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		LOG("[profiles] atomic rename failed '%s' -> '%s' (err=%lu)",
			Wide2Utf8(tmpPath).c_str(), Wide2Utf8(path).c_str(), GetLastError());
		DeleteFileW(tmpPath.c_str());
		return false;
	}

	profiles_[profile.serial_hash] = profile;
	return true;
}

DeviceProfile &ProfileStore::GetOrCreate(uint64_t serial_hash)
{
	auto it = profiles_.find(serial_hash);
	if (it != profiles_.end()) return it->second;
	DeviceProfile p;
	p.serial_hash = serial_hash;
	auto &slot = profiles_[serial_hash];
	slot = std::move(p);
	return slot;
}
