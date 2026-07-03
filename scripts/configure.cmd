@echo off
rem Configure the CMake build (Ninja single-config, Debug).
rem Optional arg %1 = absolute path to a VST3 synth for CUSTOS_HARDCODED_SYNTH_PATH.
rem   scripts\configure.cmd
rem   scripts\configure.cmd "C:/Program Files/Common Files/VST3/Surge XT.vst3"
setlocal
call "%~dp0_vsenv.cmd"
rem Point FetchContent at pre-cloned deps (avoids the subbuild clone + its Ninja
rem recompaction races, and makes rebuilds instant). Populate with scripts\fetch-deps.cmd.
set "DEPS=-DFETCHCONTENT_SOURCE_DIR_JUCE=C:/dev/_deps/JUCE -DFETCHCONTENT_SOURCE_DIR_CATCH2=C:/dev/_deps/Catch2"
if "%~1"=="" (
  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug %DEPS%
) else (
  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug %DEPS% -DCUSTOS_HARDCODED_SYNTH_PATH=%1
)
