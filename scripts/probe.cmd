@echo off
setlocal
call "%~dp0_vsenv.cmd"
echo === cmake ===
cmake --version
echo === ninja ===
ninja --version
echo === cl ===
where cl
