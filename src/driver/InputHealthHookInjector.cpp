#include "InputHealthHookInjector.h"

#include "Hooking.h"
#include "InterfaceHookInjector.h"   // InterfaceHooks::DetourScope
#include "Logging.h"
#include "ServerTrackedDeviceProvider.h"
#include "inputhealth/PerComponentStats.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// =============================================================================
// File-scope state.
//
// Stage 1D: the per-handle map graduates from {path, flags} to a full
// ComponentStats with Welford / Page-Hinkley / EWMA rolling-min / polar
// histogram. The Update detour bodies feed these on every sample when
// master_enabled is set in the driver-side config; with master_enabled off
// the detours fast-path through unmodified.
//
// Snapshot publishing (driver -> overlay) is NOT in this stage. The
// background worker thread that consumes these stats and emits detection-
// category decisions also lands later. Stage 1D's purpose is to confirm
// the per-tick math is wired up and stays inside the per-call budget
// during real-world testing; Stage 2 builds the overlay client that reads
// the snapshots.
// =============================================================================

static std::atomic<ServerTrackedDeviceProvider *> g_driver{nullptr};
static std::unordered_map<vr::VRInputComponentHandle_t, inputhealth::ComponentStats> g_componentStats;
static std::mutex                                                                    g_componentMutex;

// One-shot install / probe log markers.
static std::atomic<bool> g_firstCreateBoolLogged{false};
static std::atomic<bool> g_firstCreateScalarLogged{false};

// =============================================================================
// VirtualQuery-guarded probes (mirror SkeletalHookInjector's pair).
// =============================================================================

static bool IsAddressReadable(const void *addr, size_t bytes)
{
	if (!addr) return false;
	MEMORY_BASIC_INFORMATION mbi{};
	if (!VirtualQuery(addr, &mbi, sizeof mbi)) return false;
	if (mbi.State != MEM_COMMIT) return false;
	DWORD prot = mbi.Protect & 0xFF;
	if (prot == 0 || (prot & PAGE_NOACCESS) || (prot & PAGE_GUARD)) return false;
	auto regionEnd = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
	auto needEnd   = (uintptr_t)addr + bytes;
	return needEnd <= regionEnd;
}

static void LogVirtualQueryRegion(const char *tag, const void *addr)
{
	if (!addr) {
		LOG("[inputhealth-probe] %s: addr=NULL", tag);
		return;
	}
	MEMORY_BASIC_INFORMATION mbi{};
	if (!VirtualQuery(addr, &mbi, sizeof mbi)) {
		LOG("[inputhealth-probe] %s: addr=%p VirtualQuery FAILED (err=%lu)", tag, addr, GetLastError());
		return;
	}
	LOG("[inputhealth-probe] %s: addr=%p base=%p size=0x%llx state=0x%lx prot=0x%lx type=0x%lx",
		tag, addr, mbi.BaseAddress, (unsigned long long)mbi.RegionSize,
		mbi.State, mbi.Protect, mbi.Type);
}

// =============================================================================
// Hook<> instances (slots match IVRDriverInput_003 layout in the bundled SDK).
// =============================================================================

static Hook<vr::EVRInputError(*)(vr::IVRDriverInput *, vr::PropertyContainerHandle_t, const char *, vr::VRInputComponentHandle_t *)>
	CreateBooleanHook("IVRDriverInput::CreateBooleanComponent");
static Hook<vr::EVRInputError(*)(vr::IVRDriverInput *, vr::VRInputComponentHandle_t, bool, double)>
	UpdateBooleanHook("IVRDriverInput::UpdateBooleanComponent");
static Hook<vr::EVRInputError(*)(vr::IVRDriverInput *, vr::PropertyContainerHandle_t, const char *, vr::VRInputComponentHandle_t *, vr::EVRScalarType, vr::EVRScalarUnits)>
	CreateScalarHook("IVRDriverInput::CreateScalarComponent");
