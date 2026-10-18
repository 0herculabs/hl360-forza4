@echo off
setlocal
if "%~1"=="" (
  echo Usage: scripts\build-wheel-sdl.cmd path-to-SDL3-source
  exit /b 1
)
set "FM4_SDL_SOURCE=%~f1"
set "FM4_ROOT=%~dp0.."
call "%~dp0devenv.cmd"
if errorlevel 1 exit /b 1
cmake -S "%FM4_SDL_SOURCE%" -B "%FM4_ROOT%\out\build\sdl3-haptic" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DSDL_STATIC=ON -DSDL_SHARED=OFF -DSDL_HAPTIC=ON -DSDL_JOYSTICK=ON -DSDL_DIRECTX=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DCMAKE_INSTALL_PREFIX="%FM4_ROOT%\out\install\sdl3-haptic"
if errorlevel 1 exit /b 1
cmake --build "%FM4_ROOT%\out\build\sdl3-haptic" --target install
if errorlevel 1 exit /b 1
echo SDL with haptic support installed. Rebuild FM4 and run the wheel-haptic-backend test.
