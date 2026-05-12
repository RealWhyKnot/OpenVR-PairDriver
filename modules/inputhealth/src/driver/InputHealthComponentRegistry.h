#pragma once

#include <openvr_driver.h>

namespace inputhealth {

void RegisterBooleanComponent(
	vr::VRInputComponentHandle_t handle,
	vr::PropertyContainerHandle_t container,
	const char *path);

void RegisterScalarComponent(
	vr::VRInputComponentHandle_t handle,
	vr::PropertyContainerHandle_t container,
	const char *path);

} // namespace inputhealth
