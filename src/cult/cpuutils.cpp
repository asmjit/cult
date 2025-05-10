#include "cpudetect.h"

#ifdef _MSC_VER
  #include <intrin.h>
#else
  #include <x86intrin.h>
#endif

#if defined(__linux__)
  #include <time.h>
#endif

#include <algorithm>
#include <numeric>

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
static uint64_t get_tsc_freq_via_cpuid() {
  CpuidOut out;
  cpuid_query(&out, 0x0u);

  if (out.eax < 0x15u)
    return 0;

  // Determine the base frequency by CPUID.0x15 query.
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

// Copy of the calibration code from tsc-support.cpp (https://github.com/travisdowns/avx-turbo).
#if defined(__linux__)
static inline uint64_t get_clock_monotonic() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static uint64_t tsc_calibration_sample() {
  constexpr uint64_t kDelayNS = 20000;

  _mm_lfence();
  uint64_t  nsbefore = get_clock_monotonic();
  uint64_t tscbefore = __rdtsc();

  while (nsbefore + kDelayNS > get_clock_monotonic()) {
    continue;
  }

  uint64_t  nsafter = get_clock_monotonic();
  uint64_t tscafter = __rdtsc();

  return (tscafter - tscbefore) * 1000000000u / (nsafter - nsbefore);
}

static uint64_t get_tsc_freq_via_calibration() {
  constexpr size_t kSampleCount = 101;

  uint64_t samples[kSampleCount];
  volatile uint64_t val = 0;

  // Warmup.
  for (size_t i = 0; i < kSampleCount; i++) {
    val = tsc_calibration_sample();
  }

  // Collect samples and sort them out.
  for (size_t i = 0; i < kSampleCount; i++) {
    samples[i] = tsc_calibration_sample();
  }
  std::sort(samples, samples + kSampleCount);

  // average the middle quintile
  constexpr size_t kThirdQuantile = 2u * kSampleCount / 5u;
  constexpr size_t kSampleCountDiv5 = kSampleCount / 5u;

  uint64_t sum = std::accumulate(&samples[kThirdQuantile], &samples[kThirdQuantile + kSampleCountDiv5], uint64_t(0));
  return sum / kSampleCountDiv5;
}
#endif

uint64_t get_tsc_freq_always_calibrated() {
#if defined(__linux__)
  return get_tsc_freq_via_calibration();
#else
  return 0;
#endif
}

uint64_t get_tsc_freq() {
  uint64_t freq = get_tsc_freq_via_cpuid();

  if (freq) {
    return freq;
  }
  else {
    return get_tsc_freq_always_calibrated();
  }
}

} // CpuUtils namespace
} // {cult} namespace
