#pragma once

#include "Protocol.h"

#include <cstdint>
#include <memory>

#include <openvr_driver.h>

class ServerTrackedDeviceProvider;

struct DriverModuleContext
{
	ServerTrackedDeviceProvider *provider = nullptr;
	vr::IVRDriverContext *driverContext = nullptr;
	uint32_t featureFlags = 0;
};

class DriverModule
{
public:
	virtual ~DriverModule() = default;

	virtual const char *Name() const = 0;
	virtual uint32_t FeatureMask() const = 0;
	virtual const char *PipeName() const = 0;

	virtual bool Init(DriverModuleContext &context) = 0;
	virtual void Shutdown() {}
	virtual void OnGetGenericInterface(const char *pchInterface, void *iface) {}
	virtual bool HandleRequest(const protocol::Request &request, protocol::Response &response) = 0;
};

namespace calibration {
std::unique_ptr<DriverModule> CreateDriverModule();
}

namespace smoothing {
std::unique_ptr<DriverModule> CreateDriverModule();
}

namespace inputhealth {
std::unique_ptr<DriverModule> CreateDriverModule();
}

namespace facetracking {
std::unique_ptr<DriverModule> CreateDriverModule();
}

namespace oscrouter {
std::unique_ptr<DriverModule> CreateDriverModule();
}

namespace captions {
std::unique_ptr<DriverModule> CreateDriverModule();
}
