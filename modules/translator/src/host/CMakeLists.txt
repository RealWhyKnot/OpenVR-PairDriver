cmake_minimum_required(VERSION 3.15)

# Translator host sidecar (WKOpenVR.TranslatorHost.exe).
# Depends on: whisper.cpp (FetchContent), ONNX Runtime (vendored or prebuilt),
# CTranslate2 (vendored or prebuilt), OpenVR, WinHTTP, WASAPI.
#
# All three inference libraries are gated by CMake options so a contributor
# without the prebuilt DLLs can still build the driver+overlay with stub stubs.

option(OPENVR_PAIR_BUILD_TRANSLATOR_HOST
    "Build the native translator host sidecar. Off lets a C++-only contributor build without the inference library dependencies."
    ON)

if(NOT OPENVR_PAIR_BUILD_TRANSLATOR_HOST)
    message(STATUS "OPENVR_PAIR_BUILD_TRANSLATOR_HOST=OFF: skipping translator host build.")
    return()
endif()

# ---------------------------------------------------------------------------
# whisper.cpp (FetchContent -- MIT)
# ---------------------------------------------------------------------------
include(FetchContent)

# WHISPER_CUDA: off by default; set -DWKOPENVR_TRANSLATOR_CUDA=ON to enable.
option(WKOPENVR_TRANSLATOR_CUDA "Build whisper.cpp with CUDA backend" OFF)

set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GGML_CUDA ${WKOPENVR_TRANSLATOR_CUDA} CACHE BOOL "" FORCE)

