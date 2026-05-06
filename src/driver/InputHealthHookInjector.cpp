#include "InputHealthHookInjector.h"

#include "Hooking.h"
#include "InterfaceHookInjector.h"   // InterfaceHooks::DetourScope
#include "Logging.h"
#include "ServerTrackedDeviceProvider.h"

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
// Stage 1B keeps the surface minimal: a handle->path map so the user can
// confirm each input is being seen, plus first-call-per-handle logging so
// hook-fire validation in real-world testing has a clean signal. No stat
// updates yet -- the per-component statistics structures (Welford,
// Page-Hinkley, EWMA-min, polar histogram) and their wiring into the
// detour bodies arrive in Stage 1C.
// =============================================================================

static std::atomic<ServerTrackedDeviceProvider *> g_driver{nullptr};

// VRInputComponentHandle_t -> component path string. Populated by the
// Create{Boolean,Scalar}Component detours by capturing pchName at create time.
// Inspected on the first Update*Component call per handle so the per-input
// "first frame seen" log includes the path. After Stage 1C lands the same map
// keys the per-component statistics tables.
struct ComponentInfo
{
	std::string path;
	bool        is_scalar = false;
};
static std::unordered_map<vr::VRInputComponentHandle_t, ComponentInfo> g_componentInfo;
static std::mutex                                                      g_componentMutex;

// Per-handle "have we logged the first Update on this component?" flags.
// Held inside g_componentMutex so the look-up + flip is atomic with the path
// read above. The set keys here are the same as g_componentInfo's keys --
// kept separate so a re-create of the same handle (driver reload) re-arms
// the first-call log.
static std::unordered_map<vr::VRInputComponentHandle_t, bool> g_firstUpdateLogged;

// One-shot log markers so the install path doesn't spam when SteamVR queries
// the same vtable repeatedly (which it does once per driver loaded).
static std::atomic<bool> g_firstCreateBoolLogged{false};
static std::atomic<bool> g_firstCreateScalarLogged{false};

// =============================================================================
// VirtualQuery-guarded probes. Same role as the matching pair in
// SkeletalHookInjector: confirm the iface points at a real C++ object before
// patching. Duplicated here rather than shared via a header so each subsystem
// stays self-contained; the functions are small.
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
// Hook<> instances. Slot indices below match IVRDriverInput_003 in the bundled
// SDK (lib/openvr/headers/openvr_driver.h):
//   slot 0  CreateBooleanComponent
//   slot 1  UpdateBooleanComponent
//   slot 2  CreateScalarComponent
//   slot 3  UpdateScalarComponent
//   slot 4  CreateHapticComponent           (not hooked)
//   slot 5  CreateSkeletonComponent          (skeletal subsystem)
//   slot 6  UpdateSkeletonComponent          (skeletal subsystem)
// IVRDriverInput_004 keeps the same first 7 slots and adds Pose+EyeTracking
// at slots 7-10, so the substring-match install path catches both versions.
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
// Detours. Stage 1B: pass-through forwarding only. Each Create* records the
// (handle, path, type) tuple; each Update* logs the first call per handle so
// real-world hook-fire validation has a clean signal.
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
		auto &info = g_componentInfo[*pHandle];
		info.path = pchName;
		info.is_scalar = false;
		g_firstUpdateLogged[*pHandle] = false;
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

	// Stage 1B: pass-through. The first Update for any component prints a
	// one-shot log line so the deploy validator can grep for hook-fire
	// confirmation. Stage 1C replaces the no-op body with the per-component
	// statistics update (Welford + Page-Hinkley + button-press SPRT).
	bool needFirstLog = false;
	std::string path;
	{
		std::lock_guard<std::mutex> lk(g_componentMutex);
		auto it = g_firstUpdateLogged.find(ulComponent);
		if (it != g_firstUpdateLogged.end() && !it->second) {
			it->second = true;
			needFirstLog = true;
			auto infoIt = g_componentInfo.find(ulComponent);
			if (infoIt != g_componentInfo.end()) path = infoIt->second.path;
		}
	}
	if (needFirstLog) {
		LOG("[inputhealth] first UpdateBooleanComponent on handle=%llu path='%s' value=%d offset=%.4f",
			(unsigned long long)ulComponent,
			path.empty() ? "(unmapped)" : path.c_str(),
			(int)bNewValue, fTimeOffset);
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
		std::lock_guard<std::mutex> lk(g_componentMutex);
		auto &info = g_componentInfo[*pHandle];
		info.path = pchName;
		info.is_scalar = true;
		g_firstUpdateLogged[*pHandle] = false;
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

	bool needFirstLog = false;
	std::string path;
	{
		std::lock_guard<std::mutex> lk(g_componentMutex);
		auto it = g_firstUpdateLogged.find(ulComponent);
		if (it != g_firstUpdateLogged.end() && !it->second) {
			it->second = true;
			needFirstLog = true;
			auto infoIt = g_componentInfo.find(ulComponent);
			if (infoIt != g_componentInfo.end()) path = infoIt->second.path;
		}
	}
	if (needFirstLog) {
		LOG("[inputhealth] first UpdateScalarComponent on handle=%llu path='%s' value=%.4f offset=%.4f",
			(unsigned long long)ulComponent,
			path.empty() ? "(unmapped)" : path.c_str(),
			fNewValue, fTimeOffset);
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
		g_componentInfo.clear();
		g_firstUpdateLogged.clear();
	}
	LOG("[inputhealth] Init: subsystem armed (driver=%p), awaiting IVRDriverInput interface queries", (void*)driver);
}

void Shutdown()
{
	// Called after IHook::DestroyAll + InterfaceHooks::DrainInFlightDetours
	// from DisableHooks(). Detours are guaranteed to have exited before this
	// runs, so clearing the maps here is race-free.
	{
		std::lock_guard<std::mutex> lk(g_componentMutex);
		g_componentInfo.clear();
		g_firstUpdateLogged.clear();
	}
	// g_driver intentionally NOT cleared -- ServerTrackedDeviceProvider
	// outlives this DLL across SteamVR driver-reload cycles, and Init()
	// overwrites the slot on next load. Same rationale as the skeletal
	// subsystem: nulling here used to crash vrserver on reload coinciding
	// with an in-flight detour read.
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

	// Same defensive readability + sanity check the skeletal subsystem uses.
	// A real C++ object has its vtable pointer at offset 0; the vtable itself
	// is a contiguous block of function pointers in vrserver.exe's .rdata.
	// If the iface returned by GetGenericInterface points at JsonCpp settings
	// memory or any other non-interface pointer, the dereferenced "vtable"
	// fails the readable check OR produces a wildly large slot-spread.
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
