cmake_minimum_required(VERSION 3.15)

# In the monorepo, the PairDriver root is the repo root (CMAKE_SOURCE_DIR).
set(INPUTHEALTH_PAIRDRIVER_ROOT "${CMAKE_SOURCE_DIR}")

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

target_include_directories(openvr_pair_feature_inputhealth_driver PRIVATE
	${CMAKE_CURRENT_LIST_DIR}
	${INPUTHEALTH_PAIRDRIVER_ROOT}/core/src/driver
	${INPUTHEALTH_PAIRDRIVER_ROOT}/core/src/common
	${INPUTHEALTH_PAIRDRIVER_ROOT}/lib
	${INPUTHEALTH_PAIRDRIVER_ROOT}/lib/openvr/headers
	${INPUTHEALTH_PAIRDRIVER_ROOT}/lib/minhook/include)

target_compile_definitions(openvr_pair_feature_inputhealth_driver PRIVATE
	NOMINMAX
	UNICODE
	OPENVRPAIRDRIVER_EXPORTS)
