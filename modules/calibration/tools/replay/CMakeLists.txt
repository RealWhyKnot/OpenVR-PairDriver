cmake_minimum_required(VERSION 3.15)

# Offline replay harness for spacecal_log_v2 captures. Links directly against
# CalibrationCalc.cpp so we re-run the production math against captured raw
# poses; CalibrationMetrics_stub.cpp provides no-op shims for the metrics and
# CalCtx symbols that CalibrationCalc references in the live overlay context.

project(SpaceCalibratorReplay)
message("SpaceCalibrator - Replay Harness")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SPACECAL_SRC "${CMAKE_SOURCE_DIR}/modules/calibration/src")

set(REPLAY_SOURCES
    main.cpp
    CalibrationMetrics_stub.cpp
    ${SPACECAL_SRC}/overlay/CalibrationCalc.cpp
    ${SPACECAL_SRC}/overlay/TranslationSolveDirect.cpp
)

add_executable(spacecal_replay ${REPLAY_SOURCES})

target_include_directories(spacecal_replay
    PRIVATE ${CMAKE_SOURCE_DIR}
    PRIVATE ${SPACECAL_SRC}/common
    PRIVATE ${SPACECAL_SRC}/overlay
    # Protocol.h and MotionGate.h live in core/src/common in the monorepo.
    PRIVATE ${CMAKE_SOURCE_DIR}/core/src/common
    PRIVATE ${CMAKE_SOURCE_DIR}/lib
    PRIVATE ${CMAKE_SOURCE_DIR}/lib/openvr/headers
    PRIVATE ${CMAKE_SOURCE_DIR}/lib/Eigen
)

target_compile_definitions(spacecal_replay
    PRIVATE NOMINMAX
)

# CalibrationCalc.cpp uses Eigen, which can produce huge object files in C++20.
if (MSVC)
    target_compile_options(spacecal_replay PRIVATE /bigobj)
else ()
    target_compile_options(spacecal_replay PRIVATE -Wa,-mbig-obj)
endif ()

# Place the binary in the same artifacts/<config>/ folder as the overlay so it's
# easy to find next to the rest of the build output.
set_target_properties(spacecal_replay PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/artifacts
)
