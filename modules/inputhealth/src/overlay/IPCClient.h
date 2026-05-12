#pragma once

#include "Protocol.h"

// IPC client for the InputHealth overlay. Talks to the OpenVR-WKPairDriver
// shared driver over `\\.\pipe\OpenVR-WKInputHealth` (the third feature pipe
// alongside calibration and smoothing). Same Request/Response protocol on
// the wire; this overlay only sends RequestHandshake,
// RequestSetInputHealthConfig, RequestSetInputHealthCompensation, and
// RequestResetInputHealthStats.

class IPCClient
{
public:
	~IPCClient();

	// Open the pipe, switch to message mode, send a handshake, and verify
	// the protocol version matches. Throws std::runtime_error on any
	// failure with a human-readable message suitable for surfacing in the
	// UI's status bar.
	void Connect();

	// Send a request and read the response in one call. Transparently
	// reconnects (once) if the pipe is broken between SteamVR restarts.
	protocol::Response SendBlocking(const protocol::Request &request);
	protocol::Response SendCompensationEntry(const protocol::InputHealthCompensationEntry &entry);

	void Send(const protocol::Request &request);
	protocol::Response Receive();

	bool IsConnected() const { return pipe != INVALID_HANDLE_VALUE; }
	void Close();
	uint64_t ConnectionGeneration() const { return connection_generation; }

private:
	HANDLE pipe        = INVALID_HANDLE_VALUE;
	bool   inReconnect = false;
	uint64_t connection_generation = 0;
};
