#include "schedutils.h"

namespace cult {

#if defined(_WIN32)
void SchedUtils::setAffinity(uint32_t cpu) {
  SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)(1 << cpu));
}
#else
void SchedUtils::setAffinity(uint32_t cpu) {
  pthread_t thread = pthread_self();
  cpu_set_t cpus;

  CPU_ZERO(&cpus);
  CPU_SET(cpu, &cpus);

  pthread_setaffinity_np(thread, sizeof(cpus), &cpus);
}
#endif

} // cult namespace
