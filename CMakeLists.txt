cmake_minimum_required(VERSION 3.15)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

project("OpenVR-PairDriver")

# Build artifacts go under build/artifacts so we can find them from build.ps1
# and the consumer repos' deploy scripts without grepping.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/artifacts)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/artifacts)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/artifacts)

# Release-only LTCG. The driver isn't math-heavy enough for it to matter much,
# but the InterfaceHookInjector + SkeletalHookInjector detours are on hot paths
# (340 Hz/hand for skeletal, kHz across all tracked devices for pose) and any
# inlining win there is worth the linker time.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

add_subdirectory(src)
add_subdirectory(lib)

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
