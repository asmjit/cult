#ifndef _CULT_BENCHBASE_H
#define _CULT_BENCHBASE_H

#include "./app.h"

namespace cult {

class BenchBase {
public:
  typedef void (*Func)(uint32_t nIter, uint64_t* out);

  BenchBase(App* app) noexcept;
  virtual ~BenchBase() noexcept;

  inline bool hasRDTSC() noexcept { return _cpuInfo.hasFeature(asmjit::CpuInfo::kX86FeatureRDTSC); }
  inline bool hasRDTSCP() noexcept { return _cpuInfo.hasFeature(asmjit::CpuInfo::kX86FeatureRDTSCP); }
  inline bool hasSSE() noexcept { return _cpuInfo.hasFeature(asmjit::CpuInfo::kX86FeatureSSE); }
  inline bool hasSSE2() noexcept { return _cpuInfo.hasFeature(asmjit::CpuInfo::kX86FeatureSSE2); }
  inline bool hasSSE3() noexcept { return _cpuInfo.hasFeature(asmjit::CpuInfo::kX86FeatureSSE3); }
  inline bool hasSSSE3() noexcept { return _cpuInfo.hasFeature(asmjit::CpuInfo::kX86FeatureSSSE3); }
  inline bool hasSSE4A() noexcept { return _cpuInfo.hasFeature(asmjit::CpuInfo::kX86FeatureSSE4A); }
  inline bool hasSSE4_1() noexcept { return _cpuInfo.hasFeature(asmjit::CpuInfo::kX86FeatureSSE4_1); }
  inline bool hasSSE4_2() noexcept { return _cpuInfo.hasFeature(asmjit::CpuInfo::kX86FeatureSSE4_2); }
  inline bool hasPCLMULQDQ() noexcept { return _cpuInfo.hasFeature(asmjit::CpuInfo::kX86FeaturePCLMULQDQ); }

  Func compileFunc();
  void releaseFunc(Func func);

  virtual void run() = 0;
  virtual void beforeBody(X86Assembler& a) = 0;
  virtual void compileBody(X86Assembler& a, X86Gp rCnt) = 0;
  virtual void afterBody(X86Assembler& a) = 0;

  App* _app;

  asmjit::JitRuntime _runtime;
  asmjit::CpuInfo _cpuInfo;
};

} // cult namespace

#endif // _CULT_BENCHBASE_H
