#pragma once

namespace openvr_pair::overlay {

struct ShellContext;

class FeaturePlugin
{
public:
	virtual ~FeaturePlugin() = default;

	virtual const char *Name() const = 0;
	virtual const char *IconGlyph() const { return ""; }
	virtual const char *Subtitle() const { return ""; }
	virtual const char *FlagFileName() const = 0;
	virtual const char *PipeName() const = 0;
	virtual void OnStart(ShellContext &) {}
	virtual void OnShutdown(ShellContext &) {}
	virtual void Tick(ShellContext &) {}
	virtual void DrawTab(ShellContext &) = 0;
	virtual bool IsInstalled(ShellContext &) const;
	virtual bool DriverStatusOk(ShellContext &) const;
	virtual bool IpcStatusOk(ShellContext &) const { return false; }
	virtual bool SharedMemoryStatusOk(ShellContext &) const { return true; }
};

} // namespace openvr_pair::overlay
