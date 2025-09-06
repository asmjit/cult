#ifndef _CULT_CPUUTILS_H
#define _CULT_CPUUTILS_H

#include "globals.h"

namespace cult {
namespace CpuUtils {

struct CpuidIn {
  uint32_t eax, ecx;
};

struct CpuidOut {
  // CPU returns all zeros if the CPUID call was invalid.
  inline bool is_valid() const { return (eax | ebx | ecx | edx) != 0; }

  uint32_t eax, ebx, ecx, edx;
};

struct CpuidEntry {
  CpuidIn in;
  CpuidOut out;
};

void cpuid_query(CpuidOut* result, uint32_t in_eax, uint32_t in_ecx = 0);

uint64_t get_tsc_freq();
uint64_t get_tsc_freq_always_calibrated();

} // CpuUtils namespace
} // {cult} namespace

#endif // _CULT_CPUUTILS_H
