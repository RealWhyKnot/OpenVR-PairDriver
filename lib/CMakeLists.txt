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
message("Adding gl3w...")
add_library(pair_gl3w STATIC ${CMAKE_SOURCE_DIR}/lib/gl3w/src/gl3w.c)
target_include_directories(pair_gl3w PUBLIC ${CMAKE_SOURCE_DIR}/lib/gl3w/include)
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
