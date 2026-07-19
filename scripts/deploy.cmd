@echo off
rem Build every Custos facade-ladder variant and deploy each VST3 bundle to Gig Performer's scan folder.
rem   scripts\deploy.cmd                         :: silent passthrough build
rem   scripts\deploy.cmd "C:/path/Synth.vst3"    :: hard-code an inner synth for E2E
rem Override the destination with the CUSTOS_DEPLOY_DIR environment variable.
rem NOTE: if Gig Performer has a Custos loaded, the target may be locked — close it first.
setlocal enabledelayedexpansion
call "%~dp0_vsenv.cmd"
if not defined CUSTOS_DEPLOY_DIR set "CUSTOS_DEPLOY_DIR=C:\Users\marti\OneDrive\Keyboard\GigPerformer\Kapellmeister\Custos"
set "DEPS=-DFETCHCONTENT_SOURCE_DIR_JUCE=C:/dev/_deps/JUCE -DFETCHCONTENT_SOURCE_DIR_CATCH2=C:/dev/_deps/Catch2"
rem The facade ladder (CMakeLists.txt custos_variant): one VST3 per rung, product name "Custos <SIZE>".
set "SIZES=1000 2000 3000 4000 5000 10000"

rem Build the synth-path arg in a variable (handles spaces; avoids cmd paren-block parsing).
rem Clear the cached synth path when none is given (silent-passthrough deploy).
set SYNTHARG=-DCUSTOS_HARDCODED_SYNTH_PATH=
if not "%~1"=="" set SYNTHARG=-DCUSTOS_HARDCODED_SYNTH_PATH="%~1"
rem Release into its own tree: the rig gets optimized builds (a Debug ladder shipped ucrtbased.dll
rem into GP and slowed the big facades — 2026-07-19 findings), while build\ stays the Debug/test tree.
cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release %DEPS% %SYNTHARG%
if errorlevel 1 exit /b 1

set "TARGETS="
for %%S in (%SIZES%) do set "TARGETS=!TARGETS! Custos%%S_VST3"
cmake --build build-release --target !TARGETS!
if errorlevel 1 exit /b 2

if not exist "%CUSTOS_DEPLOY_DIR%" mkdir "%CUSTOS_DEPLOY_DIR%"
for %%S in (%SIZES%) do (
    rem Locate the built VST3 bundle (a directory) under build\ for this rung.
    set "BUNDLE="
    for /f "delims=" %%D in ('dir /b /s /a:d "build-release\Custos%%S_artefacts\Custos %%S.vst3" 2^>nul') do set "BUNDLE=%%D"
    if not defined BUNDLE ( echo ERROR: "Custos %%S.vst3" not found under build-release\ & exit /b 3 )
    echo Deploying "Custos %%S.vst3"  -^>  "%CUSTOS_DEPLOY_DIR%\Custos %%S.vst3"
    robocopy "!BUNDLE!" "%CUSTOS_DEPLOY_DIR%\Custos %%S.vst3" /MIR /XF *.ilk *.pdb *.exp /NJH /NJS /NDL /NP /R:2 /W:1 >nul
    if errorlevel 8 ( echo ERROR: robocopy failed for "Custos %%S.vst3" & exit /b 4 )
)
echo done.
exit /b 0
