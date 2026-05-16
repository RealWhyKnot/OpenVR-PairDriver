cmake_minimum_required(VERSION 3.14)

# Keep test registration in one place. Module-owned unit tests remain beside the
# code they cover, while cross-module scenarios live under this tests/ tree.
add_subdirectory("${CMAKE_SOURCE_DIR}/modules/calibration/tests" "${CMAKE_BINARY_DIR}/tests/calibration")
add_subdirectory("${CMAKE_SOURCE_DIR}/modules/facetracking/tests" "${CMAKE_BINARY_DIR}/tests/facetracking")
add_subdirectory("${CMAKE_SOURCE_DIR}/modules/inputhealth/tests" "${CMAKE_BINARY_DIR}/tests/inputhealth")
add_subdirectory("${CMAKE_SOURCE_DIR}/modules/oscrouter/tests" "${CMAKE_BINARY_DIR}/tests/oscrouter")
add_subdirectory("${CMAKE_SOURCE_DIR}/modules/translator/tests" "${CMAKE_BINARY_DIR}/tests/translator")
add_subdirectory(e2e)
