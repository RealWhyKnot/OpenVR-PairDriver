#include "InputHealthHookInjector.h"

#include "Hooking.h"
#include "InterfaceHookInjector.h"   // InterfaceHooks::DetourScope
#include "Logging.h"
#include "ServerTrackedDeviceProvider.h"
#include "inputhealth/PerComponentStats.h"
#include "inputhealth/SerialHash.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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
static std::atomic<uint64_t> g_hotPathLockSkips{0};
static std::atomic<uint64_t> g_hotPathObservationErrors{0};

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
	inputhealth::AxisRole this_role,
	vr::PropertyContainerHandle_t container)
{
	const inputhealth::AxisRole want = (this_role == inputhealth::AxisRole::StickX)
		? inputhealth::AxisRole::StickY
		: inputhealth::AxisRole::StickX;
	for (auto &kv : g_componentStats) {
		auto &peer = kv.second;
		if (!peer.is_scalar) continue;
		if (peer.axis_role != want) continue;
		if (peer.container_handle != container) continue;
		// Match on stem: peer's path with its trailing /x or /y stripped.
		std::string peerStem;
		(void)inputhealth::ClassifyAxisRole(peer.path, peerStem);
		if (peerStem == stem) return kv.first;
	}
	return 0;
}

static uint64_t ResolveSerialHash(vr::PropertyContainerHandle_t container)
{
	if (container == vr::k_ulInvalidPropertyContainer) return 0;
	auto *helpers = vr::VRProperties();
	if (!helpers) return 0;

	vr::ETrackedPropertyError err = vr::TrackedProp_Success;
	std::string serial = helpers->GetStringProperty(container, vr::Prop_SerialNumber_String, &err);
	if (err != vr::TrackedProp_Success || serial.empty()) return 0;
	return inputhealth::Fnv1a64(serial);
}

static void LogHotPathLockSkip(const char *kind)
{
	const uint64_t n = g_hotPathLockSkips.fetch_add(1, std::memory_order_relaxed) + 1;
	if (n == 1 || n == 100 || (n % 10000) == 0) {
		LOG("[inputhealth] skipped %s observation because stats mutex was busy (count=%llu); forwarded raw value",
			kind, (unsigned long long)n);
	}
}

