#include "InputHealthHookInjector.h"

#include "Logging.h"
#include "ServerTrackedDeviceProvider.h"

#include <atomic>

namespace inputhealth {

namespace {
// Cached driver pointer captured in Init(). Read by future detour bodies and
// by the background worker thread (Stage 1C+). Atomic so a Shutdown() racing
// against a late-arriving GetGenericInterface query (driver-reload edge case)
// produces a clean nullptr fast-path instead of a use-after-clear crash.
std::atomic<ServerTrackedDeviceProvider *> g_driver{nullptr};
std::atomic<bool>                          g_logged_first_iface{false};
} // namespace

void Init(ServerTrackedDeviceProvider *driver)
{
	g_driver.store(driver, std::memory_order_release);
	g_logged_first_iface.store(false, std::memory_order_release);
	LOG("[inputhealth] subsystem online (Stage 1A scaffold; hooks land in Stage 1B)");
}

void Shutdown()
{
	g_driver.store(nullptr, std::memory_order_release);
	LOG("[inputhealth] subsystem offline");
}

void TryInstallScalarBooleanHooks(void *iface)
{
	if (g_driver.load(std::memory_order_acquire) == nullptr) return;
	if (iface == nullptr) return;

	// One-shot log so the next-stage implementer can confirm the same iface
	// pointer the skeletal subsystem sees is also reaching the InputHealth
	// branch. Subsequent queries (driver-reload, late vtable handoff) do not
	// re-log; we only need the first to validate the routing.
	bool expected = false;
	if (g_logged_first_iface.compare_exchange_strong(expected, true,
			std::memory_order_acq_rel)) {
		LOG("[inputhealth] IVRDriverInput vtable visible (iface=%p); Stage 1B will patch boolean + scalar slots here",
			iface);
	}
}

} // namespace inputhealth
