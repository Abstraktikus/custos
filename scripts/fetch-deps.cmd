@echo off
rem One-time dependency cache: clone the pinned deps into C:\dev\_deps so CMake's
rem FetchContent uses them directly instead of a per-build subbuild clone (which is
rem slow and races on Ninja recompaction). Safe to re-run (skips existing clones).
if not exist C:\dev\_deps mkdir C:\dev\_deps
if not exist C:\dev\_deps\JUCE   git clone --depth 1 --branch 8.0.14 https://github.com/juce-framework/JUCE.git C:\dev\_deps\JUCE
if not exist C:\dev\_deps\Catch2 git clone --depth 1 --branch v3.15.1 https://github.com/catchorg/Catch2.git C:\dev\_deps\Catch2
echo deps cache ready in C:\dev\_deps
