cmake_minimum_required(VERSION 3.15)

get_filename_component(INPUTHEALTH_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(INPUTHEALTH_PAIRDRIVER_ROOT "${OPENVR_PAIRDRIVER_ROOT}" CACHE PATH "OpenVR-WKPairDriver checkout")
if(NOT INPUTHEALTH_PAIRDRIVER_ROOT)
	set(_sibling_pairdriver "${INPUTHEALTH_REPO_ROOT}/../OpenVR-WKPairDriver")
	if(EXISTS "${_sibling_pairdriver}/src/overlay/FeaturePlugin.h")
		get_filename_component(INPUTHEALTH_PAIRDRIVER_ROOT "${_sibling_pairdriver}" ABSOLUTE)
	else()
		get_filename_component(INPUTHEALTH_PAIRDRIVER_ROOT "${INPUTHEALTH_REPO_ROOT}/lib/OpenVR-WKPairDriver" ABSOLUTE)
	endif()
endif()

add_library(openvr_pair_feature_inputhealth_overlay STATIC
	DebugTab.cpp
	DebugTab.h
	DiagnosticsTab.cpp
	DiagnosticsTab.h
	IPCClient.cpp
	IPCClient.h
	InputHealthPlugin.cpp
	InputHealthPlugin.h
	LearningEngine.cpp
	LearningEngine.h
	Logging.cpp
	Logging.h
	Profiles.cpp
	Profiles.h
	SettingsTab.cpp
	SettingsTab.h
	SnapshotReader.cpp
	SnapshotReader.h
	BuildStamp.h)

target_include_directories(openvr_pair_feature_inputhealth_overlay PUBLIC
	${CMAKE_CURRENT_LIST_DIR}
	${INPUTHEALTH_PAIRDRIVER_ROOT}/src/common
	${INPUTHEALTH_PAIRDRIVER_ROOT}/src/common/inputhealth
	${INPUTHEALTH_PAIRDRIVER_ROOT}/src/overlay
	${INPUTHEALTH_REPO_ROOT}/lib
	${INPUTHEALTH_REPO_ROOT}/lib/imgui
	${INPUTHEALTH_REPO_ROOT}/lib/implot
	${INPUTHEALTH_REPO_ROOT}/lib/openvr/headers)

target_compile_definitions(openvr_pair_feature_inputhealth_overlay PRIVATE
	NOMINMAX
	UNICODE
	_UNICODE
	"OPENVR_INPUTHEALTH_VERSION=\"feature\"")

if(MSVC)
	target_compile_options(openvr_pair_feature_inputhealth_overlay PRIVATE
		/W4
		/wd4100
		/wd4505
		/we4715
		/we4828
		/we4473 /we4474 /we4475 /we4476 /we4477 /we4478)
endif()
