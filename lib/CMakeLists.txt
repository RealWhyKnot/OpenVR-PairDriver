cmake_minimum_required(VERSION 3.24)

project("lib")

# MinHook -- the only third-party dependency the driver needs at runtime.
# OpenVR is consumed as headers + import library, not a CMake target.
message("Adding minhook...")
add_subdirectory(minhook)

set_property(TARGET minhook PROPERTY FOLDER "lib")
