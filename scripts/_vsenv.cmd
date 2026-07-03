@echo off
rem Shared build environment for Custos (Windows / VS Build Tools 2022).
rem Sets up the MSVC toolchain via vcvars64 and puts the bundled CMake + Ninja on PATH.
rem No setlocal here on purpose: env changes must persist into the calling script.
set "VSBT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VSBT%\VC\Auxiliary\Build\vcvars64.bat" >nul
set "PATH=%VSBT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSBT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
