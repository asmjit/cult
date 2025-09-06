#include "schedutils.h"

#if defined(__APPLE__)
#include <mach/thread_act.h>
#include <mach/thread_policy.h>
#endif

namespace cult {

#if defined(_WIN32)
void SchedUtils::set_affinity(uint32_t cpu) {
  SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)(uint64_t(1) << cpu));
}
#elif defined(__APPLE__)
void SchedUtils::set_affinity(uint32_t cpu) {
  pthread_t thread = pthread_self();
  thread_port_t mach_thread = pthread_mach_thread_np(thread);
  thread_affinity_policy_data_t policy = { int(cpu) };

  thread_policy_set(mach_thread, THREAD_AFFINITY_POLICY, (thread_policy_t)&policy, 1);
}
#else
void SchedUtils::set_affinity(uint32_t cpu) {
  pthread_t thread = pthread_self();
  cpu_set_t cpus;

  CPU_ZERO(&cpus);
  CPU_SET(cpu, &cpus);

  pthread_setaffinity_np(thread, sizeof(cpus), &cpus);
}
#endif

} // {cult} namespace
