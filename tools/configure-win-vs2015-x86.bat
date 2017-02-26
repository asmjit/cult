@echo off

set CURRENT_DIR=%CD%
set BUILD_DIR="build_vs2015_x86"
set ASMJIT_DIR="../../asmjit"

mkdir ..\%BUILD_DIR%
cd ..\%BUILD_DIR%
cmake .. -G"Visual Studio 14" -DASMJIT_DIR="%ASMJIT_DIR%"
cd %CURRENT_DIR%
