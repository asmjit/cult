#include "./cpuinfo.h"

namespace cult {

inline void callCpuid(CpuidOut* result, uint32_t inEax, uint32_t inEcx = 0) noexcept {
  #if ASMJIT_CC_MSC
    __cpuidex(reinterpret_cast<int*>(result), inEax, inEcx);
  #elif ASMJIT_CC_MSC && ASMJIT_ARCH_X86
    uint32_t paramEax = inEax;
    uint32_t paramEcx = inEcx;
    uint32_t* out = &result->eax;

    __asm {
      mov     eax, paramEax
      mov     ecx, paramEcx
      mov     edi, out
      cpuid
      mov     dword ptr[edi +  0], eax
      mov     dword ptr[edi +  4], ebx
      mov     dword ptr[edi +  8], ecx
      mov     dword ptr[edi + 12], edx
    }
  #elif (ASMJIT_CC_GCC || ASMJIT_CC_CLANG) && ASMJIT_ARCH_X86
    __asm__ __volatile__(
      "mov %%ebx, %%edi\n"
      "cpuid\n"
      "xchg %%edi, %%ebx\n"
        : "=a"(result->eax),
          "=D"(result->ebx),
          "=c"(result->ecx),
          "=d"(result->edx)
        : "a"(inEax),
          "c"(inEcx)
    );
  #elif (ASMJIT_CC_GCC || ASMJIT_CC_CLANG || ASMJIT_CC_INTEL) && ASMJIT_ARCH_X64
    __asm__ __volatile__(
      "mov %%rbx, %%rdi\n"
      "cpuid\n"
      "xchg %%rdi, %%rbx\n"
        : "=a"(result->eax),
          "=D"(result->ebx),
          "=c"(result->ecx),
          "=d"(result->edx)
        : "a"(inEax),
          "c"(inEcx)
    );
  #else
    #error "CpuidOut - Unsupported compiler"
  #endif
}

CpuInfo::CpuInfo(App* app) noexcept
  : _app(app) {}

CpuInfo::~CpuInfo() noexcept {
}

void CpuInfo::run() {
  JSONBuilder& json = _app->json();

  if (_app->verbose())
    printf("CPUID:\n");

  json.beforeRecord()
      .addKey("cpuid")
      .openArray();

  CpuidIn in = { 0 };
  CpuidOut out = { 0 };

  uint32_t maxEax = 0;
  callCpuid(&out, in.eax);

  for (;;) {
    uint32_t maxEcx = 0;

    switch (in.eax) {
      case 0x00U:
        maxEax = out.eax;
        break;

      case 0x04U:
        // Deterministic Cache Parameters.
        maxEcx = 63;
        break;

      case 0x07U:
        // Structured Extended Feature Flags Enumeration.
        maxEcx = out.eax;
        break;

      case 0x0BU:
        // Extended Topology Enumeration. Scan all possibilities.
        maxEcx = 255;
        for (;;) {
          // Bits 7-0 in `out.ecx` are always the same as bits 7-0 in `in.ecx`.
          // If there are no other bits set we consider that output invalid.
          if (out.eax || out.ebx || (out.ecx & 0xFFFFFF00U) || in.eax == 0)
            onCpuInfo(in, out);

          if (in.ecx == maxEcx)
            break;
          callCpuid(&out, in.eax, ++in.ecx);
        }

        maxEcx = 0xFFFFFFFFU;
        break;

      case 0x0DU:
        // Processor Extended State Enumeration. We set ECX to a possible
        // maximum value, and discard results from invalid calls.
        maxEcx = 63;
        break;

      case 0x0FU:
        // Currently it seems only defined one is for ECX 0 or 1, but the
        // manual says to check bits in EDX, so we set `maxEcx` to maximum
        // possible value in the future.
        maxEcx = 31;
        break;

      case 0x10U:
        // L3 Cache QoS Enforcement Enumeration.
        maxEcx = 63;
        break;

      case 0x14U:
        // Intel Processor Trace Enumeration
        maxEcx = out.eax;
        break;
    }

    if (maxEcx != 0xFFFFFFFFU) {
      if (maxEcx > 0) {
        for (;;) {
          // Sometimes we just use max possible value and iterate all, if CPU
          // reports invalid call it's fine, we just continue until we reach
          // the limit.
          if (out.isValid())
            onCpuInfo(in, out);

          if (in.ecx == maxEcx)
            break;
          callCpuid(&out, in.eax, ++in.ecx);
        }
      }
      else {
        onCpuInfo(in, out);
      }
    }

    in.ecx = 0;
    if (in.eax == maxEax)
      break;

    // Don't ask for CPU serial number.
    if (++in.eax == 3)
      ::memset(&out, 0, sizeof(out));
    else
      callCpuid(&out, in.eax, 0);
  }

  in.eax = 0x80000000U;
  in.ecx = 0;

  maxEax = in.eax;
  callCpuid(&out, in.eax);

  for (;;) {
    switch (in.eax) {
      case 0x80000000U: {
        if (out.eax > 0x80000000U)
          maxEax = out.eax;
        break;
      }
    }

    onCpuInfo(in, out);

    if (in.eax == maxEax)
      break;
    callCpuid(&out, ++in.eax, 0);
  }

  if (_app->verbose())
    printf("\n");

  json.closeArray();
}

void CpuInfo::onCpuInfo(const CpuidIn& in, const CpuidOut& out) {
  if (_app->verbose())
    printf("  In:%08X Sub:%08X -> EAX:%08X EBX:%08X ECX:%08X EDX:%08X\n", in.eax, in.ecx, out.eax, out.ebx, out.ecx, out.edx);

  JSONBuilder& json = _app->json();
  json.beforeRecord()
      .openObject()
      .addKey("level").addUIntHex(in.eax, 8)
      .addKey("subleaf").addUIntHex(in.ecx, 8)
      .addKey("eax").addUIntHex(out.eax, 8)
      .addKey("ebx").addUIntHex(out.ebx, 8)
      .addKey("ecx").addUIntHex(out.ecx, 8)
      .addKey("edx").addUIntHex(out.edx, 8)
      .closeObject();
}

} // cult namespace
