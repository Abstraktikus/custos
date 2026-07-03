@echo off
rem Build the CMake project. Optional args = target names.
rem   scripts\build.cmd                (build everything)
rem   scripts\build.cmd custos_tests   (build one target)
rem   scripts\build.cmd custos_tests Custos
setlocal
call "%~dp0_vsenv.cmd"
if "%~1"=="" (
  cmake --build build
) else (
  cmake --build build --target %*
)
