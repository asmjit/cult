#ifndef _CULT_INSTBENCH_H
#define _CULT_INSTBENCH_H

#include <vector>

#include "basebench.h"

namespace cult {

// ============================================================================
// [cult::InstSpec]
// ============================================================================

struct InstSpec {
  // Instruction signature is 6 values (8-bit each) describing 6 operands:
  enum Op : uint8_t {
    kOpNone = 0,
    kOpRel,
    kOpGpb,
    kOpGpw,
    kOpGpd,
    kOpGpq,
    kOpAl,
    kOpCl,
    kOpDl,
    kOpBl,
    kOpAx,
    kOpCx,
    kOpDx,
    kOpBx,
    kOpEax,
    kOpEcx,
    kOpEdx,
    kOpEbx,
    kOpRax,
    kOpRcx,
    kOpRdx,
    kOpRbx,
    kOpMm,
    kOpXmm,
    kOpXmm0,
    kOpYmm,
    kOpZmm,
    kOpKReg,
    kOpImm8,
    kOpImm16,
    kOpImm32,
    kOpImm64,
    kOpMem8,
    kOpMem16,
    kOpMem32,
    kOpMem64,
    kOpMem128,
    kOpMem256,
    kOpMem512,
    kOpVm32x,
    kOpVm32y,
    kOpVm32z,
    kOpVm64x,
    kOpVm64y,
    kOpVm64z
  };

  enum Flags : uint8_t {
    kLeaScale = 0x01
  };

  static inline InstSpec none() {
    return InstSpec{};
  }

  static inline InstSpec pack(uint32_t o0, uint32_t o1 = 0, uint32_t o2 = 0, uint32_t o3 = 0, uint32_t o4 = 0, uint32_t o5 = 0) {
    return InstSpec{{Op(o0), Op(o1), Op(o2), Op(o3), Op(o4), Op(o5)}};
  }

  static inline bool isMemOp(uint32_t op) {
    return op >= kOpMem8 && op <= kOpMem512;
  }

  static inline bool isVmOp(uint32_t op) {
    return op >= kOpVm32x && op <= kOpVm64z;
  }

  inline bool operator<(const InstSpec& other) const noexcept {
    for (uint32_t i = 0; i < 6; i++)
      if (_opData[i] < other._opData[i])
        return true;
    return _flags < other._flags;
  }

  inline bool operator==(const InstSpec& other) const noexcept {
    for (uint32_t i = 0; i < 6; i++)
      if (_opData[i] != other._opData[i])
        return false;
    return _flags == other._flags;
  }

  inline bool isValid() const {
    return _opData[0] != 0;
  }

  inline bool isLeaScale() const noexcept { return (_flags & kLeaScale) != 0; }

  inline uint32_t count() const {
    uint32_t i = 0;
    while (i < 6 && _opData[i] != 0)
      i++;
    return i;
  }

  inline uint32_t get(size_t index) const {
    assert(index < 6);
    return _opData[index];
  }

  inline void set(size_t index, uint32_t v) {
    assert(index < 6);
    _opData[index] = Op(v);
  }

  inline uint32_t memOp() const {
    uint32_t n = count();
    for (uint32_t i = 0; i < n; i++)
      if (_opData[i] >= kOpMem8 && _opData[i] <= kOpVm64z)
        return _opData[i];
    return kOpNone;
  }

  static inline bool isImplicitOp(uint32_t op) {
    return (op >= kOpAl && op <= kOpRdx) || op == kOpXmm0;
  }

  inline InstSpec leaScale() const noexcept {
    InstSpec out(*this);
    out._flags = uint8_t(out._flags | kLeaScale);
    return out;
  }

  Op _opData[6];
  uint8_t _flags;
};

// ============================================================================
// [cult::InstBench]
// ============================================================================

class InstBench : public BaseBench {
public:
  typedef void (*Func)(uint32_t nIter, uint64_t* out);

  InstBench(App* app);
  virtual ~InstBench();

  void classify(std::vector<InstSpec>& dst, InstId instId);
  double testInstruction(InstId instId, InstSpec instSpec, uint32_t parallel, uint32_t memAlignment, bool overheadOnly);

  inline bool is64Bit() const {
    return Environment::is64Bit(Arch::kHost);
  }

  bool isImplicit(InstId instId);

  uint32_t numIterByInstId(InstId instId) const;

  inline bool isMMX(InstId instId, InstSpec spec) {
    return spec.get(0) == InstSpec::kOpMm || spec.get(1) == InstSpec::kOpMm;
  }

  inline bool isVec(InstId instId, InstSpec spec) {
    const x86::InstDB::InstInfo& inst = x86::InstDB::infoById(instId);
    return inst.isVec() && !isMMX(instId, spec);
  }

  inline bool isSSE(InstId instId, InstSpec spec) {
    const x86::InstDB::InstInfo& inst = x86::InstDB::infoById(instId);
    return inst.isVec() && !isMMX(instId, spec) && !inst.isVex() && !inst.isEvex();
  }

  inline bool isAVX(InstId instId, InstSpec spec) {
    const x86::InstDB::InstInfo& inst = x86::InstDB::infoById(instId);
    return inst.isVec() && (inst.isVex() || inst.isEvex());
  }

  inline bool canRun(InstId instId) const {
    return _canRun(BaseInst(instId), nullptr, 0);
  }

  template<typename... ArgsT>
  inline bool canRun(InstId instId, ArgsT&&... args) const {
    Operand_ ops[] = { args... };
    return _canRun(BaseInst(instId), ops, sizeof...(args));
  }

  bool _canRun(const BaseInst& inst, const Operand_* operands, uint32_t count) const;

  const void* ensureGatherData(uint32_t elementSize, bool isAligned);
  void freeGatherData(uint32_t elementSize);

  uint32_t localStackSize() const override;
  void run() override;
  void beforeBody(x86::Assembler& a) override;
  void compileBody(x86::Assembler& a, x86::Gp rCnt) override;
  void afterBody(x86::Assembler& a) override;

  void fillMemory32(x86::Assembler& a, x86::Gp baseAddress, uint32_t value, uint32_t n);

  uint32_t _instId {};
  InstSpec _instSpec {};
  uint32_t _nUnroll {};
  uint32_t _nParallel {};
  uint32_t _memAlignment {};
  bool _overheadOnly {};

  void* _gatherData[2];
  uint32_t _gatherDataSize;
};

} // {cult} namespace

#endif // _CULT_INSTBENCH_H
