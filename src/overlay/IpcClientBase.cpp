#include "IpcClientBase.h"

#include <stdexcept>

namespace openvr_pair::overlay {
namespace {

std::string LastErrorString(DWORD lastError)
{
	LPSTR buffer = nullptr;
	size_t size = FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, lastError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPSTR)&buffer, 0, nullptr);
	std::string message(buffer ? buffer : "", size);
	if (buffer) LocalFree(buffer);
	return message;
}

bool IsBrokenPipeError(DWORD code)
{
	return code == ERROR_BROKEN_PIPE || code == ERROR_PIPE_NOT_CONNECTED || code == ERROR_NO_DATA;
}

} // namespace

IpcClientBase::~IpcClientBase()
{
	Close();
}

void IpcClientBase::Close()
{
	if (pipe_ != INVALID_HANDLE_VALUE) {
		CloseHandle(pipe_);
		pipe_ = INVALID_HANDLE_VALUE;
	}
}

void IpcClientBase::Connect(const char *pipeName)
{
	Close();
	pipeName_ = pipeName ? pipeName : "";
	WaitNamedPipeA(pipeName_.c_str(), 1000);
	pipe_ = CreateFileA(pipeName_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (pipe_ == INVALID_HANDLE_VALUE) {
		DWORD err = GetLastError();
		throw std::runtime_error("IPC pipe unavailable: " + std::to_string(err) + ": " + LastErrorString(err));
	}

	DWORD mode = PIPE_READMODE_MESSAGE;
	if (!SetNamedPipeHandleState(pipe_, &mode, nullptr, nullptr)) {
		DWORD err = GetLastError();
		Close();
		throw std::runtime_error("IPC pipe mode failed: " + std::to_string(err) + ": " + LastErrorString(err));
	}

	auto response = SendBlocking(protocol::Request(protocol::RequestHandshake));
	if (response.type != protocol::ResponseHandshake || response.protocol.version != protocol::Version) {
		Close();
		throw std::runtime_error("Driver protocol version mismatch.");
	}
	++connectionGeneration_;
}

protocol::Response IpcClientBase::SendBlocking(const protocol::Request &request)
{
	if (pipe_ == INVALID_HANDLE_VALUE) {
		if (pipeName_.empty()) throw std::runtime_error("IPC pipe is not connected.");
		Connect(pipeName_.c_str());
	}

	DWORD bytesWritten = 0;
	if (!WriteFile(pipe_, &request, sizeof request, &bytesWritten, nullptr)) {
		DWORD err = GetLastError();
		Close();
		if (IsBrokenPipeError(err) && !reconnecting_ && !pipeName_.empty()) {
			reconnecting_ = true;
			Connect(pipeName_.c_str());
			reconnecting_ = false;
			return SendBlocking(request);
		}
		throw std::runtime_error("IPC write failed: " + std::to_string(err) + ": " + LastErrorString(err));
	}

	protocol::Response response(protocol::ResponseInvalid);
	DWORD bytesRead = 0;
	if (!ReadFile(pipe_, &response, sizeof response, &bytesRead, nullptr) || bytesRead != sizeof response) {
		DWORD err = GetLastError();
		Close();
		throw std::runtime_error("IPC read failed: " + std::to_string(err) + ": " + LastErrorString(err));
	}

	return response;
}

} // namespace openvr_pair::overlay
