cmake_minimum_required(VERSION 3.15)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(OPENVR_PAIRDRIVER_ROOT ${CMAKE_SOURCE_DIR} CACHE PATH "OpenVR-WKPairDriver checkout" FORCE)
set(OPENVR_PAIR_BUILD_UMBRELLA ON CACHE BOOL "Build feature overlay plugins for OpenVR-Pair" FORCE)

# Feature modules are always present in the monorepo; add unconditionally.
set(OPENVR_PAIR_OVERLAY_FEATURE_LIBS "")
set(OPENVR_PAIR_OVERLAY_DEFINES "")

add_subdirectory("${CMAKE_SOURCE_DIR}/modules/inputhealth/src/overlay" "${CMAKE_BINARY_DIR}/modules/inputhealth-overlay")
list(APPEND OPENVR_PAIR_OVERLAY_FEATURE_LIBS openvr_pair_feature_inputhealth_overlay)
list(APPEND OPENVR_PAIR_OVERLAY_DEFINES OPENVR_PAIR_HAS_INPUTHEALTH_OVERLAY=1)

add_subdirectory("${CMAKE_SOURCE_DIR}/modules/smoothing/src/overlay" "${CMAKE_BINARY_DIR}/modules/smoothing-overlay")
list(APPEND OPENVR_PAIR_OVERLAY_FEATURE_LIBS openvr_pair_feature_smoothing_overlay)
list(APPEND OPENVR_PAIR_OVERLAY_DEFINES OPENVR_PAIR_HAS_SMOOTHING_OVERLAY=1)

add_subdirectory("${CMAKE_SOURCE_DIR}/modules/calibration/src/overlay" "${CMAKE_BINARY_DIR}/modules/calibration-overlay")
list(APPEND OPENVR_PAIR_OVERLAY_FEATURE_LIBS openvr_pair_feature_calibration_overlay)
list(APPEND OPENVR_PAIR_OVERLAY_DEFINES OPENVR_PAIR_HAS_CALIBRATION_OVERLAY=1)

add_executable(OpenVRPairOverlay WIN32
	FeaturePlugin.cpp
	FeaturePlugin.h
	IpcClientBase.cpp
	IpcClientBase.h
	ProfileStoreBase.h
	ShellContext.cpp
	ShellContext.h
	ShellFooter.cpp
	ShellFooter.h
	main.cpp)

set_target_properties(OpenVRPairOverlay PROPERTIES OUTPUT_NAME "OpenVR-Pair")

target_include_directories(OpenVRPairOverlay PRIVATE
	${CMAKE_CURRENT_LIST_DIR}
	${CMAKE_SOURCE_DIR}/core/src/common
	${CMAKE_SOURCE_DIR}/lib
	${CMAKE_SOURCE_DIR}/lib/imgui
	${CMAKE_SOURCE_DIR}/lib/imgui/backends
	${CMAKE_SOURCE_DIR}/lib/implot
	${CMAKE_SOURCE_DIR}/lib/gl3w/include)

target_link_libraries(OpenVRPairOverlay PRIVATE
	pair_imgui
	pair_implot
	pair_gl3w
	glfw
	opengl32
	shell32
	ole32
	advapi32
	${OPENVR_PAIR_OVERLAY_FEATURE_LIBS})

file(READ "${CMAKE_SOURCE_DIR}/version.txt" OPENVR_PAIR_VERSION_RAW)
string(STRIP "${OPENVR_PAIR_VERSION_RAW}" OPENVR_PAIR_VERSION_STAMP)

target_compile_definitions(OpenVRPairOverlay PRIVATE
	NOMINMAX
	UNICODE
	_UNICODE
	${OPENVR_PAIR_OVERLAY_DEFINES}
	"OPENVR_PAIR_VERSION_STRING=\"${OPENVR_PAIR_VERSION_STAMP}\"")

if(MSVC)
	target_link_options(OpenVRPairOverlay PRIVATE /ENTRY:mainCRTStartup)
endif()
