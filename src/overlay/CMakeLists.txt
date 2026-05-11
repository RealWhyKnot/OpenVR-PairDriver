cmake_minimum_required(VERSION 3.15)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(OPENVR_PAIRDRIVER_ROOT ${CMAKE_SOURCE_DIR} CACHE PATH "OpenVR-PairDriver checkout" FORCE)
set(OPENVR_PAIR_BUILD_UMBRELLA ON CACHE BOOL "Build feature overlay plugins for OpenVR-Pair" FORCE)

set(OPENVR_PAIR_INPUTHEALTH_DIR "${CMAKE_SOURCE_DIR}/features/OpenVR-InputHealth")
set(OPENVR_PAIR_SMOOTHING_DIR "${CMAKE_SOURCE_DIR}/features/OpenVR-Smoothing")
set(OPENVR_PAIR_CALIBRATION_DIR "${CMAKE_SOURCE_DIR}/features/OpenVR-SpaceCalibrator")
set(OPENVR_PAIR_UI_DEPS_DIR "${OPENVR_PAIR_INPUTHEALTH_DIR}/lib")

if(NOT EXISTS "${OPENVR_PAIR_UI_DEPS_DIR}/imgui/imgui.cpp")
	message(FATAL_ERROR "OpenVR-Pair overlay dependencies not found under ${OPENVR_PAIR_UI_DEPS_DIR}")
endif()

add_library(pair_gl3w STATIC ${OPENVR_PAIR_UI_DEPS_DIR}/gl3w/src/gl3w.c)
target_include_directories(pair_gl3w PUBLIC ${OPENVR_PAIR_UI_DEPS_DIR}/gl3w/include)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${OPENVR_PAIR_UI_DEPS_DIR}/glfw ${CMAKE_BINARY_DIR}/pair-glfw EXCLUDE_FROM_ALL)

add_library(pair_imgui STATIC
	${OPENVR_PAIR_UI_DEPS_DIR}/imgui/imgui.cpp
	${OPENVR_PAIR_UI_DEPS_DIR}/imgui/imgui_demo.cpp
	${OPENVR_PAIR_UI_DEPS_DIR}/imgui/imgui_draw.cpp
	${OPENVR_PAIR_UI_DEPS_DIR}/imgui/imgui_tables.cpp
	${OPENVR_PAIR_UI_DEPS_DIR}/imgui/imgui_widgets.cpp
	${OPENVR_PAIR_UI_DEPS_DIR}/imgui/backends/imgui_impl_glfw.cpp
	${OPENVR_PAIR_UI_DEPS_DIR}/imgui/backends/imgui_impl_opengl3.cpp)
target_include_directories(pair_imgui PUBLIC
	${OPENVR_PAIR_UI_DEPS_DIR}
	${OPENVR_PAIR_UI_DEPS_DIR}/imgui
	${OPENVR_PAIR_UI_DEPS_DIR}/imgui/backends)
target_link_libraries(pair_imgui PUBLIC glfw pair_gl3w)

add_library(pair_implot STATIC
	${OPENVR_PAIR_UI_DEPS_DIR}/implot/implot.cpp
	${OPENVR_PAIR_UI_DEPS_DIR}/implot/implot_items.cpp)
target_include_directories(pair_implot PUBLIC
	${OPENVR_PAIR_UI_DEPS_DIR}/implot
	${OPENVR_PAIR_UI_DEPS_DIR})
target_link_libraries(pair_implot PUBLIC pair_imgui)

set(OPENVR_PAIR_OVERLAY_FEATURE_LIBS "")
set(OPENVR_PAIR_OVERLAY_DEFINES "")

if(EXISTS "${OPENVR_PAIR_INPUTHEALTH_DIR}/src/overlay/CMakeLists.txt")
	add_subdirectory("${OPENVR_PAIR_INPUTHEALTH_DIR}/src/overlay" "${CMAKE_BINARY_DIR}/features/inputhealth-overlay")
	if(TARGET openvr_pair_feature_inputhealth_overlay)
		list(APPEND OPENVR_PAIR_OVERLAY_FEATURE_LIBS openvr_pair_feature_inputhealth_overlay)
		list(APPEND OPENVR_PAIR_OVERLAY_DEFINES OPENVR_PAIR_HAS_INPUTHEALTH_OVERLAY=1)
	endif()
endif()

if(EXISTS "${OPENVR_PAIR_SMOOTHING_DIR}/src/overlay/CMakeLists.txt")
	add_subdirectory("${OPENVR_PAIR_SMOOTHING_DIR}/src/overlay" "${CMAKE_BINARY_DIR}/features/smoothing-overlay")
	if(TARGET openvr_pair_feature_smoothing_overlay)
		list(APPEND OPENVR_PAIR_OVERLAY_FEATURE_LIBS openvr_pair_feature_smoothing_overlay)
		list(APPEND OPENVR_PAIR_OVERLAY_DEFINES OPENVR_PAIR_HAS_SMOOTHING_OVERLAY=1)
	endif()
endif()

if(EXISTS "${OPENVR_PAIR_CALIBRATION_DIR}/src/overlay/CMakeLists.txt")
	add_subdirectory("${OPENVR_PAIR_CALIBRATION_DIR}/src/overlay" "${CMAKE_BINARY_DIR}/features/calibration-overlay")
	if(TARGET openvr_pair_feature_calibration_overlay)
		list(APPEND OPENVR_PAIR_OVERLAY_FEATURE_LIBS openvr_pair_feature_calibration_overlay)
		list(APPEND OPENVR_PAIR_OVERLAY_DEFINES OPENVR_PAIR_HAS_CALIBRATION_OVERLAY=1)
	endif()
endif()

add_executable(OpenVRPairOverlay WIN32
	FeaturePlugin.cpp
	FeaturePlugin.h
	IpcClientBase.cpp
	IpcClientBase.h
	ProfileStoreBase.h
	ShellContext.cpp
	ShellContext.h
	main.cpp)

set_target_properties(OpenVRPairOverlay PROPERTIES OUTPUT_NAME "OpenVR-Pair")

target_include_directories(OpenVRPairOverlay PRIVATE
	${CMAKE_CURRENT_LIST_DIR}
	${CMAKE_SOURCE_DIR}/src/common
	${OPENVR_PAIR_UI_DEPS_DIR}
	${OPENVR_PAIR_UI_DEPS_DIR}/imgui
	${OPENVR_PAIR_UI_DEPS_DIR}/imgui/backends
	${OPENVR_PAIR_UI_DEPS_DIR}/implot
	${OPENVR_PAIR_UI_DEPS_DIR}/gl3w/include)

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
