#pragma once

#include "Protocol.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <string>

namespace openvr_pair::overlay {

class IpcClientBase
{
public:
	virtual ~IpcClientBase();

	void Connect(const char *pipeName);
	protocol::Response SendBlocking(const protocol::Request &request);
	bool IsConnected() const { return pipe_ != INVALID_HANDLE_VALUE; }
	void Close();
	uint64_t ConnectionGeneration() const { return connectionGeneration_; }

protected:
	HANDLE pipe_ = INVALID_HANDLE_VALUE;
	std::string pipeName_;
	uint64_t connectionGeneration_ = 0;
	bool reconnecting_ = false;
};

} // namespace openvr_pair::overlay
