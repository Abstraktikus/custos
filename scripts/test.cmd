@echo off
rem Run the test suite via ctest (Ninja single-config, no -C needed).
setlocal
call "%~dp0_vsenv.cmd"
ctest --test-dir build --output-on-failure
