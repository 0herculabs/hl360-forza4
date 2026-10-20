# fm4 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(fm4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prefer the wheel-enabled SDL built locally by scripts/build-wheel-sdl.cmd.
# Some prebuilt ReXGlue SDKs ship SDL_HAPTIC=OFF; that library cannot drive FFB.
set(FM4_SDL3_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/out/install/sdl3-haptic" CACHE PATH
    "Local SDL install with haptic/DirectInput support")
if(EXISTS "${FM4_SDL3_ROOT}/cmake/SDL3Config.cmake")
    set(SDL3_DIR "${FM4_SDL3_ROOT}/cmake" CACHE PATH "SDL3 package directory" FORCE)
endif()

option(FM4_BUILD_GAME "Build the game (requires supported Disc 1 and code generation)" ON)
if(FM4_BUILD_GAME)
    include(generated/rexglue.cmake)
    if(APPLE)
        # Tens of thousands of virtual registration calls make Apple Clang's
        # optimizer and GlobalISel localizer very slow. This table runs only
        # during module startup; the SelectionDAG path handles it efficiently.
        # Skip the optimized PCH as its __OPTIMIZE__ macro differs at -O0.
        set(FM4_REGISTRY "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/fm4_register.cpp")
        set_property(SOURCE "${FM4_REGISTRY}" APPEND PROPERTY COMPILE_OPTIONS -O0 -fno-global-isel)
        set_source_files_properties("${FM4_REGISTRY}" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    endif()
else()
    # Avoid discovering generated DLL targets in a tools-only configuration.
    set(REXSDK_DIR "" CACHE PATH "Path to rexglue-sdk source tree")
    set(REXSDK_VERSION "" CACHE STRING "Override SDK version")
    if(REXSDK_DIR)
        add_subdirectory("${REXSDK_DIR}" rexglue-sdk)
    elseif(REXSDK_VERSION)
        find_package(rexglue ${REXSDK_VERSION} EXACT REQUIRED CONFIG)
    else()
        find_package(rexglue 0.10.0 REQUIRED CONFIG)
    endif()
endif()
if(FM4_BUILD_GAME AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/sources.cmake")
    message(FATAL_ERROR "Run rexglue codegen fm4_manifest.toml first, or use -DFM4_BUILD_GAME=OFF")
endif()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/config/fm4.toml" FM4_PORT_CONFIG)
if(APPLE)
    string(REPLACE "gpu_backend = \"d3d12\"" "gpu_backend = \"vulkan\"" FM4_PORT_CONFIG "${FM4_PORT_CONFIG}")
endif()
if(APPLE)
    string(APPEND FM4_PORT_CONFIG "\n# Keep logging off the guest and GPU threads.\nlog_async = true\n")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/fm4.toml" "${FM4_PORT_CONFIG}")

if(FM4_BUILD_GAME)

# Sources
set(FM4_SOURCES
    src/fm4_app.cpp
    src/crash_handler.cpp
    src/frame_stats.cpp
    src/file_signatures.cpp
    src/wheel.cpp
    src/wheel_input.cpp
    src/wheel_ffb.cpp
    src/main.cpp
)

if(WIN32)
    add_executable(fm4 WIN32 ${FM4_SOURCES})
else()
    add_executable(fm4 ${FM4_SOURCES})
endif()

rexglue_setup_target(fm4 GPU_PLUGINS xenos)

if(WIN32)
    target_compile_options(fm4 PRIVATE -gline-tables-only)
    target_link_options(fm4 PRIVATE -g)
    target_link_libraries(fm4 PRIVATE dbghelp bcrypt)
endif()
if(TARGET rex::gpu-xenos)
    set_property(TARGET fm4 APPEND PROPERTY LINK_DEPENDS "$<TARGET_FILE:rex::gpu-xenos>")
endif()
add_custom_command(TARGET fm4 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_BINARY_DIR}/fm4.toml $<TARGET_FILE_DIR:fm4>/fm4.toml)
endif()

add_executable(fm4tool tools/fm4tool/main.cpp)
target_include_directories(fm4tool PRIVATE launcher/src)
target_link_libraries(fm4tool PRIVATE rex::runtime rex::kernel rex::system)
if(WIN32)
    add_custom_command(TARGET fm4tool POST_BUILD
        COMMAND "$<$<BOOL:$<TARGET_RUNTIME_DLLS:fm4tool>>:${CMAKE_COMMAND};-E;copy_if_different;$<TARGET_RUNTIME_DLLS:fm4tool>;$<TARGET_FILE_DIR:fm4tool>>"
        COMMAND_EXPAND_LISTS VERBATIM)
endif()

# FM4 launcher, adapted from FM3; dependencies are vendored for offline builds.
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui)
find_package(SDL3 REQUIRED CONFIG)
add_executable(fm4-wheel-probe tools/wheel_probe.cpp src/wheel.cpp)
target_link_libraries(fm4-wheel-probe PRIVATE SDL3::SDL3-static)
add_executable(fm4-launcher
    launcher/src/main.cpp
    launcher/src/graphics_fixes.cpp
    launcher/src/installer.cpp
    launcher/src/settings.cpp
    launcher/src/sha256.cpp
    src/wheel.cpp
    src/crash_handler.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlgpu3.cpp)
target_include_directories(fm4-launcher BEFORE PRIVATE
    ${IMGUI_DIR} ${IMGUI_DIR}/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tomlplusplus/include)