static Hook<vr::EVRInputError(*)(vr::IVRDriverInput *, vr::VRInputComponentHandle_t, float, double)>
	UpdateScalarHook("IVRDriverInput::UpdateScalarComponent");

// =============================================================================
// Helpers.
// =============================================================================

// Wall-clock microseconds since QPC epoch. Cheap and monotonic.
static uint64_t QpcMicros()
{
	static LARGE_INTEGER s_freq{};
	if (s_freq.QuadPart == 0) QueryPerformanceFrequency(&s_freq);
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	if (s_freq.QuadPart == 0) return 0;
	return static_cast<uint64_t>(t.QuadPart * 1000000ULL / s_freq.QuadPart);
}

// Find an existing scalar handle whose path stem and complementary axis role
// match the new handle. Caller must hold g_componentMutex. Returns 0 (the
// invalid component handle) if no partner exists yet.
static vr::VRInputComponentHandle_t FindStickPartner_locked(
	const std::string &stem,
	inputhealth::AxisRole this_role)
{
	const inputhealth::AxisRole want = (this_role == inputhealth::AxisRole::StickX)
		? inputhealth::AxisRole::StickY
		: inputhealth::AxisRole::StickX;
	for (auto &kv : g_componentStats) {
		auto &peer = kv.second;
		if (!peer.is_scalar) continue;
		if (peer.axis_role != want) continue;
		// Match on stem: peer's path with its trailing /x or /y stripped.
		std::string peerStem;
		(void)inputhealth::ClassifyAxisRole(peer.path, peerStem);
		if (peerStem == stem) return kv.first;
	}
	return 0;
}

// Tunable Page-Hinkley parameters. Defaults from the research doc Q3:
// alpha set so the EWMA half-life is ~30s at 250 Hz observation rate;
// delta = 0.002 (1/5 of a typical "still considered rest" envelope of 0.01);
// lambda = 0.05 (gives ARL ~10 hours for a step shift of 0.01, per Q8).
// These are starting points; per-category retuning lands once the snapshot
// publish path exists and field telemetry can be sampled.
static inline inputhealth::PageHinkleyParams DefaultDriftParams()
{
	inputhealth::PageHinkleyParams p;
	p.alpha               = 0.0001;  // ~30s half-life at 250 Hz: 1 - exp(-1/(250*30)) ~ 1.3e-4
	p.delta               = 0.002;
	p.lambda              = 0.05;
	p.one_sided_positive  = false;
	return p;
}

// Asymmetric rolling-min decay: 24-hour half-life at 250 Hz works out to
// alpha ~3.2e-8 per sample. Picks up genuinely stuck triggers without being
// fooled by transient near-zero crossings.
static constexpr double kRestMinDecay = 3.2e-8;

// Threshold below which a sample is treated as "in rest" for the purposes
// of updating rest_min. Conservative: 0.1 catches the rest band of typical
// analog triggers (factory-stocked rest near 0.0, noise typically <0.05)
// without including mid-pull samples.
static constexpr float kRestThreshold = 0.1f;

// =============================================================================
// Detours.
// =============================================================================

static vr::EVRInputError DetourCreateBooleanComponent(
	vr::IVRDriverInput *_this,
	vr::PropertyContainerHandle_t ulContainer,
	const char *pchName,
	vr::VRInputComponentHandle_t *pHandle)
{
	InterfaceHooks::DetourScope _scope;
	auto result = CreateBooleanHook.originalFunc(_this, ulContainer, pchName, pHandle);

	bool firstExpected = false;
	if (g_firstCreateBoolLogged.compare_exchange_strong(firstExpected, true)) {
		LOG("[inputhealth] FIRST CreateBooleanComponent: result=%d this=%p container=%llu name='%s' outHandle=%llu",
			(int)result, (void*)_this,
			(unsigned long long)ulContainer,
			pchName ? pchName : "(null)",
			pHandle ? (unsigned long long)*pHandle : 0ULL);
	}

	if (result == vr::VRInputError_None
		&& pHandle && *pHandle != vr::k_ulInvalidInputComponentHandle
		&& pchName)
	{
		std::lock_guard<std::mutex> lk(g_componentMutex);
		auto &stats = g_componentStats[*pHandle];
		stats.path       = pchName;
		stats.is_boolean = true;
		stats.is_scalar  = false;
		stats.first_update_logged = false;
	}
	return result;
}

