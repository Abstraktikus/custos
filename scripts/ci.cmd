@echo off
rem Full clean-ish CI run: configure + build (custos_tests, Custos) + test.
rem Uses cmd errorlevel chaining so the real exit code propagates (PS stream
rem redirection would corrupt it). Optional arg %1 = synth path for the plugin build.
setlocal
call "%~dp0_vsenv.cmd"
set "DEPS=-DFETCHCONTENT_SOURCE_DIR_JUCE=C:/dev/_deps/JUCE -DFETCHCONTENT_SOURCE_DIR_CATCH2=C:/dev/_deps/Catch2"
echo === configure ===
if "%~1"=="" (
  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug %DEPS%
) else (
  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug %DEPS% -DCUSTOS_HARDCODED_SYNTH_PATH=%1
)
if errorlevel 1 exit /b 1
echo === build ===
cmake --build build --target custos_tests Custos_VST3
if errorlevel 1 exit /b 2
echo === test ===
ctest --test-dir build --output-on-failure
if errorlevel 1 exit /b 3
echo === ALL_DONE ===