FetchContent_Declare(
    whisper_cpp
    GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
    GIT_TAG        v1.7.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(whisper_cpp)

# ---------------------------------------------------------------------------
# ONNX Runtime (prebuilt DLL -- Microsoft, MIT)
# Location: lib/onnxruntime/ with onnxruntime.lib and onnxruntime.dll
# If not present the build silently stubs out Silero VAD.
# ---------------------------------------------------------------------------
set(ORT_ROOT "${CMAKE_SOURCE_DIR}/lib/onnxruntime")
if(EXISTS "${ORT_ROOT}/include/onnxruntime_c_api.h")
    set(HAVE_ORT ON)
    message(STATUS "ONNX Runtime found at ${ORT_ROOT}")
else()
    set(HAVE_ORT OFF)
    message(WARNING "ONNX Runtime not found at ${ORT_ROOT}; Silero VAD will be stubbed.")
endif()

# ---------------------------------------------------------------------------
# CTranslate2 (prebuilt -- OpenNMT, MIT)
# Location: lib/ctranslate2/ with ctranslate2.lib and ctranslate2.dll
# ---------------------------------------------------------------------------
set(CT2_ROOT "${CMAKE_SOURCE_DIR}/lib/ctranslate2")
if(EXISTS "${CT2_ROOT}/include/ctranslate2/translator.h")
    set(HAVE_CT2 ON)
    message(STATUS "CTranslate2 found at ${CT2_ROOT}")
else()
    set(HAVE_CT2 OFF)
    message(WARNING "CTranslate2 not found at ${CT2_ROOT}; translation will be stubbed.")
endif()

# ---------------------------------------------------------------------------
# Host executable
# ---------------------------------------------------------------------------
set(TRANSLATOR_PAIRDRIVER_ROOT "${CMAKE_SOURCE_DIR}")
set(OPENVR_LIB_DIR "${CMAKE_SOURCE_DIR}/lib/openvr")

find_library(OPENVR_LIBRARIES
    NAMES openvr_api
    PATHS ${OPENVR_LIB_DIR}/bin ${OPENVR_LIB_DIR}/lib
    PATH_SUFFIXES win64
    NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)

set(HOST_SOURCES
    ActionBindings.cpp
    ActionBindings.h
    ChatboxPacer.cpp
    ChatboxPacer.h
    HostStatus.cpp
    HostStatus.h
    Logging.cpp
    Logging.h
    ModelDownloader.cpp
    ModelDownloader.h
    RouterPublisher.cpp
    RouterPublisher.h
    SileroVad.cpp
    SileroVad.h
    Translator.cpp
    Translator.h
    WasapiCapture.cpp
    WasapiCapture.h
    WhisperEngine.cpp
    WhisperEngine.h
    main.cpp)

add_executable(WKOpenVRTranslatorHost ${HOST_SOURCES})
set_target_properties(WKOpenVRTranslatorHost PROPERTIES
    OUTPUT_NAME "WKOpenVR.TranslatorHost"
    WIN32_EXECUTABLE TRUE)

target_compile_definitions(WKOpenVRTranslatorHost PRIVATE
    NOMINMAX
    UNICODE
    WIN32_LEAN_AND_MEAN)

target_compile_features(WKOpenVRTranslatorHost PRIVATE cxx_std_17)

# WIN32_EXECUTABLE sets SUBSYSTEM:WINDOWS (no console window) but expects WinMain.
# Override the entry point so int main() is used as-is.
if(MSVC)
    target_link_options(WKOpenVRTranslatorHost PRIVATE /ENTRY:mainCRTStartup)
endif()

target_include_directories(WKOpenVRTranslatorHost PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${OPENVR_LIB_DIR}/headers
    ${TRANSLATOR_PAIRDRIVER_ROOT}/lib)

target_link_libraries(WKOpenVRTranslatorHost PRIVATE
    whisper
    ${OPENVR_LIBRARIES}
    ole32
    shell32
    winhttp
    mmdevapi)

# ONNX Runtime.
if(HAVE_ORT)
    target_include_directories(WKOpenVRTranslatorHost PRIVATE ${ORT_ROOT}/include)
    find_library(ORT_LIB onnxruntime PATHS "${ORT_ROOT}/lib" NO_DEFAULT_PATH)
    if(ORT_LIB)
        target_link_libraries(WKOpenVRTranslatorHost PRIVATE ${ORT_LIB})
        target_compile_definitions(WKOpenVRTranslatorHost PRIVATE HAVE_ORT=1)
    endif()
endif()

# CTranslate2.
if(HAVE_CT2)
    target_include_directories(WKOpenVRTranslatorHost PRIVATE ${CT2_ROOT}/include)
    find_library(CT2_LIB ctranslate2 PATHS "${CT2_ROOT}/lib" NO_DEFAULT_PATH)
    if(CT2_LIB)
        target_link_libraries(WKOpenVRTranslatorHost PRIVATE ${CT2_LIB})
        target_compile_definitions(WKOpenVRTranslatorHost PRIVATE HAVE_CT2=1)
    endif()
endif()

# ---------------------------------------------------------------------------
# Stage resources (silero_vad.onnx) and bindings into the build tree.
# ---------------------------------------------------------------------------
set(TRANSLATOR_HOST_STAGE_DIR
    "${CMAKE_BINARY_DIR}/driver_wkopenvr/resources/translator/host")

add_custom_command(TARGET WKOpenVRTranslatorHost POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TRANSLATOR_HOST_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TRANSLATOR_HOST_STAGE_DIR}/resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:WKOpenVRTranslatorHost>"
        "${TRANSLATOR_HOST_STAGE_DIR}/WKOpenVR.TranslatorHost.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_LIST_DIR}/actions.json"
        "${TRANSLATOR_HOST_STAGE_DIR}/actions.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_LIST_DIR}/bindings_knuckles.json"
        "${TRANSLATOR_HOST_STAGE_DIR}/bindings_knuckles.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_LIST_DIR}/bindings_touch.json"
        "${TRANSLATOR_HOST_STAGE_DIR}/bindings_touch.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_LIST_DIR}/bindings_vive.json"
        "${TRANSLATOR_HOST_STAGE_DIR}/bindings_vive.json"
    COMMENT "Staging WKOpenVR.TranslatorHost")

# Copy silero_vad.onnx if it exists in the source tree.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/resources/silero_vad.onnx")
    add_custom_command(TARGET WKOpenVRTranslatorHost POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_LIST_DIR}/resources/silero_vad.onnx"
            "${TRANSLATOR_HOST_STAGE_DIR}/resources/silero_vad.onnx")
endif()

adjust_bin_paths(WKOpenVRTranslatorHost)

set_target_properties(WKOpenVRTranslatorHost PROPERTIES
    FOLDER "modules/translator")
