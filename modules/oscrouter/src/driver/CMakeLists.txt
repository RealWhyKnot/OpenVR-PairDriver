cmake_minimum_required(VERSION 3.15)

set(OSCROUTER_PAIRDRIVER_ROOT "${CMAKE_SOURCE_DIR}")

add_library(openvr_pair_feature_oscrouter_driver STATIC
    Logging.cpp
    Logging.h
    OscRouter.cpp
    OscRouter.h
    OscWire.cpp
    OscWire.h
    RouteTable.cpp
    RouteTable.h
    RouterPublishApi.cpp
    RouterPublishApi.h
    UdpSender.cpp
    UdpSender.h)

target_include_directories(openvr_pair_feature_oscrouter_driver PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${OSCROUTER_PAIRDRIVER_ROOT}/core/src/driver
    ${OSCROUTER_PAIRDRIVER_ROOT}/core/src/common
    ${OSCROUTER_PAIRDRIVER_ROOT}/lib
    ${OSCROUTER_PAIRDRIVER_ROOT}/lib/openvr/headers)

target_link_libraries(openvr_pair_feature_oscrouter_driver PRIVATE
    openvr_pair_common
    Ws2_32)

target_compile_definitions(openvr_pair_feature_oscrouter_driver PRIVATE
    NOMINMAX
    UNICODE
    OPENVRPAIRDRIVER_EXPORTS)
