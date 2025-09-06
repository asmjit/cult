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

  uint32_t _model_id;
  uint32_t _family_id;
  uint32_t _stepping_id;

  char _vendor_string[16];
  char _vendor_name[16];
  char _brand_string[64];
  char _uarch_name[32];
};

} // {cult} namespace

#endif // _CULT_CPUDETECT_H
