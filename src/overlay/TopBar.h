#pragma once

namespace openvr_pair::overlay {

struct ShellContext;

void DrawTopBar(ShellContext &context,
	const char *title,
	const char *subtitle,
	bool driver_ok,
	bool ipc_ok,
	bool shmem_ok);

} // namespace openvr_pair::overlay
