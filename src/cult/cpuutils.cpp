#include "cpudetect.h"

#ifdef _MSC_VER
  #include <intrin.h>
#else
  #include <x86intrin.h>
#endif

namespace cult {
namespace CpuUtils {

void cpuid_query(CpuidOut* result, uint32_t inEax, uint32_t inEcx) {
#if defined(_MSC_VER)
  __cpuidex(reinterpret_cast<int*>(result), inEax, inEcx);
#elif ASMJIT_ARCH_X86 == 32 && defined(__GNUC__)
  __asm__ __volatile__(
    "mov %%ebx, %%edi\n"
    "cpuid\n"
    "xchg %%edi, %%ebx\n"
      : "=a"(result->eax), "=D"(result->ebx), "=c"(result->ecx), "=d"(result->edx)
      : "a"(inEax), "c"(inEcx));
#elif ASMJIT_ARCH_X86 == 64 && defined(__GNUC__)
  __asm__ __volatile__(
    "mov %%rbx, %%rdi\n"
    "cpuid\n"
    "xchg %%rdi, %%rbx\n"
      : "=a"(result->eax), "=D"(result->ebx), "=c"(result->ecx), "=d"(result->edx)
      : "a"(inEax), "c"(inEcx));
#else
  #error "[cult] cpuid_query() - Unsupported compiler"
#endif
}

// Heavily inspired in avx-turbo's tsc-support.cpp (https://github.com/travisdowns/avx-turbo).
uint64_t get_tsc_freq() {
  CpuidOut out;
  cpuid_query(&out, 0x0u);

  if (out.eax < 0x15u)
    return 0;

  // Determine the base frequency by CPUID.15 query.
  CpuidOut _15;
  cpuid_query(&_15, 0x15u);
  if (_15.ecx) {
    if (_15.eax == 0)
      return 0;
    return uint64_t(_15.ecx) * _15.ebx / _15.eax;
  }

  // Determine the base frequency by CPU family & model ids.
  // (Intel Manual Volume 3 - Determining the Processor Base Frequency, Section 18.18.3)
  cpuid_query(&out, 0x1u);

  uint32_t family = (out.eax >> 8) & 0xF;
  uint32_t model = (out.eax >> 8) & 0xF;

  if (family == 15)
    family += (out.eax >> 20) & 0xFF;

  if (family == 15 || family == 6)
    model += ((out.eax >> 16) & 0xF) << 4;

  // 24 MHz crystal clock (Skylake or Kabylake).
  if (family == 6 && (model == 0x4E || model == 0x5E || model == 0x8E || model == 0x9E))
    return (int64_t)24000000 * _15.ebx / _15.eax;

  return 0;
}

} // CpuUtils namespace
} // cult namespace
