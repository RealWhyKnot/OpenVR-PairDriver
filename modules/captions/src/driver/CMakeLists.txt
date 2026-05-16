cmake_minimum_required(VERSION 3.15)

set(CAPTIONS_PAIRDRIVER_ROOT "${CMAKE_SOURCE_DIR}")

add_library(openvr_pair_feature_captions_driver STATIC
    HostSupervisor.cpp
    HostSupervisor.h
    Logging.cpp
    Logging.h
    CaptionsDriverModule.cpp)

target_include_directories(openvr_pair_feature_captions_driver PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CAPTIONS_PAIRDRIVER_ROOT}/core/src/driver
    ${CAPTIONS_PAIRDRIVER_ROOT}/core/src/common
    ${CAPTIONS_PAIRDRIVER_ROOT}/lib
    ${CAPTIONS_PAIRDRIVER_ROOT}/lib/Eigen
    ${CAPTIONS_PAIRDRIVER_ROOT}/lib/openvr/headers)

target_link_libraries(openvr_pair_feature_captions_driver PRIVATE
    openvr_pair_common)

target_compile_definitions(openvr_pair_feature_captions_driver PRIVATE
    NOMINMAX
    UNICODE
    OPENVRPAIRDRIVER_EXPORTS)
