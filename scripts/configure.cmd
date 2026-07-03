@echo off
rem Configure the CMake build (Ninja single-config, Debug).
rem Optional arg %1 = absolute path to a VST3 synth for CUSTOS_HARDCODED_SYNTH_PATH.
rem   scripts\configure.cmd
rem   scripts\configure.cmd "C:/Program Files/Common Files/VST3/Surge XT.vst3"
setlocal
call "%~dp0_vsenv.cmd"
if "%~1"=="" (
  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
) else (
  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCUSTOS_HARDCODED_SYNTH_PATH=%1
)
