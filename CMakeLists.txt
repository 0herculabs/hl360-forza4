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

include(generated/rexglue.cmake)

# Sources
set(FM4_SOURCES
    src/fm4_app.cpp
    src/crash_handler.cpp
    src/frame_stats.cpp
    src/file_signatures.cpp
    src/wheel.cpp
    src/wheel_mapping.cpp
    src/wheel_input.cpp
    src/wheel_ffb.cpp
    src/telemetry/forza_dash.cpp
    src/telemetry/telemetry_out.cpp
    src/telemetry/fm4_car.cpp
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
    target_link_libraries(fm4 PRIVATE dbghelp bcrypt ws2_32)
endif()
add_executable(fm4tool tools/fm4tool/main.cpp)
target_link_libraries(fm4tool PRIVATE rex::runtime rex::kernel rex::system)
add_custom_command(TARGET fm4 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config/fm4.toml $<TARGET_FILE_DIR:fm4>)
add_custom_command(TARGET fm4tool POST_BUILD
    COMMAND "$<$<BOOL:$<TARGET_RUNTIME_DLLS:fm4tool>>:${CMAKE_COMMAND};-E;copy_if_different;$<TARGET_RUNTIME_DLLS:fm4tool>;$<TARGET_FILE_DIR:fm4tool>>"
    COMMAND_EXPAND_LISTS VERBATIM)


# FM4 launcher, adapted from FM3; dependencies are vendored for offline builds.
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui)
find_package(SDL3 REQUIRED CONFIG)
add_executable(fm4-wheel-probe tools/wheel_probe.cpp src/wheel.cpp src/wheel_mapping.cpp)
target_link_libraries(fm4-wheel-probe PRIVATE SDL3::SDL3-static)
add_executable(fm4-launcher WIN32
    launcher/src/main.cpp
    launcher/src/graphics_fixes.cpp
    launcher/src/installer.cpp
    launcher/src/settings.cpp
    launcher/src/sha256.cpp
    src/wheel.cpp
    src/wheel_mapping.cpp
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
target_link_libraries(fm4-launcher PRIVATE SDL3::SDL3-static dbghelp)
target_compile_options(fm4-launcher PRIVATE -gline-tables-only)
target_link_options(fm4-launcher PRIVATE -g)
set_target_properties(fm4-launcher PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
add_dependencies(fm4-launcher fm4tool)
add_custom_command(TARGET fm4-launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/launcher/data/game_files.sha256
        ${CMAKE_CURRENT_SOURCE_DIR}/config/fm4.toml
        $<TARGET_FILE_DIR:fm4-launcher>)
configure_file(scripts/install-forza-plus.ps1 ${CMAKE_CURRENT_BINARY_DIR}/install-forza-plus.ps1 COPYONLY)
configure_file(scripts/uninstall-forza-plus.ps1 ${CMAKE_CURRENT_BINARY_DIR}/uninstall-forza-plus.ps1 COPYONLY)

option(FM4_BUILD_TESTS "Build launcher compatibility regression checks" OFF)
if(FM4_BUILD_TESTS)
    enable_testing()
    add_executable(fm4-telemetry-tests tests/telemetry_dash_tests.cpp src/telemetry/forza_dash.cpp)
    add_test(NAME telemetry-dash COMMAND fm4-telemetry-tests)
    add_executable(fm4-launcher-tests tests/graphics_fixes_tests.cpp
        launcher/src/graphics_fixes.cpp launcher/src/settings.cpp src/wheel_mapping.cpp)
    target_include_directories(fm4-launcher-tests PRIVATE launcher/src
        thirdparty/tomlplusplus/include)
    set_target_properties(fm4-launcher-tests PROPERTIES CXX_STANDARD 17)
    add_test(NAME launcher-compatibility COMMAND fm4-launcher-tests)
    add_executable(fm4-wheel-tests tests/wheel_tests.cpp src/wheel_mapping.cpp launcher/src/settings.cpp)
    target_include_directories(fm4-wheel-tests PRIVATE thirdparty/tomlplusplus/include)
    target_link_libraries(fm4-wheel-tests PRIVATE SDL3::SDL3-static)
    set_target_properties(fm4-wheel-tests PROPERTIES CXX_STANDARD 17)
    add_test(NAME wheel-input COMMAND fm4-wheel-tests)
    add_executable(fm4-wheel-ffb-tests tests/wheel_ffb_tests.cpp src/wheel_ffb.cpp src/wheel_mapping.cpp)
    target_include_directories(fm4-wheel-ffb-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(fm4-wheel-ffb-tests PRIVATE rex::runtime)
    add_test(NAME wheel-native-ffb COMMAND fm4-wheel-ffb-tests)
    add_executable(fm4-wheel-runtime-tests tests/wheel_runtime_tests.cpp src/wheel.cpp src/wheel_mapping.cpp src/wheel_input.cpp)
    target_link_libraries(fm4-wheel-runtime-tests PRIVATE rex::runtime)
    add_test(NAME wheel-runtime COMMAND fm4-wheel-runtime-tests)
    add_executable(fm4-wheel-haptic-backend-tests tests/wheel_haptic_backend_tests.cpp)
    target_link_libraries(fm4-wheel-haptic-backend-tests PRIVATE SDL3::SDL3-static)
    add_test(NAME wheel-haptic-backend COMMAND fm4-wheel-haptic-backend-tests)
    add_executable(fm4-wheel-ui-tests tests/wheel_ui_tests.cpp src/wheel.cpp src/wheel_mapping.cpp)
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