static vr::EVRInputError DetourUpdateBooleanComponent(
	vr::IVRDriverInput *_this,
	vr::VRInputComponentHandle_t ulComponent,
	bool bNewValue,
	double fTimeOffset)
{
	InterfaceHooks::DetourScope _scope;

	auto *driver = g_driver.load(std::memory_order_acquire);
	if (driver) {
		const auto cfg = driver->GetInputHealthConfig();
		if (cfg.master_enabled) {
			std::lock_guard<std::mutex> lk(g_componentMutex);
			auto it = g_componentStats.find(ulComponent);
			if (it != g_componentStats.end()) {
				auto &s = it->second;
				if (bNewValue && !s.last_boolean) ++s.press_count;
				s.last_boolean    = bNewValue;
				s.last_update_us  = QpcMicros();
				if (!s.first_update_logged) {
					s.first_update_logged = true;
					LOG("[inputhealth] first UpdateBooleanComponent on handle=%llu path='%s' value=%d",
						(unsigned long long)ulComponent, s.path.c_str(), (int)bNewValue);
				}
			}
		}
	}

	return UpdateBooleanHook.originalFunc(_this, ulComponent, bNewValue, fTimeOffset);
}

static vr::EVRInputError DetourCreateScalarComponent(
	vr::IVRDriverInput *_this,
	vr::PropertyContainerHandle_t ulContainer,
	const char *pchName,
	vr::VRInputComponentHandle_t *pHandle,
	vr::EVRScalarType eType,
	vr::EVRScalarUnits eUnits)
{
	InterfaceHooks::DetourScope _scope;
	auto result = CreateScalarHook.originalFunc(_this, ulContainer, pchName, pHandle, eType, eUnits);

	bool firstExpected = false;
	if (g_firstCreateScalarLogged.compare_exchange_strong(firstExpected, true)) {
		LOG("[inputhealth] FIRST CreateScalarComponent: result=%d this=%p container=%llu name='%s' type=%d units=%d outHandle=%llu",
			(int)result, (void*)_this,
			(unsigned long long)ulContainer,
			pchName ? pchName : "(null)",
			(int)eType, (int)eUnits,
			pHandle ? (unsigned long long)*pHandle : 0ULL);
	}

	if (result == vr::VRInputError_None
		&& pHandle && *pHandle != vr::k_ulInvalidInputComponentHandle
		&& pchName)
	{
		std::string path = pchName;
		std::string stem;
		const auto role = inputhealth::ClassifyAxisRole(path, stem);

		std::lock_guard<std::mutex> lk(g_componentMutex);
		auto &stats = g_componentStats[*pHandle];
		stats.path       = std::move(path);
		stats.is_boolean = false;
		stats.is_scalar  = true;
		stats.first_update_logged = false;
		stats.axis_role  = role;
		stats.partner_handle = 0;

		if (role == inputhealth::AxisRole::StickX || role == inputhealth::AxisRole::StickY) {
			vr::VRInputComponentHandle_t partner = FindStickPartner_locked(stem, role);
			if (partner != 0) {
				stats.partner_handle                       = partner;
				g_componentStats[partner].partner_handle   = *pHandle;
				LOG("[inputhealth] paired stick axes: stem='%s' xHandle=%llu yHandle=%llu",
					stem.c_str(),
					role == inputhealth::AxisRole::StickX ? (unsigned long long)*pHandle  : (unsigned long long)partner,
					role == inputhealth::AxisRole::StickY ? (unsigned long long)*pHandle  : (unsigned long long)partner);
			}
		}
	}
	return result;
}

