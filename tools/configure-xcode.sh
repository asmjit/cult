#!/bin/sh

CURRENT_DIR=`pwd`
BUILD_DIR="build_xcode"
ASMJIT_DIR="../../asmjit"

mkdir -p ../${BUILD_DIR}
cd ../${BUILD_DIR}
cmake .. -G"Xcode" -D"ASMJIT_DIR=${ASMJIT_DIR}"
cd ${CURRENT_DIR}
