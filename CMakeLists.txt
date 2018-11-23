cmake_minimum_required(VERSION 3.1)

project(cult C CXX)

set(CULT_DIR   "${CMAKE_CURRENT_LIST_DIR}" CACHE PATH "CULT directory")
set(ASMJIT_DIR "${CULT_DIR}/../asmjit"     CACHE PATH "Location of 'asmjit'")

if(NOT DEFINED ASMJIT_BUILD_EMBED)
  set(ASMJIT_BUILD_EMBED TRUE)
endif()

# =============================================================================
# [CULT - Project]
# =============================================================================

include("${CULT_DIR}/CxxProject.cmake")
cxx_project(cult)
cxx_detect_standard(CULT_PRIVATE_CFLAGS)

include("${ASMJIT_DIR}/CMakeLists.txt")
list(APPEND CULT_DEPS ${ASMJIT_LIBS})
list(APPEND CULT_PRIVATE_CFLAGS ${ASMJIT_CFLAGS})

list(REMOVE_DUPLICATES CULT_DEPS)
list(REMOVE_DUPLICATES CULT_PRIVATE_CFLAGS)

cxx_project_info(cult)

# =============================================================================
# [CULT - Source]
# =============================================================================

set(CULT_SRC "")

cxx_add_source(cult CULT_SRC cult
  app.cpp
  app.h
  basebench.cpp
  basebench.h
  cpudetect.cpp
  cpudetect.h
  globals.h
  instbench.cpp
  instbench.h
  jsonbuilder.cpp
  jsonbuilder.h
  schedutils.cpp
  schedutils.h
)

# =============================================================================
# [CULT - Targets]
# =============================================================================

add_executable(cult ${CULT_SRC} ${ASMJIT_SRC})
target_link_libraries(cult ${ASMJIT_DEPS})
target_include_directories(cult BEFORE PRIVATE ${ASMJIT_INCLUDE_DIR})
set_target_properties(cult PROPERTIES LINK_FLAGS "${ASMJIT_PRIVATE_LFLAGS}")

if(CMAKE_BUILD_TYPE)
  if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    target_compile_options(cult PRIVATE ${CULT_PRIVATE_CFLAGS} ${CULT_PRIVATE_CFLAGS_DBG})
  else()
    target_compile_options(cult PRIVATE ${CULT_PRIVATE_CFLAGS} ${CULT_PRIVATE_CFLAGS_REL})
  endif()
else()
  target_compile_options(cult PRIVATE ${CULT_PRIVATE_CFLAGS}
    $<$<CONFIG:Debug>:${CULT_PRIVATE_CFLAGS_DBG}>
    $<$<NOT:$<CONFIG:Debug>>:${CULT_PRIVATE_CFLAGS_REL}>)
endif()
