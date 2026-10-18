@echo off
setlocal
if "%~1"=="" (
  echo Usage: install_game.cmd "Disc 1.iso" ["Disc 2.iso"]
  exit /b 1
)
set "PLAY_ISO=%~f1"
set "CONTENT_ISO=%~f2"
cd /d "%~dp0.."
set "FM4_TOOL=%CD%\out\build\win-amd64-release\fm4tool.exe"
if not exist "%FM4_TOOL%" (
  echo Build fm4tool first. See README.md.
  exit /b 1
)
"%FM4_TOOL%" iso "%PLAY_ISO%" "%CD%\game\disc1" || exit /b 1
if "%CONTENT_ISO%"=="" exit /b 0
"%FM4_TOOL%" iso "%CONTENT_ISO%" "%CD%\game\disc2" || exit /b 1
"%FM4_TOOL%" install-dlc "%CD%\game\disc1" "%CD%\game\disc2" "%CD%\user"
exit /b %ERRORLEVEL%
