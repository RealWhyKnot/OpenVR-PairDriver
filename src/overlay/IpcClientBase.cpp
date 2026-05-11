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
	// If the last Connect() saw a version mismatch, refuse to try again until
	// the back-off period has elapsed. This prevents the overlay's polling loop
	// from hammering the pipe with reconnect attempts that will all fail with
	// the same mismatch. Real I/O errors still throw immediately.
	if (mismatchState_ != MismatchState::Matching) {
		if (std::chrono::steady_clock::now() < backoffUntil_) {
			return; // still in back-off window; caller sees IsConnected()==false
		}
		// Back-off elapsed -- attempt again and update state below.
		mismatchState_ = MismatchState::Matching;
	}

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
		// Store which side is newer so callers can surface a meaningful message,
		// then close the pipe and enter a 30-second back-off. Do NOT throw --
		// the caller's reconnect loop should see a clean "not connected" outcome
		// and display the mismatch state rather than logging a crash.
		driverVersion_ = response.protocol.version;
		mismatchState_ = (response.protocol.version < protocol::Version)
			? MismatchState::OverlayNewer
			: MismatchState::DriverNewer;
		backoffUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(30);
		Close();
		return;
	}
	mismatchState_ = MismatchState::Matching;
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
	if (!ReadFile(pipe_, &response, sizeof response, &bytesRead, nullptr)) {
		DWORD err = GetLastError();
		Close();
		throw std::runtime_error("IPC read failed: " + std::to_string(err) + ": " + LastErrorString(err));
	}
	if (bytesRead != sizeof response) {
		Close();
		throw std::runtime_error(
			"IPC read truncated: got " + std::to_string(bytesRead) +
			" of " + std::to_string(sizeof response) + " bytes");
	}

	return response;
}

} // namespace openvr_pair::overlay
