#include "IPCClient.h"

#include <cstdio>
#include <stdexcept>
#include <string>

namespace {

class BrokenPipeException : public std::runtime_error
{
public:
	BrokenPipeException(const std::string& msg, DWORD code)
		: std::runtime_error(msg), errorCode(code) {}
	DWORD errorCode;
};

bool IsBrokenPipeError(DWORD code)
{
	return code == ERROR_BROKEN_PIPE
		|| code == ERROR_PIPE_NOT_CONNECTED
		|| code == ERROR_NO_DATA;
}

std::string LastErrorString(DWORD lastError)
{
	LPWSTR buffer = nullptr;
	size_t size = FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, lastError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&buffer, 0, nullptr);
	if (!buffer) return {};
	int needed = WideCharToMultiByte(CP_UTF8, 0, buffer, (int)size, nullptr, 0, nullptr, nullptr);
	std::string out(needed, '\0');
	if (needed > 0) {
		WideCharToMultiByte(CP_UTF8, 0, buffer, (int)size, out.data(), needed, nullptr, nullptr);
	}
	LocalFree(buffer);
	return out;
}

} // namespace

IPCClient::~IPCClient()
{
	Close();
}

void IPCClient::Close()
{
	if (pipe && pipe != INVALID_HANDLE_VALUE) {
		CloseHandle(pipe);
		pipe = INVALID_HANDLE_VALUE;
	}
}

void IPCClient::Connect()
{
	Close();
	LPCSTR pipeName = OPENVR_PAIRDRIVER_INPUTHEALTH_PIPE_NAME;
	WaitNamedPipeA(pipeName, 1000);
	pipe = CreateFileA(pipeName, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);

	if (pipe == INVALID_HANDLE_VALUE) {
		throw std::runtime_error(
			"InputHealth driver unavailable. SteamVR is not running, the WKOpenVR shared driver is not installed, or the InputHealth feature is not enabled (enable_inputhealth.flag missing in the driver's resources folder).");
	}

	DWORD mode = PIPE_READMODE_MESSAGE;
	if (!SetNamedPipeHandleState(pipe, &mode, 0, 0)) {
		DWORD err = GetLastError();
		Close();
		throw std::runtime_error("Could not set pipe mode. Error " + std::to_string(err) + ": " + LastErrorString(err));
	}

	auto response = SendBlocking(protocol::Request(protocol::RequestHandshake));
	if (response.type != protocol::ResponseHandshake || response.protocol.version != protocol::Version) {
		Close();
		throw std::runtime_error(
			"Driver protocol version mismatch. Reinstall InputHealth so the overlay and shared driver are paired. (Overlay: " +
			std::to_string(protocol::Version) + ", driver: " + std::to_string(response.protocol.version) + ")");
	}
	++connection_generation;
}

protocol::Response IPCClient::SendBlocking(const protocol::Request &request)
{
	try {
		Send(request);
		return Receive();
	} catch (const BrokenPipeException &e) {
		if (inReconnect) throw;

		// Pipe broke mid-request (typically vrserver restarted). Try one
		// transparent reconnect before propagating.
		fprintf(stderr, "[InputHealth IPC] broken pipe (err=%lu); attempting reconnect\n", (unsigned long)e.errorCode);

		Close();

		inReconnect = true;
		try {
			Connect();
		} catch (const std::exception &reconnectErr) {
			inReconnect = false;
			throw std::runtime_error(std::string("InputHealth IPC reconnect failed: ") + reconnectErr.what());
		}
		inReconnect = false;

		Send(request);
		return Receive();
	}
}

protocol::Response IPCClient::SendCompensationEntry(const protocol::InputHealthCompensationEntry &entry)
{
	protocol::Request req(protocol::RequestSetInputHealthCompensation);
	req.setInputHealthCompensation = entry;
	return SendBlocking(req);
}

void IPCClient::Send(const protocol::Request &request)
{
	DWORD bytesWritten;
	BOOL ok = WriteFile(pipe, &request, sizeof(request), &bytesWritten, 0);
	if (!ok) {
		DWORD err = GetLastError();
		std::string msg = "InputHealth IPC write error " + std::to_string(err) + ": " + LastErrorString(err);
		if (IsBrokenPipeError(err)) throw BrokenPipeException(msg, err);
		throw std::runtime_error(msg);
	}
}

protocol::Response IPCClient::Receive()
{
	protocol::Response response(protocol::ResponseInvalid);
	DWORD bytesRead;

	BOOL ok = ReadFile(pipe, &response, sizeof(response), &bytesRead, 0);
	if (!ok) {
		DWORD err = GetLastError();
		if (err != ERROR_MORE_DATA) {
			std::string msg = "InputHealth IPC read error " + std::to_string(err) + ": " + LastErrorString(err);
			if (IsBrokenPipeError(err)) throw BrokenPipeException(msg, err);
			throw std::runtime_error(msg);
		}
		// Drain the rest of an oversized message so the next Receive() starts
		// on a clean message boundary.
		char drain[1024];
		while (true) {
			DWORD drained = 0;
			BOOL drainOk = ReadFile(pipe, drain, sizeof(drain), &drained, 0);
			if (drainOk) break;
			DWORD drainErr = GetLastError();
			if (drainErr == ERROR_MORE_DATA) continue;
			if (IsBrokenPipeError(drainErr)) {
				throw BrokenPipeException("Pipe broken while draining oversized response", drainErr);
			}
			break;
		}
		throw std::runtime_error("Invalid IPC response: message exceeds expected size " + std::to_string(sizeof(response)));
	}

	if (bytesRead != sizeof(response)) {
		throw std::runtime_error("Invalid IPC response: got " + std::to_string(bytesRead) + " bytes, expected " + std::to_string(sizeof(response)));
	}

	return response;
}
