cmake_minimum_required(VERSION 3.14)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(TR_HOST_SRC "${CMAKE_SOURCE_DIR}/modules/translator/src/host")

add_executable(translator_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/translator_tests.cpp
    # Pull in the ChatboxPacer implementation for the pacer tests.
    ${TR_HOST_SRC}/ChatboxPacer.cpp)

target_include_directories(translator_tests PRIVATE
    ${TR_HOST_SRC}
    ${CMAKE_SOURCE_DIR}/core/src/common
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/lib/openvr/headers)

target_link_libraries(translator_tests PRIVATE GTest::gtest_main)

target_compile_definitions(translator_tests PRIVATE
    NOMINMAX
    UNICODE
    TRANSLATOR_TESTS=1)

if(MSVC)
    target_compile_options(translator_tests PRIVATE /bigobj)
endif()

adjust_bin_paths(translator_tests)

include(GoogleTest)
gtest_discover_tests(translator_tests)

set_property(TARGET translator_tests PROPERTY FOLDER "tests")
