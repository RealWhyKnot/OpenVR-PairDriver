cmake_minimum_required(VERSION 3.15)

add_library(openvr_pair_common STATIC
	DriverMemoryProbe.cpp
	DriverMemoryProbe.h
	JsonUtil.cpp
	JsonUtil.h
	LogPaths.cpp
	LogPaths.h
	Win32Errors.cpp
	Win32Errors.h
	Win32Paths.cpp
	Win32Paths.h
	Win32Text.cpp
	Win32Text.h)

set_target_properties(openvr_pair_common PROPERTIES
	CXX_STANDARD 20
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF)

target_include_directories(openvr_pair_common
	PUBLIC
		${CMAKE_CURRENT_LIST_DIR}
		${CMAKE_SOURCE_DIR}/lib)

target_compile_definitions(openvr_pair_common
	PUBLIC
		NOMINMAX
		WIN32_LEAN_AND_MEAN)

if(WIN32)
	target_link_libraries(openvr_pair_common
		PUBLIC
			shell32
			ole32)
endif()
