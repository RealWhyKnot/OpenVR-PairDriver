cmake_minimum_required(VERSION 3.15)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

project("OpenVR-WKPair")

# Build artifacts go under build/artifacts so we can find them from build.ps1
# and the consumer repos' deploy scripts without grepping.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/artifacts)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/artifacts)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/artifacts)

# adjust_bin_paths: redirect a target's output directories to artifacts/.
# Used by module CMakeLists (e.g. spacecal_tests) that were originally built
# standalone and call this helper.
function(adjust_bin_paths target_name)
    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/artifacts
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/artifacts
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/artifacts)
endfunction()

# Release-only LTCG. The driver isn't math-heavy enough for it to matter much,
# but the InterfaceHookInjector + SkeletalHookInjector detours are on hot paths
# (340 Hz/hand for skeletal, kHz across all tracked devices for pose) and any
# inlining win there is worth the linker time.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

# lib first: third-party targets must exist before the feature modules reference them.
add_subdirectory(lib)

# Core umbrella driver + overlay. These CMakeLists add the feature
# module driver/overlay subdirectories directly.
add_subdirectory(core/src)

# Unit tests (not pulled in by any core CMakeLists).
add_subdirectory(modules/calibration/tests)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC" OR "${CMAKE_C_COMPILER_ID}" STREQUAL "MSVC")
	add_compile_options(
		/we4715  # not all control paths return a value
		/we4828  # disallow invalid characters
		/we4473 /we4474 /we4475 /we4476 /we4477 /we4478  # printf format-string mismatches
		/we4775 /we4776 /we4777 /we4778
		/we4002 /we4003  # macro arg count mismatches
		/MP      # multiprocessor compilation
		/utf-8
		/GF)     # fold duplicate strings
	add_compile_options(
		$<$<CONFIG:Release>:/fp:fast>
		$<$<CONFIG:Release>:/arch:AVX2>)
endif()
