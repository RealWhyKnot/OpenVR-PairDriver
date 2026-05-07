#pragma once

#include <openvr_driver.h>

#include <vector>

#include "Protocol.h"

class ServerTrackedDeviceProvider;

// Public entry points for the InputHealth subsystem.
//
// Architecture target (lands in stages):
//
// Lighthouse and other vendor drivers publish per-component input updates by
// calling vr::VRDriverInput()->UpdateBooleanComponent (digital buttons) and
// vr::VRDriverInput()->UpdateScalarComponent (analog axes / triggers / finger
// curls). vr::VRDriverInput() resolves to a pointer obtained through
// IVRDriverContext::GetGenericInterface("IVRDriverInput_003" or "_004") -- the
// same vtable the skeletal subsystem already patches at slots 5 and 6.
//
// Stage 1B will extend the GetGenericInterface detour to install InputHealth
// hooks on the boolean and scalar slots of the same vtable. Each detour
// records the raw value into a per-component ring buffer and updates O(1)
// rolling statistics (Welford accumulator, Page-Hinkley accumulator,
// EWMA-of-min, polar-bin max for paired axes) before forwarding the call.
// Heavy detection work (Weiszfeld geometric median, SPRT category decisions,
// hull rebuild, ellipse fit) runs on a background worker thread that wakes
// at ~10 Hz; the detour thread never blocks on it.
//
// Stage 1A (this commit) adds the subsystem skeleton: cached driver pointer,
// Init/Shutdown lifecycle, a placeholder TryInstall entry point logged when
// the IVRDriverInput vtable is queried. No actual hooks are installed yet --
// that step is gated behind real-world validation of the slot indices.
//
// Default-OFF semantics: with master_enabled=false in InputHealthConfig the
// future detour bodies will be one-line counter-bump + originalFunc forward
// (the same shape SkeletalHookInjector uses), so installing has zero
// behaviour change for users who do not opt in.

namespace inputhealth {

// Cache the driver pointer for InputHealth config access from inside future
// detour bodies. Called once from InjectHooks() in InterfaceHookInjector.cpp
// during driver Init when kFeatureInputHealth is set.
void Init(ServerTrackedDeviceProvider *driver);

// Tear down the subsystem. Called from DisableHooks(). The IHook registry
// drops any installed hooks on its own; this hook resets cached driver-
// pointer state so a driver-reload cycle starts clean.
void Shutdown();

// Called from DetourGetGenericInterface when an "IVRDriverInput_*" interface
// (substring; "Internal" excluded) is queried. Stage 1A logs the call so the
// Stage 1B implementer can confirm the same vtable is being seen the
// skeletal subsystem already taps. Stage 1B will replace the body with the
// MinHook installs on slots for UpdateBooleanComponent / UpdateScalarComponent
// (slot indices to be confirmed against headers/openvr_driver.h at the time;
// the public IVRDriverInput_003 layout has them at slots 1 and 3 but that is
// not load-bearing on this commit).
void TryInstallScalarBooleanHooks(void *iface);

// Apply an InputHealthResetStats request to the per-component map. Resolves
// each entry's owning device serial via vr::VRProperties() lazily (cached
// per-handle for the lifetime of the entry) and clears the stats categories
// the request asked for on every entry whose serial hash matches. A request
// with device_serial_hash == kSerialHashAllDevices targets every entry.
//
// Runs on the IPC dispatcher thread. Safe to call concurrently with the
// per-tick detour bodies; both paths take g_componentMutex.
void ApplyResetRequest(const protocol::InputHealthResetStats &req);

// Per-snapshot record + handle pair emitted to the snapshot publisher.
struct StagedSnapshot
{
	uint64_t                       handle;
	protocol::InputHealthSnapshotBody body;
};

// Build wire-format snapshot bodies for every component currently registered
// in g_componentStats and append them to `out`. Holds g_componentMutex for
// the duration. The publisher calls this once per ~10 Hz tick and then
// writes the staged bodies to shmem under the per-slot seqlock without
// holding the mutex.
void StageSnapshots(std::vector<StagedSnapshot> &out);

} // namespace inputhealth
