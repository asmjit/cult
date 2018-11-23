#ifndef _CULT_CPUDETECT_H
#define _CULT_CPUDETECT_H

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

struct CpuidEntry {
  CpuidIn in;
  CpuidOut out;
};

class CpuDetect {
public:
  CpuDetect(App* app) noexcept;
  ~CpuDetect() noexcept;

  void run() noexcept;

  void _queryCpuData() noexcept;
  void _queryCpuInfo() noexcept;

  void addEntry(const CpuidIn& in, const CpuidOut& out) noexcept;
  CpuidOut entryOf(uint32_t eax, uint32_t ecx = 0) noexcept;

  App* _app;
  ZoneVector<CpuidEntry> _entries;

  uint32_t _modelId;
  uint32_t _familyId;
  uint32_t _steppingId;

  char _vendorString[16];
  char _vendorName[16];
  char _brandString[64];
  char _archCodename[32];
};

} // cult namespace

#endif // _CULT_CPUDETECT_H
