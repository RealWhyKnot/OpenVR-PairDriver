cmake_minimum_required(VERSION 3.15)

project("lib")

# MinHook -- needed by the umbrella driver and feature driver modules.
message("Adding minhook...")
add_subdirectory(minhook)
set_property(TARGET minhook PROPERTY FOLDER "lib")

# GLFW (window + OpenGL context for the overlay UI).
message("Adding GLFW...")
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS   OFF CACHE BOOL "" FORCE)
add_subdirectory(glfw)
set_property(TARGET glfw         PROPERTY FOLDER "lib/glfw")
if(TARGET update_mappings)
    set_property(TARGET update_mappings PROPERTY FOLDER "lib/glfw")
endif()

# gl3w -- OpenGL extension loader.
# The gl3w submodule is a generator repo; run gl3w_gen.py at configure time
# to produce include/GL/gl3w.h and src/gl3w.c from the Khronos registry headers.
message("Adding gl3w...")
set(GL3W_ROOT "${CMAKE_SOURCE_DIR}/lib/gl3w")
set(GL3W_GENERATED_H "${GL3W_ROOT}/include/GL/gl3w.h")
set(GL3W_GENERATED_C "${GL3W_ROOT}/src/gl3w.c")
if(NOT EXISTS "${GL3W_GENERATED_H}" OR NOT EXISTS "${GL3W_GENERATED_C}")
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        message(STATUS "gl3w: running generator (Python ${Python3_VERSION})")
        execute_process(
            COMMAND "${Python3_EXECUTABLE}" "${GL3W_ROOT}/gl3w_gen.py"
            WORKING_DIRECTORY "${GL3W_ROOT}"
            RESULT_VARIABLE GL3W_GEN_RESULT)
        if(NOT GL3W_GEN_RESULT EQUAL 0)
            message(WARNING "gl3w generator failed (exit ${GL3W_GEN_RESULT}); build will fail if headers are missing")
        endif()
    else()
        message(WARNING "gl3w: Python3 not found; pre-generated headers not present -- build will fail")
    endif()
else()
    message(STATUS "gl3w: using submodule pre-generated headers (Python generator not invoked)")
endif()
add_library(pair_gl3w STATIC ${GL3W_GENERATED_C})
target_include_directories(pair_gl3w PUBLIC ${GL3W_ROOT}/include)
set_property(TARGET pair_gl3w PROPERTY FOLDER "lib")

# Dear ImGui (core + GLFW/OpenGL3 backends).
message("Adding ImGui...")
add_library(pair_imgui STATIC
    ${CMAKE_SOURCE_DIR}/lib/imgui/imgui.cpp
    ${CMAKE_SOURCE_DIR}/lib/imgui/imgui_demo.cpp
    ${CMAKE_SOURCE_DIR}/lib/imgui/imgui_draw.cpp
    ${CMAKE_SOURCE_DIR}/lib/imgui/imgui_tables.cpp
    ${CMAKE_SOURCE_DIR}/lib/imgui/imgui_widgets.cpp
    ${CMAKE_SOURCE_DIR}/lib/imgui/backends/imgui_impl_glfw.cpp
    ${CMAKE_SOURCE_DIR}/lib/imgui/backends/imgui_impl_opengl3.cpp)
target_include_directories(pair_imgui PUBLIC
    ${CMAKE_SOURCE_DIR}/lib/imgui
    ${CMAKE_SOURCE_DIR}/lib/imgui/backends
    ${CMAKE_SOURCE_DIR}/lib)
target_compile_definitions(pair_imgui PRIVATE IMGUI_DEFINE_MATH_OPERATORS)
target_link_libraries(pair_imgui PUBLIC glfw pair_gl3w)
set_property(TARGET pair_imgui PROPERTY FOLDER "lib")

# ImPlot -- plotting add-on for ImGui.
message("Adding ImPlot...")
add_library(pair_implot STATIC
    ${CMAKE_SOURCE_DIR}/lib/implot/implot.cpp
    ${CMAKE_SOURCE_DIR}/lib/implot/implot_items.cpp)
target_include_directories(pair_implot PUBLIC
    ${CMAKE_SOURCE_DIR}/lib/implot
    ${CMAKE_SOURCE_DIR}/lib)
target_compile_definitions(pair_implot PRIVATE IMGUI_DEFINE_MATH_OPERATORS)
target_link_libraries(pair_implot PUBLIC pair_imgui)
set_property(TARGET pair_implot PROPERTY FOLDER "lib")

# Discord RPC -- Rich Presence static library built from source.
# Bypasses the upstream CMakeLists (which tries to download rapidjson at
# configure time); instead we point it at the rapidjson submodule directly.
message("Adding discord-rpc...")
add_library(pair_discord_rpc STATIC
    ${CMAKE_SOURCE_DIR}/lib/discord-rpc/src/discord_rpc.cpp
    ${CMAKE_SOURCE_DIR}/lib/discord-rpc/src/discord_register_win.cpp
    ${CMAKE_SOURCE_DIR}/lib/discord-rpc/src/connection_win.cpp
    ${CMAKE_SOURCE_DIR}/lib/discord-rpc/src/serialization.cpp
    ${CMAKE_SOURCE_DIR}/lib/discord-rpc/src/rpc_connection.cpp)
target_include_directories(pair_discord_rpc PUBLIC
    ${CMAKE_SOURCE_DIR}/lib/discord-rpc/include
    ${CMAKE_SOURCE_DIR}/lib/rapidjson/include)
target_compile_definitions(pair_discord_rpc PRIVATE DISCORD_WINDOWS)
target_link_libraries(pair_discord_rpc PRIVATE psapi advapi32 Ws2_32)
set_property(TARGET pair_discord_rpc PROPERTY FOLDER "lib")

# Eigen -- header-only. The upstream repo lays headers under lib/Eigen/Eigen/*,
# so a consumer using <Eigen/Dense> needs lib/Eigen on the include path (not
# lib, which would only find the old flat-vendored layout).
add_library(pair_eigen INTERFACE)
target_include_directories(pair_eigen INTERFACE
    ${CMAKE_SOURCE_DIR}/lib/Eigen)
