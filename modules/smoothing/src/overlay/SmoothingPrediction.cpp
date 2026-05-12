// openvr.h must precede Protocol.h's indirect inclusion chain, see SmoothingPlugin.cpp.
#include <openvr.h>

#include "SmoothingPlugin.h"

#include "Protocol.h"

#include <imgui.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cwctype>
#include <string>

namespace {

struct ExternalSmoothingTool
{
	const wchar_t *exeName;
	const char *humanName;
};

const ExternalSmoothingTool kKnownTools[] = {
	{ L"OpenVR-SmoothTracking.exe", "OpenVR-SmoothTracking" },
	{ L"OpenVRSmoothTracking.exe",  "OpenVR-SmoothTracking" },
	{ L"OVR-SmoothTracking.exe",    "OVR-SmoothTracking" },
	{ L"OVRSmoothTracking.exe",     "OVR-SmoothTracking" },
	{ L"ovr_smooth_tracking.exe",   "OVR-SmoothTracking" },
};

struct SubstringSmoothingPattern
{
	const wchar_t *requireA;
	const wchar_t *requireB;
};

const SubstringSmoothingPattern kSubstringTools[] = {
	{ L"smooth", L"track" },
};

bool DetectExternalSmoothingTool(std::string &outName)
{
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE) return false;

	PROCESSENTRY32W pe{};
	pe.dwSize = sizeof pe;
	bool found = false;

	if (Process32FirstW(snap, &pe)) {
		do {
			for (const auto &tool : kKnownTools) {
				if (_wcsicmp(pe.szExeFile, tool.exeName) == 0) {
					outName = tool.humanName;
					found = true;
					break;
				}
			}
			if (found) break;

			std::wstring lower = pe.szExeFile;
			for (auto &c : lower) c = (wchar_t)towlower(c);
			for (const auto &pat : kSubstringTools) {
				if (lower.find(pat.requireA) != std::wstring::npos &&
				    lower.find(pat.requireB) != std::wstring::npos) {
					int n = WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, nullptr, 0, nullptr, nullptr);
					if (n > 0) {
						outName.assign((size_t)(n - 1), '\0');
						WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, outName.data(), n, nullptr, nullptr);
					}
					found = true;
					break;
				}
			}
		} while (!found && Process32NextW(snap, &pe));
	}

	CloseHandle(snap);
	return found;
}

double MonotonicSeconds()
{
	LARGE_INTEGER freq, ctr;
	if (!QueryPerformanceFrequency(&freq) || !QueryPerformanceCounter(&ctr)) return 0.0;
	return (double)ctr.QuadPart / (double)freq.QuadPart;
}

const char *PrettyTrackingSystem(const char *raw)
{
	if (!raw || !*raw) return "?";
	if (_stricmp(raw, "lighthouse") == 0) return "lighthouse";
	if (_stricmp(raw, "oculus") == 0) return "oculus";
	if (_stricmp(raw, "indexcontroller") == 0) return "lighthouse";
	return raw;
}

} // namespace

void SmoothingPlugin::TickExternalToolDetection()
{
	const double now = MonotonicSeconds();
	if (now - lastExternalScanSeconds_ < 5.0) return;
	lastExternalScanSeconds_ = now;

	std::string detectedName;
	const bool detected = DetectExternalSmoothingTool(detectedName);
	if (detected != externalSmoothingDetected_ ||
	    detectedName != externalSmoothingToolName_) {
		externalSmoothingDetected_ = detected;
		externalSmoothingToolName_ = detected ? detectedName : std::string();
	}
}

