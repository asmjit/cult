#include "./basebench.h"

namespace cult {

class SimpleErrorHandler : public ErrorHandler {
public:
  inline SimpleErrorHandler() : _err(kErrorOk) {}

  void handleError(Error err, const char * message, BaseEmitter * origin) override {
    _err = err;
    printf("Assembler Error: %s\n", message);
  }

  Error _err;
};

BaseBench::BaseBench(App* app)
  : _app(app),
    _runtime(),
    _cpuInfo(CpuInfo::host()) {}
BaseBench::~BaseBench() {}

BaseBench::Func BaseBench::compileFunc() {
  FileLogger logger(stdout);
  SimpleErrorHandler eh;

  CodeHolder code;

  code.init(_runtime.environment());
  code.setErrorHandler(&eh);

  if (_app->dump()) {
    logger.addFlags(FormatFlags::kMachineCode | FormatFlags::kExplainImms);
    code.setLogger(&logger);
  }

  x86::Assembler a(&code);

  FuncDetail fd;
  fd.init(FuncSignatureT<void, uint32_t, uint64_t*>(CallConvId::kCDecl), code.environment());

  FuncFrame frame;
  frame.init(fd);

  uint32_t localStackSize = 64 * 65 + 128;

  frame.setAllDirty(RegGroup::kGp);
  frame.setAllDirty(RegGroup::kVec);
  frame.setLocalStackSize(localStackSize);
  frame.setLocalStackAlignment(64);

  // Configure some stack vars that we use to save GP regs.
  x86::Mem stack     = x86::ptr(a.zsp(), localStackSize - 128);
  x86::Mem mOut      = stack; stack.addOffset(a.registerSize());
  x86::Mem mCyclesLo = stack; stack.addOffset(4);
  x86::Mem mCyclesHi = stack; stack.addOffset(4);

  x86::Gp rCnt = x86::ebp;                 // Cannot be EAX|EBX|ECX|EDX as these are clobbered by CPUID.
  x86::Gp rOut = a.zbx();                  // Cannot be ESI|EDI as these are used by the cycle counter.

  FuncArgsAssignment args(&fd);
  args.assignAll(rCnt, rOut);
  args.updateFuncFrame(frame);
  frame.finalize();

  // --- Function prolog ---
  a.emitProlog(frame);
  a.emitArgsAssignment(frame, args);

  // --- Benchmark prolog ---
  a.mov(mOut, rOut);
  beforeBody(a);

  a.xor_(x86::eax, x86::eax);
  a.cpuid();
  a.rdtsc();
  a.mov(mCyclesLo, x86::eax);
  a.mov(mCyclesHi, x86::edx);

  // --- Benchmark body ---
  compileBody(a, rCnt);

  // --- Benchmark epilog ---
  if (x86Features().hasRDTSCP()) {
    a.rdtscp();
    a.mov(x86::esi, x86::eax);
    a.mov(x86::edi, x86::edx);
    a.xor_(x86::eax, x86::eax);
    a.cpuid();
  }
  else {
    if (x86Features().hasSSE2())
      a.lfence();
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
  a.emitEpilog(frame);
  code.detach(&a);

  if (eh._err)
    return nullptr;

  Func func;
  _runtime.add(&func, &code);
  return func;
}

void BaseBench::releaseFunc(Func func) {
  _runtime.release(func);
}

} // {cult} namespace
