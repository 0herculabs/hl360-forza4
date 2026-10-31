@echo off
setlocal
set "CFG=%~1"
if "%CFG%"=="" set "CFG=release"
call "%~dp0devenv.cmd"
cd /d "%~dp0.."
rem ShiftGlue (Vulkan renderer) is preferred; thirdparty\sdk holds upstream ReXGlue.
if not defined REXSDK_ROOT if exist "%CD%\thirdparty\sdk-shiftglue\bin\rexglue.exe" set "REXSDK_ROOT=%CD%\thirdparty\sdk-shiftglue"
if not defined REXSDK_ROOT set "REXSDK_ROOT=%CD%\thirdparty\sdk"
if not exist "%REXSDK_ROOT%\bin\rexglue.exe" (
  echo Set REXSDK_ROOT to a built ReXGlue SDK installation.
  exit /b 1
)
if not exist generated\default\sources.cmake (
  "%REXSDK_ROOT%\bin\rexglue.exe" codegen fm4_manifest.toml || exit /b 1
)
cmake --preset win-amd64-%CFG% -DCMAKE_PREFIX_PATH="%REXSDK_ROOT%" || exit /b 1
cmake --build --preset win-amd64-%CFG% || exit /b 1
