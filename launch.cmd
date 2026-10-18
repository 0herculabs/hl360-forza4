@echo off
setlocal
set "FM4_LAUNCHER=%~dp0out\build\win-amd64-release\fm4-launcher.exe"
if not exist "%FM4_LAUNCHER%" (
  echo Primero compila el proyecto con scripts\build.cmd release.
  pause
  exit /b 1
)
start "" "%FM4_LAUNCHER%"
