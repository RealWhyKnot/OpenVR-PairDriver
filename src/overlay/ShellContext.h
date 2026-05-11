#pragma once

#include "Fonts.h"

#include <string>
#include <vector>

namespace openvr_pair::overlay {

struct ShellContext
{
	std::wstring installDir;
	std::wstring profileRoot;
	std::wstring logRoot;
	std::vector<std::wstring> driverResourceDirs;
	std::string status;
	FontSet fontSet;

	std::wstring FlagPath(const char *flagFileName) const;
	bool IsFlagPresent(const char *flagFileName) const;
	bool SetFlagPresent(const char *flagFileName, bool present);
	void SetStatus(std::string message);
	const FontSet &fonts() const { return fontSet; }
	FontSet &fonts() { return fontSet; }
};

ShellContext CreateShellContext();
std::string Narrow(const std::wstring &value);

} // namespace openvr_pair::overlay
