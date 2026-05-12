cmake_minimum_required(VERSION 3.15)

set(SMOOTHING_PAIRDRIVER_ROOT "${OPENVR_PAIRDRIVER_ROOT}" CACHE PATH "OpenVR-WKPairDriver checkout")
get_filename_component(SMOOTHING_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(NOT SMOOTHING_PAIRDRIVER_ROOT)
	set(_sibling_pairdriver "${CMAKE_CURRENT_LIST_DIR}/../../../OpenVR-WKPairDriver")
	if(EXISTS "${_sibling_pairdriver}/src/overlay/FeaturePlugin.h")
		get_filename_component(SMOOTHING_PAIRDRIVER_ROOT "${_sibling_pairdriver}" ABSOLUTE)
	else()
		get_filename_component(SMOOTHING_PAIRDRIVER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../lib/OpenVR-WKPairDriver" ABSOLUTE)
	endif()
endif()

add_library(openvr_pair_feature_smoothing_overlay STATIC
	Config.cpp
	Config.h
	IPCClient.cpp
	IPCClient.h
	SmoothingFingers.cpp
	SmoothingPlugin.cpp
	SmoothingPlugin.h
	SmoothingPrediction.cpp)

target_include_directories(openvr_pair_feature_smoothing_overlay PUBLIC
	${CMAKE_CURRENT_LIST_DIR}
	${SMOOTHING_PAIRDRIVER_ROOT}/src/common
	${SMOOTHING_PAIRDRIVER_ROOT}/src/overlay
	${SMOOTHING_REPO_ROOT}/lib/imgui
	${SMOOTHING_REPO_ROOT}/lib/openvr/headers)

target_compile_definitions(openvr_pair_feature_smoothing_overlay PRIVATE
	NOMINMAX
	UNICODE
	_UNICODE)
