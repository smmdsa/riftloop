@echo off
rem Build only the targets named on the command line, debug.
rem Usage: buildt.cmd RiftLoop.Analyzer riftloop_tests
rem The Desktop, Agent and Overlay executables stay locked while the user runs
rem them, and a full build then fails with LNK1168. This script skips them.
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
set PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
cmake --preset x64-debug >nul && cmake --build build\x64-debug --target %*
endlocal
