#!/bin/sh

CURRENT_DIR=`pwd`
BUILD_DIR="build_xcode"
BUILD_OPTIONS="-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"

mkdir -p ../${BUILD_DIR}
cd ../${BUILD_DIR}
eval cmake .. -G"Xcode" ${BUILD_OPTIONS}
cd ${CURRENT_DIR}
