#ifndef _CULT_BASEBENCH_H
#define _CULT_BASEBENCH_H

#include "app.h"

namespace cult {

class BaseBench {
public:
  typedef void (*Func)(uint32_t nIter, uint64_t* out);

  BaseBench(App* app);
  virtual ~BaseBench();

  inline const CpuFeatures::X86& x86Features() const { return _cpuInfo.features().x86(); }

  Func compileFunc();
  void releaseFunc(Func func);

  virtual void run() = 0;
  virtual void beforeBody(x86::Assembler& a) = 0;
  virtual void compileBody(x86::Assembler& a, x86::Gp rCnt) = 0;
  virtual void afterBody(x86::Assembler& a) = 0;

  App* _app;

  JitRuntime _runtime;
  CpuInfo _cpuInfo;
};

} // cult namespace

#endif // _CULT_BASEBENCH_H
