#include "DriverModule.h"
#include "FeatureFlags.h"
#include "Logging.h"
#include "ServerTrackedDeviceProvider.h"
#include "SkeletalHookInjector.h"

#include <string>

namespace smoothing {
namespace {

class SmoothingDriverModule final : public DriverModule
{
public:
	const char *Name() const override { return "Smoothing"; }
	uint32_t FeatureMask() const override { return pairdriver::kFeatureSmoothing; }
	const char *PipeName() const override { return OPENVR_PAIRDRIVER_SMOOTHING_PIPE_NAME; }

	bool Init(DriverModuleContext &context) override
	{
		provider_ = context.provider;
		skeletal::Init(provider_);
		return true;
	}

	void Shutdown() override
	{
		skeletal::Shutdown();
		provider_ = nullptr;
	}

	void OnGetGenericInterface(const char *pchInterface, void *iface) override
	{
		if (!pchInterface || !iface) return;
		std::string name(pchInterface);
		if (name.find("IVRDriverInput_") != std::string::npos &&
			name.find("Internal") == std::string::npos) {
			LOG("[skeletal] %s queried via context: iface=%p", name.c_str(), iface);
			skeletal::TryInstallPublicHooks(iface);
		}
	}

	bool HandleRequest(const protocol::Request &request, protocol::Response &response) override
	{
		if (!provider_) return false;
		switch (request.type) {
		case protocol::RequestSetFingerSmoothing:
			provider_->SetFingerSmoothingConfig(request.setFingerSmoothing);
			response.type = protocol::ResponseSuccess;
			return true;
		case protocol::RequestSetDevicePrediction:
			provider_->SetDevicePrediction(request.setDevicePrediction);
			response.type = protocol::ResponseSuccess;
			return true;
		default:
			return false;
		}
	}

private:
	ServerTrackedDeviceProvider *provider_ = nullptr;
};

} // namespace

std::unique_ptr<DriverModule> CreateDriverModule()
{
	return std::make_unique<SmoothingDriverModule>();
}

} // namespace smoothing
