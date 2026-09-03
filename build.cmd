@echo off
rem Build RiftLoop with the VS 2022 toolchain.
rem Usage: build.cmd [debug|release|test]
setlocal
set MODE=%1
if "%MODE%"=="" set MODE=debug

call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
set PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%

if "%MODE%"=="release" (
  cmake --preset x64-release && cmake --build --preset x64-release
) else if "%MODE%"=="test" (
  cmake --preset x64-debug && cmake --build --preset x64-debug && ctest --preset x64-debug
) else (
  cmake --preset x64-debug && cmake --build --preset x64-debug
)
endlocal
