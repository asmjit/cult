@echo off

set CURRENT_DIR=%CD%
set BUILD_DIR="build_vs2015_x64"
set ASMJIT_DIR="../../asmjit"

mkdir ..\%BUILD_DIR%
cd ..\%BUILD_DIR%
cmake .. -G"Visual Studio 15 Win64" -D"ASMJIT_DIR=%ASMJIT_DIR%"
cd %CURRENT_DIR%
