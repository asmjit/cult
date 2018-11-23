#ifndef _CULT_INSTBENCH_H
#define _CULT_INSTBENCH_H

#include "./basebench.h"

namespace cult {

// ============================================================================
// [cult::InstSpec]
// ============================================================================

struct InstSpec {
  // Instruction signature is 4 values (8-bit each) describing 4 operands:
  enum Op : uint32_t {
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
// [cult::InstBench]
// ============================================================================

class InstBench : public BaseBench {
public:
  typedef void (*Func)(uint32_t nIter, uint64_t* out);

  InstBench(App* app) noexcept;
  virtual ~InstBench() noexcept;

  double testInstruction(uint32_t instId, InstSpec instSpec, uint32_t parallel);
  void classify(ZoneVector<InstSpec>& dst, uint32_t instId);

  inline bool is64Bit() const noexcept {
    return ArchInfo::kIdHost == ArchInfo::kIdX64;
  }

  bool isImplicit(uint32_t instId) noexcept;

  uint32_t numIterByInstId(uint32_t instId) noexcept;

  inline bool isMMX(uint32_t instId, InstSpec spec) noexcept {
    return spec.get(0) == InstSpec::kOpMm || spec.get(1) == InstSpec::kOpMm;
  }

  inline bool isVec(uint32_t instId, InstSpec spec) noexcept {
    const x86::InstDB::InstInfo& inst = x86::InstDB::infoById(instId);
    return inst.isVec() && !isMMX(instId, spec);
  }

  inline bool isSSE(uint32_t instId, InstSpec spec) noexcept {
    const x86::InstDB::InstInfo& inst = x86::InstDB::infoById(instId);
    return inst.isVec() && !isMMX(instId, spec) && !inst.isVex() && !inst.isEvex();
  }

  inline bool isAVX(uint32_t instId, InstSpec spec) noexcept {
    const x86::InstDB::InstInfo& inst = x86::InstDB::infoById(instId);
    return inst.isVec() && (inst.isVex() || inst.isEvex());
  }

  inline bool canRun(uint32_t instId) const noexcept {
    return _canRun(BaseInst(instId), nullptr, 0);
  }

  inline bool canRun(uint32_t instId, const Operand_& op0) const noexcept {
    Operand_ ops[] = { op0 };
    return _canRun(BaseInst(instId), ops, 1);
  }

  inline bool canRun(uint32_t instId, const Operand_& op0, const Operand_& op1) const noexcept {
    Operand extraOp;
    Operand_ ops[] = { op0, op1 };
    return _canRun(BaseInst(instId), ops, 2);
  }

  inline bool canRun(uint32_t instId, const Operand_& op0, const Operand_& op1, const Operand_& op2) const noexcept {
    Operand extraOp;
    Operand_ ops[] = { op0, op1, op2 };
    return _canRun(BaseInst(instId), ops, 3);
  }

  inline bool canRun(uint32_t instId, const Operand_& op0, const Operand_& op1, const Operand_& op2, const Operand_& op3) const noexcept {
    Operand extraOp;
    Operand_ ops[] = { op0, op1, op2, op3 };
    return _canRun(BaseInst(instId), ops, 4);
  }

  bool _canRun(const BaseInst& inst, const Operand_* operands, uint32_t count) const noexcept;

  void run() override;
  void beforeBody(x86::Assembler& a) override;
  void compileBody(x86::Assembler& a, x86::Gp rCnt) override;
  void afterBody(x86::Assembler& a) override;

  uint32_t _instId;
  InstSpec _instSpec;
  uint32_t _nUnroll;
  uint32_t _nParallel;
};

} // cult namespace

#endif // _CULT_INSTBENCH_H
