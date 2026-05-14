#include "TranslatorIpcClient.h"
#include "Protocol.h"

#include <string>

namespace {

openvr_pair::overlay::IpcClientConnectOptions Options()
{
    openvr_pair::overlay::IpcClientConnectOptions options;
    options.pipeUnavailable = [](DWORD, const std::string &) {
        return "Translator driver unavailable. SteamVR is not running, "
            "the WKOpenVR shared driver is not installed, or "
            "the Translator feature is not enabled "
            "(enable_translator.flag missing from the driver's resources folder).";
    };
    options.pipeModeFailed = [](DWORD error, const std::string &details) {
        return "Could not set Translator pipe mode. Error "
            + std::to_string(error) + ": " + details;
    };
    options.versionMismatch = [](uint32_t expected, uint32_t driver) {
        return "Translator driver protocol version mismatch. "
            "Reinstall WKOpenVR so the overlay and driver are paired. "
            "(Overlay: " + std::to_string(expected) +
            ", driver: " + std::to_string(driver) + ")";
    };
    options.reconnectFailurePrefix   = "Translator IPC reconnect failed: ";
    options.writeFailurePrefix       = "Translator IPC write error";
    options.readFailurePrefix        = "Translator IPC read error";
    options.oversizedResponseMessage = "Invalid Translator IPC response: message exceeds expected size ";
    options.sizeMismatchMessagePrefix= "Invalid Translator IPC response";
    return options;
}

} // namespace

void TranslatorIpcClient::Connect()
{
    openvr_pair::overlay::IpcClientBase::Connect(
        OPENVR_PAIRDRIVER_TRANSLATOR_PIPE_NAME,
        Options());
}
