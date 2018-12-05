#ifndef _CULT_GLOBALS_H
#define _CULT_GLOBALS_H

#define CULT_VERSION_MAJOR 0
#define CULT_VERSION_MINOR 0
#define CULT_VERSION_MICRO 1

#if defined(_WIN32)
  // Windows.h will be included by asmjit...
#else
  // As it seems that `pthread_setaffinity_np` on Linux is only available
  // as a GNU extension so defining `_GNU_SOURCE` seems to be the way.
  #if !defined(_GNU_SOURCE)
    #define _GNU_SOURCE
  #endif
  #include <pthread.h>
#endif

// [Dependencies]
#include <asmjit/x86.h>

#include <assert.h>
#include <stdint.h>
#include <algorithm>

namespace cult {
using namespace asmjit;
} // cult namespace

#endif // _CULT_GLOBALS_H
