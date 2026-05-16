cmake_minimum_required(VERSION 3.14)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Cross-module tests launch staged sidecar executables and verify packets at a
# fake VRChat UDP endpoint. Unit tests stay in modules/<name>/tests.
add_executable(e2e_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/e2e_tests.cpp)

target_include_directories(e2e_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/core/src/common
    ${CMAKE_SOURCE_DIR}/core/src/driver
    ${CMAKE_SOURCE_DIR}/modules/facetracking/src/driver
    ${CMAKE_SOURCE_DIR}/modules/oscrouter/src/driver
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/lib/openvr/headers)

target_link_libraries(e2e_tests PRIVATE
    GTest::gtest_main
    openvr_pair_common
    openvr_pair_feature_oscrouter_driver
    openvr_pair_feature_facetracking_driver
    Ws2_32)

if(TARGET openvr_pair_feature_facetracking_host)
    add_dependencies(e2e_tests openvr_pair_feature_facetracking_host)
endif()

if(TARGET WKOpenVRCaptionsHost)
    add_dependencies(e2e_tests WKOpenVRCaptionsHost)
endif()

target_compile_definitions(e2e_tests PRIVATE
    NOMINMAX
    UNICODE
    WKOPENVR_E2E_TESTS=1)

if(MSVC)
    target_compile_options(e2e_tests PRIVATE /bigobj)
endif()

adjust_bin_paths(e2e_tests)

include(GoogleTest)
gtest_discover_tests(e2e_tests)

set_property(TARGET e2e_tests PROPERTY FOLDER "tests")
