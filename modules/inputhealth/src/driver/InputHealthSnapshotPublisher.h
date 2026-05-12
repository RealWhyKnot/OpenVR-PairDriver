#pragma once

#include "Protocol.h"

// InputHealth snapshot publisher. Owns the shmem segment named
// OPENVR_PAIRDRIVER_INPUTHEALTH_SHMEM_NAME and a background worker thread
// that wakes at ~10 Hz, snapshots the per-component stats from
// InputHealthHookInjector's g_componentStats map, and writes them into the
// shmem slot table under a per-slot seqlock.
//
// Lifecycle is owned by ServerTrackedDeviceProvider:
//   Init() called once, after the InputHealth subsystem's IPC server is up
//          and before SteamVR's tracked-device pose loop starts.
//   Shutdown() called from DisableHooks / driver teardown so the worker
//          joins cleanly before the DLL unloads.
//
// Thread-safety: Init / Shutdown are not reentrant; call them from the same
// thread that owns ServerTrackedDeviceProvider's lifecycle. The worker
// itself takes InputHealthHookInjector's g_componentMutex briefly per pass
// (during the staged copy-out) and then writes to shmem with no mutex.

namespace inputhealth {

// Start the publisher. Creates the shmem segment, allocates a fixed-size
// staging buffer (one record per slot), spawns the worker thread. Safe to
// call multiple times -- subsequent calls are no-ops if already running.
void SnapshotPublisherInit();

// Stop the publisher. Signals the worker to exit, joins it, closes the
// shmem segment. Safe to call before Init or after Shutdown.
void SnapshotPublisherShutdown();

} // namespace inputhealth
