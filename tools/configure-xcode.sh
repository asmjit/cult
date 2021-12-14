#!/bin/sh
BUILD_OPTIONS="-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
eval cmake .. -B ../build_xcode -G"Xcode" ${BUILD_OPTIONS}