static vr::EVRInputError DetourUpdateScalarComponent(
	vr::IVRDriverInput *_this,
	vr::VRInputComponentHandle_t ulComponent,
	float fNewValue,
	double fTimeOffset)
{
	InterfaceHooks::DetourScope _scope;

	auto *driver = g_driver.load(std::memory_order_acquire);
	if (driver) {
		const auto cfg = driver->GetInputHealthConfig();
		if (cfg.master_enabled) {
			const uint64_t now_us = QpcMicros();
			std::lock_guard<std::mutex> lk(g_componentMutex);
			auto it = g_componentStats.find(ulComponent);
			if (it != g_componentStats.end()) {
				auto &s = it->second;

				// Stage 1D per-tick budget keeps to the items the research
				// doc Q6 admits onto the detour thread: ring push (skipped
				// for now -- ring-buffer wiring lands with the snapshot
				// path), Welford update, Page-Hinkley update, EWMA-min
				// update for rest samples, polar bin update for paired
				// axes. Heavy work (geometric median, hull rebuild, SPRT)
				// runs on the background worker (also not yet wired).
				WelfordUpdate(s.welford, static_cast<double>(fNewValue));
				const auto driftParams = DefaultDriftParams();
				PageHinkleyUpdate(s.ph_drift, driftParams, static_cast<double>(fNewValue));
				if (fNewValue < kRestThreshold && fNewValue > -kRestThreshold) {
					EWMARollingMinUpdate(s.rest_min,
						static_cast<double>(fNewValue),
						kRestMinDecay);
				}

				// Polar histogram is owned by the X-side of a paired stick.
				// On a Y-side update, look up the X partner and update its
				// histogram with the (partner_last_x, this_y) tuple. On an
				// X-side update, do the same with this value as x and the
				// partner's last value as y. The histogram thus integrates
				// samples from both axes against a slight (~1 tick) cross-
				// axis lag, which is well below the 10 deg bin resolution.
				if (s.axis_role == inputhealth::AxisRole::StickX) {
					float partner_y = 0.0f;
					auto pit = g_componentStats.find(s.partner_handle);
					if (pit != g_componentStats.end()) partner_y = pit->second.last_value;
					PolarHistogramUpdate(s.polar,
						static_cast<double>(fNewValue),
						static_cast<double>(partner_y),
						now_us, kRestMinDecay);
				} else if (s.axis_role == inputhealth::AxisRole::StickY) {
					auto pit = g_componentStats.find(s.partner_handle);
					if (pit != g_componentStats.end() && pit->second.axis_role == inputhealth::AxisRole::StickX) {
						PolarHistogramUpdate(pit->second.polar,
							static_cast<double>(pit->second.last_value),
							static_cast<double>(fNewValue),
							now_us, kRestMinDecay);
					}
				}

				s.last_value     = fNewValue;
				s.last_update_us = now_us;

				if (!s.first_update_logged) {
					s.first_update_logged = true;
					LOG("[inputhealth] first UpdateScalarComponent on handle=%llu path='%s' value=%.4f role=%d",
						(unsigned long long)ulComponent, s.path.c_str(),
						fNewValue, (int)s.axis_role);
				}
			}
		}
	}

	return UpdateScalarHook.originalFunc(_this, ulComponent, fNewValue, fTimeOffset);
}

// =============================================================================
// Public API.
// =============================================================================

