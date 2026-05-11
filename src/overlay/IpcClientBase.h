#pragma once

#include "Protocol.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <chrono>
#include <cstdint>
#include <string>

namespace openvr_pair::overlay {

class IpcClientBase
{
public:
	// Version-mismatch state after a failed handshake. Matching means the last
	// Connect() succeeded (or hasn't been attempted yet). OverlayNewer /
	// DriverNewer describe which side has the higher protocol version number.
	enum class MismatchState { Matching, OverlayNewer, DriverNewer };

	virtual ~IpcClientBase();

	// Connect to the named pipe. On real I/O errors (pipe unavailable, mode
	// failure) throws std::runtime_error. On a protocol-version mismatch,
	// stores the mismatch state, closes the pipe, and returns without throwing;
	// subsequent Connect() calls are silently no-oped for 30 seconds so the
	// caller's polling loop doesn't hammer the driver with reconnect attempts.
	void Connect(const char *pipeName);
	protocol::Response SendBlocking(const protocol::Request &request);
	bool IsConnected() const { return pipe_ != INVALID_HANDLE_VALUE; }
	void Close();
	uint64_t ConnectionGeneration() const { return connectionGeneration_; }

	MismatchState GetMismatchState() const { return mismatchState_; }
	uint32_t GetDriverVersion() const { return driverVersion_; }
	uint32_t GetExpectedVersion() const { return protocol::Version; }

protected:
	HANDLE pipe_ = INVALID_HANDLE_VALUE;
	std::string pipeName_;
	uint64_t connectionGeneration_ = 0;
	bool reconnecting_ = false;

	MismatchState mismatchState_ = MismatchState::Matching;
	uint32_t driverVersion_ = 0;
	std::chrono::steady_clock::time_point backoffUntil_{};
};

} // namespace openvr_pair::overlay
