cmake_minimum_required(VERSION 3.25)
project(fm4_macos_sdk_test LANGUAGES CXX)
if(NOT APPLE OR NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    message(FATAL_ERROR "Use macOS with CMAKE_OSX_ARCHITECTURES=arm64")
endif()
find_package(rexglue CONFIG REQUIRED)
if(NOT REXGLUE_USE_VULKAN)
    message(FATAL_ERROR "The SDK must include Vulkan")
endif()
add_executable(fm4-macos-sdk-test MACOSX_BUNDLE main.cpp)
set_target_properties(fm4-macos-sdk-test PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "FM4 GPU Test"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.hl360.fm4.gpu-test")
target_compile_features(fm4-macos-sdk-test PRIVATE cxx_std_23)
target_link_libraries(fm4-macos-sdk-test PRIVATE rex::runtime)
# Compile actual app hooks without requiring game assets or generated code.
set(STUBS "${CMAKE_CURRENT_BINARY_DIR}/stubs")
file(MAKE_DIRECTORY "${STUBS}/generated/default")
file(WRITE "${STUBS}/generated/default/fm4_init.h"
    "#pragma once\n#include <rex/image_info.h>\nextern const rex::PPCImageInfo PPCImageConfig;\n")
add_library(fm4-app-compile-test OBJECT
    ../../src/main.cpp ../../src/fm4_app.cpp ../../src/frame_stats.cpp
    ../../src/file_signatures.cpp ../../src/crash_handler.cpp
    ../../src/wheel.cpp ../../src/wheel_input.cpp ../../src/wheel_ffb.cpp)
target_compile_features(fm4-app-compile-test PRIVATE cxx_std_23)
target_include_directories(fm4-app-compile-test PRIVATE "${STUBS}")
target_link_libraries(fm4-app-compile-test PRIVATE rex::runtime)
