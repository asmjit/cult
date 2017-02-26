#include "./benchbase.h"

namespace cult {

class MyErrorHandler : public asmjit::ErrorHandler {
public:
  bool handleError(asmjit::Error err, const char * message, asmjit::CodeEmitter * origin) override {
    printf("Assembler Error: %s\n", message);
    return false;
  }
};

BenchBase::BenchBase(App* app) noexcept
  : _app(app),
    _runtime() {
  _cpuInfo.detect();
}

BenchBase::~BenchBase() noexcept {
}

BenchBase::Func BenchBase::compileFunc() {
  using namespace asmjit;

  FileLogger logger(stdout);
  MyErrorHandler eh;

  CodeHolder code;

  code.init(_runtime.getCodeInfo());
  code.setErrorHandler(&eh);

  if (_app->dump()) {
    logger.addOptions(Logger::kOptionBinaryForm);
    logger.addOptions(Logger::kOptionImmExtended);
    code.setLogger(&logger);
  }

  X86Assembler a(&code);

  FuncDetail fd;
  fd.init(FuncSignature2<void, uint32_t, uint64_t*>(CallConv::kIdHost));

  FuncFrameInfo ffi;
  ffi.setAllDirty(X86Reg::kKindGp);
  ffi.setAllDirty(X86Reg::kKindVec);
  ffi.setStackFrameSize(1024);

  // Configure some stack vars that we use to save GP regs.
  X86Mem stack     = x86::ptr(a.zsp());
  X86Mem mOut      = stack; stack.addOffset(a.getGpSize());
  X86Mem mCyclesLo = stack; stack.addOffset(4);
  X86Mem mCyclesHi = stack; stack.addOffset(4);

  X86Gp rCnt = x86::ebp;                 // Cannot be EAX|EBX|ECX|EDX as these are clobbered by CPUID.
  X86Gp rOut = a.zbx();                  // Cannot be ESI|EDI as these are used by the cycle counter.

  FuncArgsMapper args(&fd);
  args.assignAll(rCnt, rOut);
  args.updateFrameInfo(ffi);

  FuncFrameLayout layout;
  layout.init(fd, ffi);

  // --- Function prolog ---
  FuncUtils::emitProlog(&a, layout);
  FuncUtils::allocArgs(&a, layout, args);

  // --- Benchmark prolog ---
  a.mov(mOut, rOut);
  beforeBody(a);

  a.cpuid();
  a.rdtsc();
  a.mov(mCyclesLo, x86::eax);
  a.mov(mCyclesHi, x86::edx);

  // --- Benchmark body ---
  compileBody(a, rCnt);

  // --- Benchmark epilog ---
  if (hasRDTSCP()) {
    a.rdtscp();
    a.mov(x86::esi, x86::eax);
    a.mov(x86::edi, x86::edx);
    a.cpuid();
  }
  else {
    a.mov(a.zax(), x86::cr0);
    a.mov(x86::cr0, a.zax());
    a.rdtsc();
    a.mov(x86::esi, x86::eax);
    a.mov(x86::edi, x86::edx);
  }

  a.mov(rOut, mOut);
  a.sub(x86::esi, mCyclesLo);
  a.sbb(x86::edi, mCyclesHi);

  a.mov(x86::ptr(rOut, 0), x86::esi);
  a.mov(x86::ptr(rOut, 4), x86::edi);

  // --- Function epilog ---
  afterBody(a);
  FuncUtils::emitEpilog(&a, layout);

  if (a.isInErrorState())
    return nullptr;

  code.detach(&a);

  Func func;
  _runtime.add(&func, &code);
  return func;
}

void BenchBase::releaseFunc(Func func) {
  _runtime.release(func);
}

} // cult namespace
