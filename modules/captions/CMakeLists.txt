cmake_minimum_required(VERSION 3.15)

# Module root for the real-time STT + translation feature.
# Sub-targets are included by the root CMakeLists (driver, overlay, tests) and
# by the host CMakeLists (src/host). This file exists to document the layout;
# the actual add_subdirectory calls live in core/src/driver/CMakeLists.txt,
# core/src/overlay/CMakeLists.txt, and the root CMakeLists.txt.
#
# To skip the captions host (no inference library deps on build host):
#   cmake -DOPENVR_PAIR_BUILD_CAPTIONS_HOST=OFF ...