static void LogHotPathObservationError(const char *kind, const char *what)
{
	const uint64_t n = g_hotPathObservationErrors.fetch_add(1, std::memory_order_relaxed) + 1;
	if (n == 1 || n == 100 || (n % 10000) == 0) {
		LOG("[inputhealth] disabled %s observation for this tick after error '%s' (count=%llu); forwarded raw value",
			kind, what ? what : "unknown",
			(unsigned long long)n);
	}
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
		try {
			std::lock_guard<std::mutex> lk(g_componentMutex);
			auto &stats = g_componentStats[*pHandle];
			stats.path             = pchName;
			stats.is_boolean       = true;
			stats.is_scalar        = false;
			stats.first_update_logged = false;
			stats.container_handle = ulContainer;
			stats.device_serial_hash = ResolveSerialHash(ulContainer);
		} catch (const std::exception &e) {
			LogHotPathObservationError("boolean-create", e.what());
		} catch (...) {
			LogHotPathObservationError("boolean-create", "non-std exception");
		}
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
		try {
			const auto cfg = driver->GetInputHealthConfig();
			if (cfg.master_enabled) {
				if (!g_componentMutex.try_lock()) {
					LogHotPathLockSkip("boolean");
				} else {
					std::lock_guard<std::mutex> lk(g_componentMutex, std::adopt_lock);
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
		} catch (const std::exception &e) {
			LogHotPathObservationError("boolean", e.what());
		} catch (...) {
			LogHotPathObservationError("boolean", "non-std exception");
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
		try {
			std::string path = pchName;
			std::string stem;
			const auto role = inputhealth::ClassifyAxisRole(path, stem);

			std::lock_guard<std::mutex> lk(g_componentMutex);
			auto &stats = g_componentStats[*pHandle];
			stats.path             = std::move(path);
			stats.is_boolean       = false;
			stats.is_scalar        = true;
			stats.first_update_logged = false;
			stats.axis_role        = role;
			stats.partner_handle   = 0;
			stats.container_handle = ulContainer;
			stats.device_serial_hash = ResolveSerialHash(ulContainer);

			if (role == inputhealth::AxisRole::StickX || role == inputhealth::AxisRole::StickY) {
				vr::VRInputComponentHandle_t partner = FindStickPartner_locked(stem, role, ulContainer);
				if (partner != 0) {
					stats.partner_handle                       = partner;
					g_componentStats[partner].partner_handle   = *pHandle;
					LOG("[inputhealth] paired stick axes: stem='%s' xHandle=%llu yHandle=%llu",
						stem.c_str(),
						role == inputhealth::AxisRole::StickX ? (unsigned long long)*pHandle  : (unsigned long long)partner,
						role == inputhealth::AxisRole::StickY ? (unsigned long long)*pHandle  : (unsigned long long)partner);
				}
			}
		} catch (const std::exception &e) {
			LogHotPathObservationError("scalar-create", e.what());
		} catch (...) {
			LogHotPathObservationError("scalar-create", "non-std exception");
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
		try {
			const auto cfg = driver->GetInputHealthConfig();
			if (cfg.master_enabled) {
				const uint64_t now_us = QpcMicros();
				if (!g_componentMutex.try_lock()) {
					LogHotPathLockSkip("scalar");
				} else {
					std::lock_guard<std::mutex> lk(g_componentMutex, std::adopt_lock);
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
						if (!s.scalar_range_initialized) {
							s.scalar_range_initialized = true;
							s.observed_min = fNewValue;
							s.observed_max = fNewValue;
						} else {
							if (fNewValue < s.observed_min) s.observed_min = fNewValue;
							if (fNewValue > s.observed_max) s.observed_max = fNewValue;
						}
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
		} catch (const std::exception &e) {
			LogHotPathObservationError("scalar", e.what());
		} catch (...) {
			LogHotPathObservationError("scalar", "non-std exception");
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
	g_hotPathLockSkips.store(0, std::memory_order_release);
	g_hotPathObservationErrors.store(0, std::memory_order_release);
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

// Helper: copy one ComponentStats into the wire-format snapshot body. The
// caller owns the body; this function only translates fields. Path is
// truncated to fit INPUTHEALTH_PATH_LEN-1 bytes; OpenVR component paths are
// well under that in practice.
static void FillSnapshotBody(
	vr::VRInputComponentHandle_t handle,
	const ComponentStats &s,
	protocol::InputHealthSnapshotBody &out)
{
	std::memset(&out, 0, sizeof(out));

	out.handle             = static_cast<uint64_t>(handle);
	out.container_handle   = static_cast<uint64_t>(s.container_handle);
	out.device_serial_hash = s.device_serial_hash;
	out.partner_handle     = static_cast<uint64_t>(s.partner_handle);

	const size_t plen = std::min<size_t>(s.path.size(),
		protocol::INPUTHEALTH_PATH_LEN - 1);
	if (plen > 0) std::memcpy(out.path, s.path.data(), plen);
	out.path[plen] = '\0';

	out.is_scalar              = s.is_scalar  ? 1 : 0;
	out.is_boolean             = s.is_boolean ? 1 : 0;
	out.axis_role              = static_cast<uint8_t>(s.axis_role);
	out.ph_initialized         = s.ph_drift.initialized        ? 1 : 0;
	out.ph_triggered           = s.ph_drift.triggered          ? 1 : 0;
	out.ph_triggered_positive  = s.ph_drift.triggered_positive ? 1 : 0;
	out.rest_min_initialized   = s.rest_min.initialized        ? 1 : 0;
	out.last_boolean           = s.last_boolean ? 1 : 0;

	out.welford_count = s.welford.count;
	out.welford_mean  = s.welford.mean;
	out.welford_m2    = s.welford.m2;

	out.ph_mean = s.ph_drift.mean;
	out.ph_pos  = s.ph_drift.ph_pos;
	out.ph_neg  = s.ph_drift.ph_neg;

	out.rest_min = s.rest_min.value;

	out.last_value     = s.last_value;
	out.last_update_us = s.last_update_us;
	out.press_count    = s.press_count;
	out.scalar_range_initialized = s.scalar_range_initialized ? 1 : 0;
	out.observed_min   = s.observed_min;
	out.observed_max   = s.observed_max;

	for (int i = 0; i < protocol::INPUTHEALTH_POLAR_BIN_COUNT && i < kBinCount; ++i) {
		out.polar_max_r[i]          = s.polar.bins[i].max_r;
		out.polar_count[i]          = s.polar.bins[i].count;
		out.polar_last_update_us[i] = s.polar.bins[i].last_update_us;
	}
	out.polar_global_max_r = s.polar.global_max_r;
}

void StageSnapshots(std::vector<StagedSnapshot> &out)
{
	std::lock_guard<std::mutex> lk(g_componentMutex);
	out.reserve(out.size() + g_componentStats.size());
	for (const auto &kv : g_componentStats) {
		StagedSnapshot rec;
		rec.handle = static_cast<uint64_t>(kv.first);
		FillSnapshotBody(kv.first, kv.second, rec.body);
		out.push_back(rec);
	}
}

void ApplyResetRequest(const protocol::InputHealthResetStats &req)
{
	// Pass 1: snapshot (handle, container, cached_hash) without holding the
	// mutex during the VRProperties query. The detour path is a hot path so
	// we keep the critical section short.
	struct Snapshot {
		vr::VRInputComponentHandle_t handle;
		vr::PropertyContainerHandle_t container;
		uint64_t                      hash;
	};
	std::vector<Snapshot> snap;
	{
		std::lock_guard<std::mutex> lk(g_componentMutex);
		snap.reserve(g_componentStats.size());
		for (const auto &kv : g_componentStats) {
			snap.push_back({kv.first, kv.second.container_handle, kv.second.device_serial_hash});
		}
	}

	// Pass 2: lazily resolve any unresolved hashes via VRProperties.
	auto *helpers = vr::VRProperties();
	for (auto &s : snap) {
		if (s.hash != 0) continue;
		if (s.container == vr::k_ulInvalidPropertyContainer) continue;
		if (!helpers) continue;
		vr::ETrackedPropertyError err = vr::TrackedProp_Success;
		std::string serial = helpers->GetStringProperty(s.container, vr::Prop_SerialNumber_String, &err);
		if (err == vr::TrackedProp_Success && !serial.empty()) {
			s.hash = Fnv1a64(serial);
		}
	}

	// Pass 3: re-take the mutex, fold resolved hashes back in, and reset
	// matching entries. Entries that were added or removed between passes
	// are handled correctly: we only act on handles still present in the
	// map.
	const bool match_all = (req.device_serial_hash == kSerialHashAllDevices);
	int matched = 0;
	int reset_passive_count = 0;
	{
		std::lock_guard<std::mutex> lk(g_componentMutex);
		for (auto &s : snap) {
			auto it = g_componentStats.find(s.handle);
			if (it == g_componentStats.end()) continue;
			if (s.hash != 0 && it->second.device_serial_hash == 0) {
				it->second.device_serial_hash = s.hash;
			}
			if (!match_all && it->second.device_serial_hash != req.device_serial_hash) continue;
			++matched;
			if (req.reset_passive) {
				ComponentStatsResetPassive(it->second);
				++reset_passive_count;
			}
			// reset_active and reset_curves: nothing to do until the
			// wizard-prior and compensation-curve state lives in this
			// subsystem (Stage 3 / Stage 4 territory).
		}
	}

	LOG("[inputhealth] HandleResetInputHealthStats: serial_hash=0x%016llx passive=%d active=%d curves=%d -> matched=%d passive_reset=%d total_components=%zu",
		(unsigned long long)req.device_serial_hash,
		(int)req.reset_passive, (int)req.reset_active, (int)req.reset_curves,
		matched, reset_passive_count, snap.size());
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

	bool createBoolReady = createBoolAlready;
	bool updateBoolReady = updateBoolAlready;
	bool createScalarReady = createScalarAlready;
	bool updateScalarReady = updateScalarAlready;

	if (!createBoolAlready) {
		createBoolReady = CreateBooleanHook.CreateHookInObjectVTable(iface, 0, &DetourCreateBooleanComponent);
		if (createBoolReady) IHook::Register(&CreateBooleanHook);
	}
	if (!updateBoolAlready) {
		updateBoolReady = UpdateBooleanHook.CreateHookInObjectVTable(iface, 1, &DetourUpdateBooleanComponent);
		if (updateBoolReady) IHook::Register(&UpdateBooleanHook);
	}
	if (!createScalarAlready) {
		createScalarReady = CreateScalarHook.CreateHookInObjectVTable(iface, 2, &DetourCreateScalarComponent);
		if (createScalarReady) IHook::Register(&CreateScalarHook);
	}
	if (!updateScalarAlready) {
		updateScalarReady = UpdateScalarHook.CreateHookInObjectVTable(iface, 3, &DetourUpdateScalarComponent);
		if (updateScalarReady) IHook::Register(&UpdateScalarHook);
	}

	LogVirtualQueryRegion("public_vtable_slot0", vtable[0]);
	LogVirtualQueryRegion("public_vtable_slot1", vtable[1]);
	LogVirtualQueryRegion("public_vtable_slot2", vtable[2]);
	LogVirtualQueryRegion("public_vtable_slot3", vtable[3]);

	if (createBoolReady && updateBoolReady && createScalarReady && updateScalarReady) {
		LOG("[inputhealth] installed PUBLIC IVRDriverInput hooks: vtable[0]=CreateBool vtable[1]=UpdateBool vtable[2]=CreateScalar vtable[3]=UpdateScalar -- waiting for first calls");
	} else {
		LOG("[inputhealth] partial IVRDriverInput hook install; createBool=%d updateBool=%d createScalar=%d updateScalar=%d -- missing hooks stay pass-through",
			(int)createBoolReady, (int)updateBoolReady,
			(int)createScalarReady, (int)updateScalarReady);
	}
}

} // namespace inputhealth
