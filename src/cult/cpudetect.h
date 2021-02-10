#ifndef _CULT_CPUDETECT_H
#define _CULT_CPUDETECT_H

#include "app.h"
#include "jsonbuilder.h"
#include "cpuutils.h"

#include <vector>

namespace cult {

class CpuDetect {
public:
  CpuDetect(App* app);
  ~CpuDetect();

  void run();

  void _queryCpuData();
  void _queryCpuInfo();

  void addEntry(const CpuUtils::CpuidIn& in, const CpuUtils::CpuidOut& out);
  CpuUtils::CpuidOut entryOf(uint32_t eax, uint32_t ecx = 0);

  App* _app;
  std::vector<CpuUtils::CpuidEntry> _entries;

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