void SmoothingPlugin::DrawPredictionTab()
{
	{
		const char *tool = externalSmoothingToolName_.empty()
			? "an external smoothing tool"
			: externalSmoothingToolName_.c_str();
		char statusBuf[256];
		const char *statusText;
		ImVec4 bgColor;
		if (externalSmoothingDetected_) {
			snprintf(statusBuf, sizeof statusBuf, "DETECTED: %s is running.", tool);
			statusText = statusBuf;
			bgColor = ImVec4(0.55f, 0.40f, 0.10f, 1.0f);
		} else {
			statusText = "No external smoothing tool detected.";
			bgColor = ImVec4(0.20f, 0.40f, 0.25f, 1.0f);
		}

		const ImVec2 textSize = ImGui::CalcTextSize(statusText);
		const ImVec2 padding(10.0f, 6.0f);
		ImVec2 cursor = ImGui::GetCursorScreenPos();
		ImVec2 rectMin = cursor;
		ImVec2 rectMax(cursor.x + ImGui::GetContentRegionAvail().x,
		               cursor.y + textSize.y + padding.y * 2.0f);
		ImDrawList *dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(rectMin, rectMax, ImGui::GetColorU32(bgColor), 6.0f);
		dl->AddText(ImVec2(rectMin.x + padding.x, rectMin.y + padding.y),
		            ImGui::GetColorU32(ImVec4(0.95f, 0.95f, 0.95f, 1.0f)), statusText);
		ImGui::Dummy(ImVec2(0, textSize.y + padding.y * 2.0f));
	}

	if (externalSmoothingDetected_) {
		ImGui::Spacing();
		const char *tool = externalSmoothingToolName_.empty()
			? "An external smoothing tool"
			: externalSmoothingToolName_.c_str();
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.85f, 1.0f));
		ImGui::TextWrapped(
			"%s is running. Working alongside it is unsupported -- the two smoothing "
			"layers fight and the result is unpredictable. Close it and use the per-tracker "
			"sliders below.", tool);
		ImGui::PopStyleColor();
	}

	ImGui::Spacing();
	// Headline kept short -- the slider tooltips below carry the detail
	// about 0 vs 100, the HMD lock, and what suppressing calibration
	// trackers does to the SC math. Avoids a wall of intro text on entry.
	ImGui::TextWrapped(
		"Per-tracker prediction suppression. 0 = raw motion, 100 = fully suppressed.");
	ImGui::Spacing();
	ImGui::SeparatorText("Per-tracker smoothness");
	ImGui::Spacing();

	auto *vrSystem = vr::VRSystem();
	if (!vrSystem) {
		ImGui::TextDisabled("(VR system not available)");
		return;
	}

	bool anyShown = false;
	bool dirty = false;
	char buffer[vr::k_unMaxPropertyStringSize];
	for (uint32_t id = 0; id < vr::k_unMaxTrackedDeviceCount; ++id) {
		const auto deviceClass = vrSystem->GetTrackedDeviceClass(id);
		if (deviceClass == vr::TrackedDeviceClass_Invalid) continue;

		vr::ETrackedPropertyError err = vr::TrackedProp_Success;
		vrSystem->GetStringTrackedDeviceProperty(id, vr::Prop_SerialNumber_String, buffer, sizeof buffer, &err);
		if (err != vr::TrackedProp_Success || buffer[0] == 0) continue;
		std::string serial = buffer;

		vrSystem->GetStringTrackedDeviceProperty(id, vr::Prop_RenderModelName_String, buffer, sizeof buffer, &err);
		std::string model = (err == vr::TrackedProp_Success) ? buffer : "";

		vrSystem->GetStringTrackedDeviceProperty(id, vr::Prop_TrackingSystemName_String, buffer, sizeof buffer, &err);
		std::string sys = (err == vr::TrackedProp_Success) ? PrettyTrackingSystem(buffer) : "";

		const bool isHmd = (deviceClass == vr::TrackedDeviceClass_HMD);

		int smoothness = 0;
		auto it = cfg_.trackerSmoothness.find(serial);
		if (it != cfg_.trackerSmoothness.end()) smoothness = it->second;
		if (isHmd) smoothness = 0;

		ImGui::PushID(("trk_" + serial).c_str());
		ImGui::TextWrapped("%s  [%s]  %s",
			model.empty() ? "(unknown model)" : model.c_str(),
			sys.empty() ? "?" : sys.c_str(),
			serial.c_str());
		if (isHmd) {
			ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.55f, 1.0f), "[HMD, locked]");
		}

		ImGui::BeginDisabled(isHmd);
		if (ImGui::SliderInt("smoothness##slider", &smoothness, 0, 100, "%d%%")) {
			if (smoothness <= 0) cfg_.trackerSmoothness.erase(serial);
			else cfg_.trackerSmoothness[serial] = smoothness;
			SendDevicePrediction(id, smoothness);
			dirty = true;
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered()) {
			if (isHmd) {
				ImGui::SetTooltip("Locked to 0. Suppressing HMD prediction would cause judder in your view.");
			} else {
				ImGui::SetTooltip(
					"0 = raw motion (no suppression).\n"
					"100 = fully suppressed (matches the old binary 'freeze' behaviour).\n"
					"Try around 50-75 for IMU-based trackers that feel jittery.");
			}
		}
		ImGui::Spacing();
		ImGui::PopID();
		anyShown = true;
	}
	if (!anyShown) ImGui::TextDisabled("(No tracked devices found.)");

	if (dirty) SaveConfig(cfg_);
}
