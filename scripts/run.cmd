@echo off
setlocal
cd /d "%~dp0.."
set "FM4_EXE=%CD%\out\build\win-amd64-release\fm4.exe"
if not exist "%FM4_EXE%" (
  echo Build the project first with scripts\build.cmd release.
  exit /b 1
)
"%FM4_EXE%" --game_data_root "%CD%\game\disc1" --user_data_root "%CD%\user" %*
