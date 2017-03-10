#ifndef _CULT_CPUINFO_H
#define _CULT_CPUINFO_H

#include "./app.h"
#include "./jsonbuilder.h"

namespace cult {

struct CpuidIn {
  uint32_t eax, ecx;
};

struct CpuidOut {
  // CPU returns all zeros if the CPUID call was invalid.
  inline bool isValid() const noexcept { return (eax | ebx | ecx | edx) != 0; }

  uint32_t eax, ebx, ecx, edx;
};

struct CpuidRecord {
  CpuidIn in;
  CpuidOut out;
};

class CpuInfo {
public:
  CpuInfo(App* app) noexcept;
  ~CpuInfo() noexcept;

  void run();
  void onCpuInfo(const CpuidIn& in, const CpuidOut& out);

  App* _app;
};

} // cult namespace

#endif // _CULT_CPUINFO_H