namespace inputhealth {

void Init(ServerTrackedDeviceProvider *driver)
{
	g_driver.store(driver, std::memory_order_release);
	g_firstCreateBoolLogged.store(false, std::memory_order_release);
	g_firstCreateScalarLogged.store(false, std::memory_order_release);
	{
		std::lock_guard<std::mutex> lk(g_componentMutex);
		g_componentStats.clear();
	}
	LOG("[inputhealth] Init: subsystem armed (driver=%p), awaiting IVRDriverInput interface queries", (void*)driver);
}

void Shutdown()
{
	{
		std::lock_guard<std::mutex> lk(g_componentMutex);
		g_componentStats.clear();
	}
	// g_driver intentionally NOT cleared (same rationale as skeletal
	// subsystem: ServerTrackedDeviceProvider outlives the DLL across reload).
	LOG("[inputhealth] Shutdown: subsystem disarmed");
}

void TryInstallScalarBooleanHooks(void *iface)
{
	if (!iface) return;
	if (g_driver.load(std::memory_order_acquire) == nullptr) return;

	bool createBoolAlready  = IHook::Exists(CreateBooleanHook.name);
	bool updateBoolAlready  = IHook::Exists(UpdateBooleanHook.name);
	bool createScalarAlready = IHook::Exists(CreateScalarHook.name);
	bool updateScalarAlready = IHook::Exists(UpdateScalarHook.name);
	if (createBoolAlready && updateBoolAlready && createScalarAlready && updateScalarAlready) return;

	LOG("[inputhealth] TryInstallScalarBooleanHooks invoked: iface=%p createBool=%d updateBool=%d createScalar=%d updateScalar=%d",
		iface, (int)createBoolAlready, (int)updateBoolAlready,
		(int)createScalarAlready, (int)updateScalarAlready);

	if (!IsAddressReadable(iface, sizeof(void *))) {
		LOG("[inputhealth] iface %p not readable; aborting install", iface);
		return;
	}
	void **vtable = *((void ***)iface);
	if (!IsAddressReadable(vtable, sizeof(void *) * 7)) {
		LOG("[inputhealth] vtable %p not readable for 7 slots; aborting install (iface=%p)",
			(void *)vtable, iface);
		return;
	}
	intptr_t spread = (intptr_t)vtable[6] - (intptr_t)vtable[0];
	if (spread < 0) spread = -spread;
	if (spread > 0x10000) {
		LOG("[inputhealth] vtable spread |slot6 - slot0| = 0x%llx bytes (>64KB); refusing to install (iface=%p slot0=%p slot6=%p)",
			(unsigned long long)spread, iface, vtable[0], vtable[6]);
		return;
	}

	LOG("[inputhealth] pre-install snapshot: vtable[0]=%p [1]=%p [2]=%p [3]=%p spread=0x%llx",
		vtable[0], vtable[1], vtable[2], vtable[3], (unsigned long long)spread);

	if (!createBoolAlready) {
		CreateBooleanHook.CreateHookInObjectVTable(iface, 0, &DetourCreateBooleanComponent);
		IHook::Register(&CreateBooleanHook);
	}
	if (!updateBoolAlready) {
		UpdateBooleanHook.CreateHookInObjectVTable(iface, 1, &DetourUpdateBooleanComponent);
		IHook::Register(&UpdateBooleanHook);
	}
	if (!createScalarAlready) {
		CreateScalarHook.CreateHookInObjectVTable(iface, 2, &DetourCreateScalarComponent);
		IHook::Register(&CreateScalarHook);
	}
	if (!updateScalarAlready) {
		UpdateScalarHook.CreateHookInObjectVTable(iface, 3, &DetourUpdateScalarComponent);
		IHook::Register(&UpdateScalarHook);
	}

	LogVirtualQueryRegion("public_vtable_slot0", vtable[0]);
	LogVirtualQueryRegion("public_vtable_slot1", vtable[1]);
	LogVirtualQueryRegion("public_vtable_slot2", vtable[2]);
	LogVirtualQueryRegion("public_vtable_slot3", vtable[3]);

	LOG("[inputhealth] installed PUBLIC IVRDriverInput hooks: vtable[0]=CreateBool vtable[1]=UpdateBool vtable[2]=CreateScalar vtable[3]=UpdateScalar -- waiting for first calls");
}

} // namespace inputhealth
