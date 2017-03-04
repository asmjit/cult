#ifndef _CULT_BENCHCYCLES_H
#define _CULT_BENCHCYCLES_H

#include "./benchbase.h"

namespace cult {

// ============================================================================
// [cult::InstSpec]
// ============================================================================

struct InstSpec {
  // Instruction signature is 4 values (8-bit each) describing 4 operands:
  enum Op {
    kOpNone = 0,
    kOpGpb,
    kOpGpw,
    kOpGpd,
    kOpGpq,
    kOpAl,
    kOpBl,
    kOpCl,
    kOpDl,
    kOpAx,
    kOpBx,
    kOpCx,
    kOpDx,
    kOpEax,
    kOpEbx,
    kOpEcx,
    kOpEdx,
    kOpRax,
    kOpRbx,
    kOpRcx,
    kOpRdx,
    kOpMm,
    kOpXmm,
    kOpXmm0,
    kOpYmm,
    kOpZmm,
    kOpImm8,
    kOpImm16,
    kOpImm32,
    kOpImm64
  };

  static inline InstSpec none() noexcept {
    InstSpec spec = { 0 };
    return spec;
  }

  static inline InstSpec pack(uint32_t o0, uint32_t o1 = 0, uint32_t o2 = 0, uint32_t o3 = 0) noexcept {
    InstSpec spec;
    spec.value = (o0) | (o1 << 8) | (o2 << 16) | (o3 << 24);
    return spec;
  }

  inline bool isValid() const noexcept { return value != 0; }

  inline uint32_t count() const noexcept {
    uint32_t i = 0;
    while (i < 4 && (value & (0xFF << (i * 8))))
      i++;
    return i;
  }

  inline uint32_t get(uint32_t index) const noexcept {
    assert(index < 4);
    return (value >> (index * 8)) & 0xFF;
  }

  uint32_t value;
};

// ============================================================================
// [cult::BenchCycles]
// ============================================================================

class BenchCycles : public BenchBase {
public:
  typedef void (*Func)(uint32_t nIter, uint64_t* out);

  BenchCycles(App* app) noexcept;
  virtual ~BenchCycles() noexcept;

  double testInstruction(uint32_t instId, InstSpec instSpec, uint32_t parallel);
  void classify(ZoneVector<InstSpec>& dst, uint32_t instId);

  inline bool is64Bit() const noexcept {
    return asmjit::ArchInfo::kTypeHost == asmjit::ArchInfo::kTypeX64;
  }

  bool isImplicit(uint32_t instId) noexcept;
  bool isAvailable(uint32_t instId) noexcept;
  uint32_t getNumIters(uint32_t instId) noexcept;

  inline bool isMMX(uint32_t instId, InstSpec spec) noexcept {
    return spec.get(0) == InstSpec::kOpMm || spec.get(1) == InstSpec::kOpMm;
  }

  inline bool isVec(uint32_t instId, InstSpec spec) noexcept {
    const X86Inst& inst = X86Inst::getInst(instId);
    return inst.isVec() && !isMMX(instId, spec);
  }

  inline bool isSSE(uint32_t instId, InstSpec spec) noexcept {
    const X86Inst& inst = X86Inst::getInst(instId);
    return inst.isVec() && !isMMX(instId, spec) && !inst.isVex() && !inst.isEvex();
  }

  inline bool isAVX(uint32_t instId, InstSpec spec) noexcept {
    const X86Inst& inst = X86Inst::getInst(instId);
    return inst.isVec() && (inst.isVex() || inst.isEvex());
  }

  inline bool isValid(uint32_t instId, const Operand_& op0) const noexcept {
    Operand extraOp;
    Operand_ ops[] = { op0 };
    return X86Inst::validate(asmjit::ArchInfo::kTypeHost, instId, 0, extraOp, ops, static_cast<uint32_t>(ASMJIT_ARRAY_SIZE(ops))) == asmjit::kErrorOk;
  }

  inline bool isValid(uint32_t instId, const Operand_& op0, const Operand_& op1) const noexcept {
    Operand extraOp;
    Operand_ ops[] = { op0, op1 };
    return X86Inst::validate(asmjit::ArchInfo::kTypeHost, instId, 0, extraOp, ops, static_cast<uint32_t>(ASMJIT_ARRAY_SIZE(ops))) == asmjit::kErrorOk;
  }

  inline bool isValid(uint32_t instId, const Operand_& op0, const Operand_& op1, const Operand_& op2) const noexcept {
    Operand extraOp;
    Operand_ ops[] = { op0, op1, op2 };
    return X86Inst::validate(asmjit::ArchInfo::kTypeHost, instId, 0, extraOp, ops, static_cast<uint32_t>(ASMJIT_ARRAY_SIZE(ops))) == asmjit::kErrorOk;
  }

  inline bool isValid(uint32_t instId, const Operand_& op0, const Operand_& op1, const Operand_& op2, const Operand_& op3) const noexcept {
    Operand extraOp;
    Operand_ ops[] = { op0, op1, op2, op3 };
    return X86Inst::validate(asmjit::ArchInfo::kTypeHost, instId, 0, extraOp, ops, static_cast<uint32_t>(ASMJIT_ARRAY_SIZE(ops))) == asmjit::kErrorOk;
  }

  void run() override;
  void beforeBody(X86Assembler& a) override;
  void compileBody(X86Assembler& a, X86Gp rCnt) override;
  void afterBody(X86Assembler& a) override;

  uint32_t _instId;
  InstSpec _instSpec;
  uint32_t _nUnroll;
  uint32_t _nParallel;
};

} // cult namespace

#endif // _CULT_BENCHCYCLES_H
