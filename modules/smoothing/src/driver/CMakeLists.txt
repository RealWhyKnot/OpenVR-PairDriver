cmake_minimum_required(VERSION 3.15)

set(SMOOTHING_PAIRDRIVER_ROOT "${OPENVR_PAIRDRIVER_ROOT}" CACHE PATH "OpenVR-WKPairDriver checkout")
if(NOT SMOOTHING_PAIRDRIVER_ROOT)
	set(_sibling_pairdriver "${CMAKE_CURRENT_LIST_DIR}/../../../OpenVR-WKPairDriver")
	if(EXISTS "${_sibling_pairdriver}/src/driver/DriverModule.h")
		get_filename_component(SMOOTHING_PAIRDRIVER_ROOT "${_sibling_pairdriver}" ABSOLUTE)
	else()
		get_filename_component(SMOOTHING_PAIRDRIVER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../lib/OpenVR-WKPairDriver" ABSOLUTE)
	endif()
endif()

add_library(openvr_pair_feature_smoothing_driver STATIC
	SkeletalHookInjector.cpp
	SkeletalHookInjector.h
	SmoothingDriverModule.cpp)

target_include_directories(openvr_pair_feature_smoothing_driver PUBLIC
	${CMAKE_CURRENT_LIST_DIR}
	${SMOOTHING_PAIRDRIVER_ROOT}/src/driver
	${SMOOTHING_PAIRDRIVER_ROOT}/src/common
	${SMOOTHING_PAIRDRIVER_ROOT}/lib
	${SMOOTHING_PAIRDRIVER_ROOT}/lib/openvr/headers
	${SMOOTHING_PAIRDRIVER_ROOT}/lib/minhook/include)

target_compile_definitions(openvr_pair_feature_smoothing_driver PRIVATE
	NOMINMAX
	UNICODE
	OPENVRPAIRDRIVER_EXPORTS)