set_target_properties(fm4-launcher PROPERTIES WIN32_EXECUTABLE $<BOOL:${WIN32}>)
target_link_libraries(fm4-launcher PRIVATE SDL3::SDL3-static)
if(WIN32)
    target_link_libraries(fm4-launcher PRIVATE dbghelp)
endif()
target_compile_options(fm4-launcher PRIVATE -gline-tables-only)
target_link_options(fm4-launcher PRIVATE -g)
set_target_properties(fm4-launcher PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
add_dependencies(fm4-launcher fm4tool)
add_custom_command(TARGET fm4-launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/launcher/data/game_files.sha256
        ${CMAKE_CURRENT_BINARY_DIR}/fm4.toml
        $<TARGET_FILE_DIR:fm4-launcher>)
configure_file(scripts/install-forza-plus.ps1 ${CMAKE_CURRENT_BINARY_DIR}/install-forza-plus.ps1 COPYONLY)
configure_file(scripts/uninstall-forza-plus.ps1 ${CMAKE_CURRENT_BINARY_DIR}/uninstall-forza-plus.ps1 COPYONLY)

option(FM4_BUILD_TESTS "Build launcher compatibility regression checks" OFF)
if(FM4_BUILD_TESTS)
    enable_testing()
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME macos-tooling COMMAND ${Python3_EXECUTABLE} -m unittest discover
            -s ${CMAKE_CURRENT_SOURCE_DIR}/tests -p "test_*.py")
    endif()
    add_executable(fm4-frame-stats-tests tests/frame_stats_tests.cpp src/frame_stats.cpp)
    target_include_directories(fm4-frame-stats-tests PRIVATE src)
    target_link_libraries(fm4-frame-stats-tests PRIVATE rex::runtime)
    add_test(NAME frame-stats-regressions COMMAND fm4-frame-stats-tests)
    add_executable(fm4-host-path-tests tests/host_path_tests.cpp)
    target_link_libraries(fm4-host-path-tests PRIVATE rex::runtime)
    if(FM4_BUILD_GAME)
        add_test(NAME module-reload-regressions COMMAND fm4tool module-reload-test
            "${CMAKE_CURRENT_SOURCE_DIR}/game/disc1"
            "${CMAKE_CURRENT_BINARY_DIR}/module-reload-user" "fm4_XMediaFacade_default")
        add_test(NAME disc-path-regressions COMMAND fm4-host-path-tests
            "${CMAKE_CURRENT_SOURCE_DIR}/game/disc1")
    endif()
    add_executable(fm4-async-logging-tests tests/async_logging_tests.cpp)
    target_link_libraries(fm4-async-logging-tests PRIVATE rex::runtime)
    add_test(NAME async-logging-regressions COMMAND fm4-async-logging-tests)
    set_tests_properties(async-logging-regressions PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(fm4-wheel-calibration-tests tests/wheel_calibration_tests.cpp)
    add_test(NAME wheel-calibration-tour COMMAND fm4-wheel-calibration-tests)
    add_executable(fm4-launcher-tests tests/graphics_fixes_tests.cpp
        launcher/src/graphics_fixes.cpp launcher/src/settings.cpp)
    target_include_directories(fm4-launcher-tests PRIVATE launcher/src
        thirdparty/tomlplusplus/include)
    set_target_properties(fm4-launcher-tests PROPERTIES CXX_STANDARD 17)
    add_test(NAME launcher-compatibility COMMAND fm4-launcher-tests)
    add_executable(fm4-wheel-tests tests/wheel_tests.cpp src/wheel.cpp launcher/src/settings.cpp)
    target_include_directories(fm4-wheel-tests PRIVATE thirdparty/tomlplusplus/include)
    target_link_libraries(fm4-wheel-tests PRIVATE SDL3::SDL3-static)
    set_target_properties(fm4-wheel-tests PROPERTIES CXX_STANDARD 17)
    add_test(NAME wheel-input COMMAND fm4-wheel-tests)
    add_executable(fm4-wheel-ffb-tests tests/wheel_ffb_tests.cpp src/wheel_ffb.cpp)
    target_include_directories(fm4-wheel-ffb-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(fm4-wheel-ffb-tests PRIVATE rex::runtime)
    add_test(NAME wheel-native-ffb COMMAND fm4-wheel-ffb-tests)
    add_executable(fm4-wheel-runtime-tests tests/wheel_runtime_tests.cpp src/wheel.cpp src/wheel_input.cpp)
    target_link_libraries(fm4-wheel-runtime-tests PRIVATE rex::runtime)
    add_test(NAME wheel-runtime COMMAND fm4-wheel-runtime-tests)
    add_executable(fm4-wheel-haptic-backend-tests tests/wheel_haptic_backend_tests.cpp)
    target_link_libraries(fm4-wheel-haptic-backend-tests PRIVATE SDL3::SDL3-static)
    add_test(NAME wheel-haptic-backend COMMAND fm4-wheel-haptic-backend-tests)
    add_executable(fm4-wheel-ui-tests tests/wheel_ui_tests.cpp src/wheel.cpp)
    target_link_libraries(fm4-wheel-ui-tests PRIVATE SDL3::SDL3-static)
    add_test(NAME wheel-ui COMMAND fm4-wheel-ui-tests)
    set_tests_properties(wheel-ui PROPERTIES TIMEOUT 20)
    if(WIN32)
        add_test(NAME forza-plus-installer COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/forza_plus_tests.ps1
            -Installer ${CMAKE_CURRENT_SOURCE_DIR}/scripts/install-forza-plus.ps1
            -OutputDir ${CMAKE_CURRENT_BINARY_DIR}/forza-plus-tests)
    endif()
endif()
