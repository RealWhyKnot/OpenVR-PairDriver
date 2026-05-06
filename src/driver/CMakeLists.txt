cmake_minimum_required(VERSION 3.15)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(TARGET_NAME 01openvrpair)
set(DRIVER_NAME "driver_openvrpair")

project(OpenVRPairDriver)
message("OpenVR-PairDriver - Driver")

function(GroupSourcesByFolder target)
	set(SOURCE_GROUP_DELIMITER "/")
	set(last_dir "")
	set(files "")
	get_target_property(sources ${target} SOURCES)
	foreach(file ${sources})
		file(RELATIVE_PATH relative_file "${PROJECT_SOURCE_DIR}" ${file})
		get_filename_component(dir "${relative_file}" PATH)
		if(NOT "${dir}" STREQUAL "${last_dir}")
			if(files)
				source_group("${last_dir}" FILES ${files})
			endif()
			set(files "")
		endif()
		set(files ${files} ${file})
		set(last_dir "${dir}")
	endforeach()
	if(files)
		source_group("${last_dir}" FILES ${files})
	endif()
endfunction()

set(OPENVR_LIB_DIR ${CMAKE_SOURCE_DIR}/lib/openvr)

if(NOT PLATFORM)
	if(CMAKE_SIZEOF_VOID_P MATCHES 8)
		set(PLATFORM 64)
	else()
		set(PLATFORM 32)
	endif()
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	add_definitions(-D_WIN32)
	set(ARCH_TARGET win${PLATFORM})
	set(WINDOWS_PATH_SUFFIXES win${PLATFORM} Win${PLATFORM} x${PLATFORM})
endif()

find_library(OPENVR_LIBRARIES
	NAMES openvr_api
	PATHS ${OPENVR_LIB_DIR}/bin ${OPENVR_LIB_DIR}/lib
	PATH_SUFFIXES win${PLATFORM}
	NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)

file(GLOB_RECURSE SOURCES_DRIVER ${CMAKE_SOURCE_DIR}/src/driver "*.c" "*.h" "*.hpp" "*.cpp")
file(GLOB_RECURSE SOURCES_COMMON ${CMAKE_SOURCE_DIR}/src/common "*.h" "*.hpp")

foreach(SOURCE IN ITEMS ${SOURCES_DRIVER})
	get_filename_component(SOURCE_PATH "${SOURCE}" PATH)
	string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}" "" GROUP_PATH "${SOURCE_PATH}")
	string(REPLACE "/" "\\" GROUP_PATH "${GROUP_PATH}")
	source_group("${GROUP_PATH}" FILES "${SOURCE}")
endforeach()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_BINARY_DIR}/${DRIVER_NAME}/bin/${ARCH_TARGET}>)

add_library(OpenVRPairDriver SHARED ${SOURCES_DRIVER} ${SOURCES_COMMON})
GroupSourcesByFolder(OpenVRPairDriver)

target_include_directories(OpenVRPairDriver
	PRIVATE ${CMAKE_SOURCE_DIR}
	PRIVATE ${CMAKE_SOURCE_DIR}/src/common
	PRIVATE ${CMAKE_SOURCE_DIR}/src/driver
	PRIVATE ${OPENVR_LIB_DIR}
	PRIVATE ${CMAKE_SOURCE_DIR}/lib/openvr/headers
	PRIVATE ${CMAKE_SOURCE_DIR}/lib/minhook/include
	PRIVATE ${CMAKE_SOURCE_DIR}/lib)

target_link_libraries(OpenVRPairDriver
	PRIVATE ${OPENVR_LIBRARIES}
	PRIVATE minhook)

target_compile_definitions(OpenVRPairDriver
	PRIVATE NOMINMAX
	PRIVATE UNICODE
	PRIVATE OPENVRSPACECALIBRATORDRIVER_EXPORTS)

set_target_properties(OpenVRPairDriver PROPERTIES OUTPUT_NAME ${DRIVER_NAME})

# Stage the driver folder (manifest + resources) next to the built DLL so
# `<build>/driver_openvrpair/` is a complete deployable tree the consumer
# repos can copy to <SteamVR>\drivers\.
add_custom_command(
	TARGET OpenVRPairDriver
	PRE_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
	${CMAKE_SOURCE_DIR}/${DRIVER_NAME}
	${CMAKE_BINARY_DIR}/${DRIVER_NAME})
