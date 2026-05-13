cmake_minimum_required(VERSION 3.15)

# C# face-tracking host sidecar. Built via `dotnet publish` and staged into the
# driver's resources/facetracking/host/ tree so the driver's HostSupervisor can
# spawn it at SteamVR startup. The .NET SDK version is pinned by
# modules/facetracking/src/host/global.json (10.x); CMake locates the dotnet
# binary on the build host's PATH.

option(OPENVR_PAIR_BUILD_FACE_HOST
	"Build the C# face-tracking module host (.NET 10). Off lets a C++-only contributor build the driver+overlay without the .NET SDK."
	ON)

if(NOT OPENVR_PAIR_BUILD_FACE_HOST)
	message(STATUS "OPENVR_PAIR_BUILD_FACE_HOST=OFF: skipping FaceModuleHost build. The driver will start without the host process and the FaceTracking feature will run inert until the host is staged manually.")
	return()
endif()

find_program(DOTNET_EXECUTABLE
	NAMES dotnet dotnet.exe
	HINTS
		"$ENV{ProgramFiles}/dotnet"
		"$ENV{ProgramFiles\(x86\)}/dotnet"
	DOC "Path to the .NET CLI (dotnet) used to build the FaceModuleHost.")

if(NOT DOTNET_EXECUTABLE)
	message(WARNING "dotnet CLI not found on PATH. The FaceModuleHost will not build. Install .NET 10 SDK or set -DOPENVR_PAIR_BUILD_FACE_HOST=OFF.")
	return()
endif()

set(FACE_HOST_SOLUTION "${CMAKE_CURRENT_LIST_DIR}/OpenVRPair.FaceTracking.sln")
set(FACE_HOST_PROJECT  "${CMAKE_CURRENT_LIST_DIR}/OpenVRPair.FaceModuleHost/OpenVRPair.FaceModuleHost.csproj")
set(FACE_HOST_PUBLISH_DIR "${CMAKE_BINARY_DIR}/facetracking-host-publish")
set(FACE_HOST_ARTIFACT_DIR "${CMAKE_BINARY_DIR}/artifacts/FaceModuleHost")
# Mirror the published tree into the driver staging directory so the
# NSIS installer + the umbrella zip both find OpenVRPair.FaceModuleHost.exe
# under driver_wkopenvr/resources/facetracking/host/ without any caller-
# side post-build copy. build.ps1 used to handle this, but the CI workflow
# runs cmake directly and skipped the step -- doing it here keeps every
# build path (local + CI + future ones) producing the same tree.
set(FACE_HOST_DRIVER_STAGE_DIR "${CMAKE_BINARY_DIR}/driver_wkopenvr/resources/facetracking/host")
set(FACE_HOST_STAMP "${CMAKE_CURRENT_BINARY_DIR}/face_host.stamp")

file(GLOB_RECURSE FACE_HOST_INPUTS
	"${CMAKE_CURRENT_LIST_DIR}/*.csproj"
	"${CMAKE_CURRENT_LIST_DIR}/*.cs"
	"${CMAKE_CURRENT_LIST_DIR}/*.props"
	"${CMAKE_CURRENT_LIST_DIR}/*.json")

add_custom_command(
	OUTPUT "${FACE_HOST_STAMP}"
	COMMAND ${DOTNET_EXECUTABLE} publish "${FACE_HOST_PROJECT}"
		-c Release
		-r win-x64
		--self-contained false
		-p:PublishSingleFile=false
		-p:UseAppHost=true
		-o "${FACE_HOST_PUBLISH_DIR}"
		--nologo
		--verbosity minimal
	COMMAND ${CMAKE_COMMAND} -E make_directory "${FACE_HOST_ARTIFACT_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${FACE_HOST_PUBLISH_DIR}" "${FACE_HOST_ARTIFACT_DIR}"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${FACE_HOST_DRIVER_STAGE_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${FACE_HOST_PUBLISH_DIR}" "${FACE_HOST_DRIVER_STAGE_DIR}"
	COMMAND ${CMAKE_COMMAND} -E touch "${FACE_HOST_STAMP}"
	DEPENDS ${FACE_HOST_INPUTS}
	WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
	COMMENT "Building OpenVRPair.FaceModuleHost (.NET 10)"
	VERBATIM)

add_custom_target(openvr_pair_feature_facetracking_host ALL
	DEPENDS "${FACE_HOST_STAMP}")

set_target_properties(openvr_pair_feature_facetracking_host PROPERTIES
	FOLDER "modules/facetracking")
