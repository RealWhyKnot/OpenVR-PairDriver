cmake_minimum_required(VERSION 3.15)

set(INPUTHEALTH_PAIRDRIVER_ROOT "${OPENVR_PAIRDRIVER_ROOT}" CACHE PATH "OpenVR-WKPairDriver checkout")
if(NOT INPUTHEALTH_PAIRDRIVER_ROOT)
	set(_sibling_pairdriver "${CMAKE_CURRENT_LIST_DIR}/../../../OpenVR-WKPairDriver")
	if(EXISTS "${_sibling_pairdriver}/src/driver/DriverModule.h")
		get_filename_component(INPUTHEALTH_PAIRDRIVER_ROOT "${_sibling_pairdriver}" ABSOLUTE)
	else()
		get_filename_component(INPUTHEALTH_PAIRDRIVER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../lib/OpenVR-WKPairDriver" ABSOLUTE)
	endif()
endif()

add_library(openvr_pair_feature_inputhealth_driver STATIC
	InputHealthCompensation.cpp
	InputHealthCompensation.h
	InputHealthComponentRegistry.cpp
	InputHealthComponentRegistry.h
	InputHealthDriverModule.cpp
	InputHealthHookInjector.cpp
	InputHealthHookInjector.h
	InputHealthObservation.cpp
	InputHealthObservation.h
	InputHealthSnapshotPublisher.cpp
	InputHealthSnapshotPublisher.h
	InputHealthSnapshotStaging.cpp
	InputHealthSnapshotStaging.h
	InputHealthState.h
	inputhealth/PerComponentStats.h)

target_include_directories(openvr_pair_feature_inputhealth_driver PUBLIC
	${CMAKE_CURRENT_LIST_DIR}
	${INPUTHEALTH_PAIRDRIVER_ROOT}/src/driver
	${INPUTHEALTH_PAIRDRIVER_ROOT}/src/common
	${INPUTHEALTH_PAIRDRIVER_ROOT}/lib
	${INPUTHEALTH_PAIRDRIVER_ROOT}/lib/openvr/headers
	${INPUTHEALTH_PAIRDRIVER_ROOT}/lib/minhook/include)

target_compile_definitions(openvr_pair_feature_inputhealth_driver PRIVATE
	NOMINMAX
	UNICODE
	OPENVRPAIRDRIVER_EXPORTS)
