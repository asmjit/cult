#ifndef _CULT_BASEBENCH_H
#define _CULT_BASEBENCH_H

#include "app.h"

namespace cult {

class BaseBench {
public:
  typedef void (*Func)(uint32_t n_iter, uint64_t* out);

  BaseBench(App* app);
  virtual ~BaseBench();

  inline const CpuFeatures::X86& x86_features() const { return _cpuInfo.features().x86(); }

  Func compile_func();
  void release_func(Func func);

  virtual uint32_t local_stack_size() const = 0;
  virtual void run() = 0;
  virtual void before_body(x86::Assembler& a) = 0;
  virtual void compile_body(x86::Assembler& a, x86::Gp reg_cnt) = 0;
  virtual void after_body(x86::Assembler& a) = 0;

  App* _app;

  JitRuntime _runtime;
  CpuInfo _cpuInfo;
};

} // {cult} namespace

#endif // _CULT_BASEBENCH_H
