@echo off
rem Build Custos and deploy the VST3 bundle to the user's Gig Performer VST3 folder.
rem   scripts\deploy.cmd                         :: silent passthrough build
rem   scripts\deploy.cmd "C:/path/Synth.vst3"    :: hard-code an inner synth for E2E
rem Override the destination with the CUSTOS_DEPLOY_DIR environment variable.
rem NOTE: if Gig Performer has Custos loaded, the target may be locked — close it first.
setlocal
call "%~dp0_vsenv.cmd"
if not defined CUSTOS_DEPLOY_DIR set "CUSTOS_DEPLOY_DIR=C:\Users\marti\OneDrive\Keyboard\GigPerformer\Kapellmeister\Custos"
set "DEPS=-DFETCHCONTENT_SOURCE_DIR_JUCE=C:/dev/_deps/JUCE -DFETCHCONTENT_SOURCE_DIR_CATCH2=C:/dev/_deps/Catch2"

if "%~1"=="" (
  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug %DEPS%
) else (
  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug %DEPS% -DCUSTOS_HARDCODED_SYNTH_PATH=%1
)
if errorlevel 1 exit /b 1
cmake --build build --target Custos
if errorlevel 1 exit /b 2

rem Locate the built VST3 bundle (a directory) under build\.
set "VST3="
for /f "delims=" %%D in ('dir /b /s /a:d "build\Custos_artefacts\Custos.vst3" 2^>nul') do set "VST3=%%D"
if not defined VST3 ( echo ERROR: Custos.vst3 not found under build\ & exit /b 3 )

echo Deploying "%VST3%"
echo        -> "%CUSTOS_DEPLOY_DIR%\Custos.vst3"
if not exist "%CUSTOS_DEPLOY_DIR%" mkdir "%CUSTOS_DEPLOY_DIR%"
robocopy "%VST3%" "%CUSTOS_DEPLOY_DIR%\Custos.vst3" /MIR /NJH /NJS /NDL /NP /R:2 /W:1 >nul
if errorlevel 8 ( echo ERROR: robocopy failed & exit /b 4 )
echo done.
exit /b 0
