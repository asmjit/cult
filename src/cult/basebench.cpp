#include "./basebench.h"

namespace cult {

class SimpleErrorHandler : public ErrorHandler {
public:
  inline SimpleErrorHandler() : _err(kErrorOk) {}

  void handle_error(Error err, const char * message, BaseEmitter * origin) override {
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

BaseBench::Func BaseBench::compile_func() {
  FileLogger logger(stdout);
  SimpleErrorHandler eh;

  CodeHolder code;

  code.init(_runtime.environment());
  code.set_error_handler(&eh);

  if (_app->dump()) {
    logger.add_flags(FormatFlags::kMachineCode | FormatFlags::kExplainImms);
    code.set_logger(&logger);
  }

  x86::Assembler a(&code);
  a.add_diagnostic_options(DiagnosticOptions::kValidateAssembler);

  FuncDetail fd;
  fd.init(FuncSignature::build<void, uint32_t, uint64_t*>(CallConvId::kCDecl), code.environment());

  FuncFrame frame;
  frame.init(fd);

  frame.set_all_dirty(RegGroup::kGp);
  frame.set_all_dirty(RegGroup::kVec);
  frame.set_local_stack_size(local_stack_size() + 128);
  frame.set_local_stack_alignment(64);

  // Configure some stack vars that we use to save GP regs.
  x86::Mem stack       = x86::ptr(a.zsp(), local_stack_size());
  x86::Mem m_out       = stack; stack.add_offset(a.register_size());
  x86::Mem m_cycles_lo = stack; stack.add_offset(4);
  x86::Mem m_cycles_hi = stack; stack.add_offset(4);

  x86::Gp reg_cnt = x86::ebp; // Cannot be EAX|EBX|ECX|EDX as these are clobbered by CPUID.
  x86::Gp reg_out = a.zbx();  // Cannot be ESI|EDI as these are used by the cycle counter.

  FuncArgsAssignment args(&fd);
  args.assign_all(reg_cnt, reg_out);
  args.update_func_frame(frame);
  frame.finalize();

  // --- Function prolog ---
  a.emit_prolog(frame);
  a.emit_args_assignment(frame, args);

  // --- Benchmark prolog ---
  a.mov(m_out, reg_out);
  before_body(a);

  a.xor_(x86::eax, x86::eax);
  a.mfence();
  a.lfence();
  a.rdtsc();
  a.mov(m_cycles_lo, x86::eax);
  a.mov(m_cycles_hi, x86::edx);

  // --- Benchmark body ---
  compile_body(a, reg_cnt);

  // --- Benchmark epilog ---
  if (x86_features().has_rdtscp()) {
    a.rdtscp();
    a.lfence();
  }
  else {
    a.lfence();
    a.rdtsc();
  }

  a.mov(reg_out, m_out);
  a.sub(x86::eax, m_cycles_lo);
  a.sbb(x86::edx, m_cycles_hi);

  a.mov(x86::ptr(reg_out, 0), x86::eax);
  a.mov(x86::ptr(reg_out, 4), x86::edx);

  // --- Function epilog ---
  after_body(a);
  a.emit_epilog(frame);
  code.detach(&a);

  if (eh._err != Error::kOk)
    return nullptr;

  Func func;
  _runtime.add(&func, &code);
  return func;
}

void BaseBench::release_func(Func func) {
  _runtime.release(func);
}

} // {cult} namespace
