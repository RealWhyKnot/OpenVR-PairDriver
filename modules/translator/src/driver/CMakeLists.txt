cmake_minimum_required(VERSION 3.15)

set(TRANSLATOR_PAIRDRIVER_ROOT "${CMAKE_SOURCE_DIR}")

add_library(openvr_pair_feature_translator_driver STATIC
    HostSupervisor.cpp
    HostSupervisor.h
    Logging.cpp
    Logging.h
    TranslatorDriverModule.cpp)

target_include_directories(openvr_pair_feature_translator_driver PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${TRANSLATOR_PAIRDRIVER_ROOT}/core/src/driver
    ${TRANSLATOR_PAIRDRIVER_ROOT}/core/src/common
    ${TRANSLATOR_PAIRDRIVER_ROOT}/lib
    ${TRANSLATOR_PAIRDRIVER_ROOT}/lib/Eigen
    ${TRANSLATOR_PAIRDRIVER_ROOT}/lib/openvr/headers)

target_link_libraries(openvr_pair_feature_translator_driver PRIVATE
    openvr_pair_common)

target_compile_definitions(openvr_pair_feature_translator_driver PRIVATE
    NOMINMAX
    UNICODE
    OPENVRPAIRDRIVER_EXPORTS)
