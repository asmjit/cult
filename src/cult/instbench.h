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

  static inline bool is_mem_op(uint32_t op) {
    return op >= kOpMem8 && op <= kOpMem512;
  }

  static inline bool is_vm_op(uint32_t op) {
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

  inline bool is_valid() const {
    return _opData[0] != 0;
  }

  inline bool is_lea_scale() const noexcept { return (_flags & kLeaScale) != 0; }

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

  inline uint32_t mem_op() const {
    uint32_t n = count();
    for (uint32_t i = 0; i < n; i++)
      if (_opData[i] >= kOpMem8 && _opData[i] <= kOpVm64z)
        return _opData[i];
    return kOpNone;
  }

  static inline bool is_implicit_op(uint32_t op) {
    return (op >= kOpAl && op <= kOpRdx) || op == kOpXmm0;
  }

  inline InstSpec lea_scale() const noexcept {
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

  void classify(std::vector<InstSpec>& dst, InstId inst_id);
  double test_instruction(InstId inst_id, InstSpec inst_spec, uint32_t parallel, uint32_t mem_alignment, bool overhead_only);

  inline bool is_64bit() const {
    return Environment::is_64bit(Arch::kHost);
  }

  bool is_implicit(InstId inst_id);

  uint32_t num_iter_by_inst_id(InstId inst_id) const;

  inline bool is_mmx(InstId inst_id, InstSpec spec) {
    return spec.get(0) == InstSpec::kOpMm || spec.get(1) == InstSpec::kOpMm;
  }

  inline bool is_vec(InstId inst_id, InstSpec spec) {
    const x86::InstDB::InstInfo& inst = x86::InstDB::inst_info_by_id(inst_id);
    return inst.is_vec() && !is_mmx(inst_id, spec);
  }

  inline bool is_sse(InstId inst_id, InstSpec spec) {
    const x86::InstDB::InstInfo& inst = x86::InstDB::inst_info_by_id(inst_id);
    return inst.is_vec() && !is_mmx(inst_id, spec) && !inst.is_vex() && !inst.is_evex();
  }

  inline bool is_avx(InstId inst_id, InstSpec spec) {
    const x86::InstDB::InstInfo& inst = x86::InstDB::inst_info_by_id(inst_id);
    return inst.is_vec() && (inst.is_vex() || inst.is_evex());
  }

  inline bool can_run(InstId inst_id) const {
    return _can_run(BaseInst(inst_id), nullptr, 0);
  }

  template<typename... ArgsT>
  inline bool can_run(InstId inst_id, ArgsT&&... args) const {
    Operand_ ops[] = { args... };
    return _can_run(BaseInst(inst_id), ops, sizeof...(args));
  }

  bool _can_run(const BaseInst& inst, const Operand_* operands, uint32_t count) const;

  const void* ensure_gather_data(uint32_t element_size, bool is_aligned);
  void free_gather_data(uint32_t element_size);

  uint32_t local_stack_size() const override;
  void run() override;
  void before_body(x86::Assembler& a) override;
  void compile_body(x86::Assembler& a, x86::Gp reg_cnt) override;
  void after_body(x86::Assembler& a) override;

  void fill_memory_u32(x86::Assembler& a, x86::Gp base_address, uint32_t value, uint32_t n);

  uint32_t _inst_id {};
  InstSpec _inst_spec {};
  uint32_t _n_unroll {};
  uint32_t _n_parallel {};
  uint32_t _mem_alignment {};
  bool _overhead_only {};

  void* _gather_data[2];
  uint32_t _gather_data_size;
};

} // {cult} namespace

#endif // _CULT_INSTBENCH_H
