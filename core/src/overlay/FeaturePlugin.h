#pragma once

namespace openvr_pair::overlay {

struct ShellContext;

class FeaturePlugin
{
public:
	virtual ~FeaturePlugin() = default;

	virtual const char *Name() const = 0;
	virtual const char *FlagFileName() const = 0;
	virtual const char *PipeName() const = 0;
	virtual void OnStart(ShellContext &) {}
	virtual void OnShutdown(ShellContext &) {}
	virtual void Tick(ShellContext &) {}
	virtual void DrawTab(ShellContext &) = 0;

	// Optional: per-plugin contents for the umbrella's global Logs tab.
	// Default no-op so a plugin without log surface area doesn't need to
	// override. The umbrella wraps the call in a collapsing header named
	// after the plugin, so the implementation should just emit its file
	// list / toggle / debug controls without its own heading.
	virtual void DrawLogsSection(ShellContext &) {}

	// Called by the umbrella Logs tab when the shared debug-logging toggle is
	// drawn or changed. Plugins that still own an internal logging flag mirror
	// the shared state here; file-backed loggers can simply consult the common
	// DebugLogging gate and keep the default no-op.
	virtual void OnDebugLoggingChanged(bool) {}

	virtual bool IsInstalled(ShellContext &) const;
};

} // namespace openvr_pair::overlay
