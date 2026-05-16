#define _CRT_SECURE_NO_DEPRECATE
#include "HostSupervisor.h"
#include "Logging.h"

#include <cstdarg>

#define TR_HOST_CONTROL_PIPE_NAME  "\\\\.\\pipe\\WKOpenVR-Translator.host"

namespace translator {

HostSupervisor::HostSupervisor(const std::string &host_exe_path)
    : HostSupervisorBase(host_exe_path)
{}

std::string HostSupervisor::ControlPipeName() const
{
    return TR_HOST_CONTROL_PIPE_NAME;
}

void HostSupervisor::LogV(const char* fmt, va_list args)
{
    TrDrvLogV(fmt, args);
}

std::string HostSupervisor::DescribeExitCode(DWORD code) const
{
    // Windows loader failures.
    if (code == 0xC0000135)
        return "STATUS_DLL_NOT_FOUND -- a hard-import DLL is missing; "
               "check openvr_api.dll and VC runtime staging beside "
               "WKOpenVR.TranslatorHost.exe";
    if (code == 0xC0000005)
        return "STATUS_ACCESS_VIOLATION -- crash inside native lib; "
               "see translator_host_crash_<pid>.txt in "
               "%LocalAppDataLow%\\WKOpenVR\\Logs";
    if (code == 0xC000007B)
        return "STATUS_INVALID_IMAGE_FORMAT -- 32/64-bit mismatch";
    if (code == 0xC0000142)
        return "STATUS_DLL_INIT_FAILED -- DLL DllMain returned FALSE; a "
               "required dependency of a loaded DLL is itself missing";

    // Our delay-load failure range: 0xCEE0DC00 | (err & 0xFF).
    if ((code & 0xFFFFFF00u) == 0xCEE0DC00u) {
        DWORD winerr = code & 0xFF;
        if (winerr == 126) {
            return "delay-load failed: ERROR_MOD_NOT_FOUND (126) -- "
                   "optional translator runtime DLL missing; install the "
                   "relevant Translator pack; see translator_host_crash_<pid>.txt "
                   "for the exact DLL name";
        }
        return "delay-load failed -- optional translator runtime DLL missing; "
               "see translator_host_crash_<pid>.txt for details";
    }

    // Clean exits from the host's singleton check.
    if (code == 3) return "clean singleton exit (another host already running)";
    if (code == 4) return "clean pipe-busy exit (another host owns the control pipe)";

    if (code == 0) return "normal exit (code 0)";

    return "unknown";
}

void HostSupervisor::SetHostConfigCommand(const std::string &command)
{
    {
        std::lock_guard<std::mutex> lk(command_mutex_);
        pending_command_     = command;
        has_pending_command_ = !command.empty();
        command_sent_        = false;
    }
    if (!command.empty()) TrySendCommand(command);
}

void HostSupervisor::OnHostReady()
{
    std::string command;
    bool should_send = false;
    {
        std::lock_guard<std::mutex> lk(command_mutex_);
        if (has_pending_command_ && !command_sent_) {
            command     = pending_command_;
            should_send = true;
        }
    }
    if (should_send) TrySendCommand(command);
}

void HostSupervisor::OnHostExited()
{
    std::lock_guard<std::mutex> lk(command_mutex_);
    if (has_pending_command_) command_sent_ = false;
}

bool HostSupervisor::TrySendCommand(const std::string &command)
{
    if (!SendBytesOverControlPipe(command.data(), command.size())) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(command_mutex_);
        if (pending_command_ == command) command_sent_ = true;
    }
    Log("[host] sent translator host config (%lu bytes)",
        static_cast<unsigned long>(command.size()));
    return true;
}

} // namespace translator
