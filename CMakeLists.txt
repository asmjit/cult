cmake_minimum_required(VERSION 3.8)
project(cult CXX)

if (NOT DEFINED CULT_DIR)
  set(CULT_DIR "${CMAKE_CURRENT_LIST_DIR}")
endif()

if (NOT DEFINED ASMJIT_DIR)
  set(ASMJIT_DIR "${CULT_DIR}/../asmjit")
endif()

set(CULT_DIR   "${CULT_DIR}"   CACHE PATH "Location of 'cult'")
set(ASMJIT_DIR "${ASMJIT_DIR}" CACHE PATH "Location of 'asmjit'")

if(NOT DEFINED ASMJIT_STATIC)
  set(ASMJIT_STATIC TRUE)
endif()

include("${ASMJIT_DIR}/CMakeLists.txt")

set(CULT_SRC
  src/cult/app.cpp
  src/cult/app.h
  src/cult/basebench.cpp
  src/cult/basebench.h
  src/cult/cpudetect.cpp
  src/cult/cpudetect.h
  src/cult/cpuutils.cpp
  src/cult/cpuutils.h
  src/cult/globals.h
  src/cult/instbench.cpp
  src/cult/instbench.h
  src/cult/jsonbuilder.cpp
  src/cult/jsonbuilder.h
  src/cult/schedutils.cpp
  src/cult/schedutils.h
)

add_executable(cult ${CULT_SRC})
target_link_libraries(cult AsmJit::AsmJit)
target_compile_features(cult PUBLIC cxx_std_11)
set_property(TARGET cult PROPERTY CXX_VISIBILITY_PRESET hidden)

if (WIN32)
  target_compile_definitions(cult PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
