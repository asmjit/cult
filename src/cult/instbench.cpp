#include "instbench.h"
#include "cpuutils.h"

#include <set>

namespace cult {

class Random {
public:
  // Constants suggested as `23/18/5`.
  enum Steps : uint32_t {
    kStep1_SHL = 23,
    kStep2_SHR = 18,
    kStep3_SHR = 5
  };

  inline explicit Random(uint64_t seed = 0) noexcept { reset(seed); }
  inline Random(const Random& other) noexcept = default;

  inline void reset(uint64_t seed = 0) noexcept {
    // The number is arbitrary, it means nothing.
    constexpr uint64_t kZeroSeed = 0x1F0A2BE71D163FA0u;

    // Generate the state data by using splitmix64.
    for (uint32_t i = 0; i < 2; i++) {
      seed += 0x9E3779B97F4A7C15u;
      uint64_t x = seed;
      x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9u;
      x = (x ^ (x >> 27)) * 0x94D049BB133111EBu;
      x = (x ^ (x >> 31));
      _state[i] = x != 0 ? x : kZeroSeed;
    }
  }

  inline uint32_t next_uint32() noexcept {
    return uint32_t(next_uint64() >> 32);
  }

  inline uint64_t next_uint64() noexcept {
    uint64_t x = _state[0];
    uint64_t y = _state[1];

    x ^= x << kStep1_SHL;
    y ^= y >> kStep3_SHR;
    x ^= x >> kStep2_SHR;
    x ^= y;

    _state[0] = y;
    _state[1] = x;
    return x + y;
  }

  uint64_t _state[2];
};

class InstSignatureIterator {
public:
  typedef asmjit::x86::InstDB::InstSignature InstSignature;
  typedef asmjit::x86::InstDB::OpSignature OpSignature;

  const InstSignature* _inst_signature;
  const OpSignature* _op_sig_array[asmjit::Globals::kMaxOpCount];
  x86::InstDB::OpFlags _op_mask_array[asmjit::Globals::kMaxOpCount];
  uint32_t _opCount;
  x86::InstDB::OpFlags _filter;
  bool _isValid;

  static constexpr uint32_t kMaxOpCount = Globals::kMaxOpCount;

  static constexpr x86::InstDB::OpFlags kDefaultFilter =
    x86::InstDB::OpFlags::kRegMask |
    x86::InstDB::OpFlags::kMemMask |
    x86::InstDB::OpFlags::kVmMask  |
    x86::InstDB::OpFlags::kImmMask |
    x86::InstDB::OpFlags::kRelMask ;

  inline InstSignatureIterator() { reset(); }
  inline InstSignatureIterator(const InstSignature& inst_signature, x86::InstDB::OpFlags filter = kDefaultFilter) { init(inst_signature, filter); }
  inline InstSignatureIterator(const InstSignatureIterator& other) { init(other); }

  inline void reset() { ::memset(this, 0, sizeof(*this)); }
  inline void init(const InstSignatureIterator& other) { ::memcpy(this, &other, sizeof(*this)); }

  void init(const InstSignature& inst_signature, x86::InstDB::OpFlags filter = kDefaultFilter) {
    const OpSignature* op_sgn_array = asmjit::x86::InstDB::_op_signature_table;
    uint32_t op_count = inst_signature.op_count();

    _inst_signature = &inst_signature;
    _opCount = op_count;
    _filter = filter;

    uint32_t i;
    x86::InstDB::OpFlags flags = x86::InstDB::OpFlags::kNone;

    for (i = 0; i < op_count; i++) {
      const OpSignature& op_sgn = inst_signature.op_signature(i);
      flags = op_sgn.flags() & _filter;

      if (flags == x86::InstDB::OpFlags::kNone)
        break;

      _op_sig_array[i] = &op_sgn;
      _op_mask_array[i] = x86::InstDB::OpFlags(asmjit::Support::blsi(uint64_t(flags)));
    }

    while (i < kMaxOpCount) {
      _op_sig_array[i] = &op_sgn_array[0];
      _op_mask_array[i] = x86::InstDB::OpFlags::kNone;
      i++;
    }

    _isValid = op_count == 0 || flags != x86::InstDB::OpFlags::kNone;
  }

  inline bool is_valid() const { return _isValid; }
  inline uint32_t op_count() const { return _opCount; }

  inline const x86::InstDB::OpFlags* op_mask_array() const { return _op_mask_array; }
  inline const OpSignature* const* op_sgn_array() const { return _op_sig_array; }

  inline x86::InstDB::OpFlags op_mask(uint32_t i) const { return _op_mask_array[i]; }
  inline const OpSignature* op_sgn(uint32_t i) const { return _op_sig_array[i]; }

  bool next() {
    uint32_t i = _opCount - 1u;
    for (;;) {
      if (i == 0xFFFFFFFFu) {
        _isValid = false;
        return false;
      }

      // Iterate over OpFlags.
      x86::InstDB::OpFlags prevBit = _op_mask_array[i];
      x86::InstDB::OpFlags allFlags = _op_sig_array[i]->flags() & _filter;

      x86::InstDB::OpFlags bitsToClear = (x86::InstDB::OpFlags)(uint64_t(prevBit) | (uint64_t(prevBit) - 1u));
      x86::InstDB::OpFlags remainingBits = allFlags & ~bitsToClear;

      if (remainingBits != x86::InstDB::OpFlags::kNone) {
        _op_mask_array[i] = (x86::InstDB::OpFlags)asmjit::Support::blsi(uint64_t(remainingBits));
        return true;
      }
      else {
        _op_mask_array[i--] = (x86::InstDB::OpFlags)asmjit::Support::blsi(uint64_t(allFlags));
      }
    }
  }
};

// TODO: These require pretty special register pattern - not tested yet.
static bool is_ignored_inst(InstId inst_id) {
  return inst_id == x86::Inst::kIdVp2intersectd ||
         inst_id == x86::Inst::kIdVp2intersectq;
}

// Returns true when the instruction is safe to be benchmarked.
//
// There is many general purpose instructions including system ones. We only
// benchmark those that may appear commonly in user code, but not in kernel.
static bool is_safe_gp_inst(InstId inst_id) {
  return inst_id == x86::Inst::kIdAdc        ||
         inst_id == x86::Inst::kIdAdcx       ||
         inst_id == x86::Inst::kIdAdd        ||
         inst_id == x86::Inst::kIdAdox       ||
         inst_id == x86::Inst::kIdAnd        ||
         inst_id == x86::Inst::kIdAndn       ||
         inst_id == x86::Inst::kIdBextr      ||
         inst_id == x86::Inst::kIdBlcfill    ||
         inst_id == x86::Inst::kIdBlci       ||
         inst_id == x86::Inst::kIdBlcic      ||
         inst_id == x86::Inst::kIdBlcmsk     ||
         inst_id == x86::Inst::kIdBlcs       ||
         inst_id == x86::Inst::kIdBlsfill    ||
         inst_id == x86::Inst::kIdBlsi       ||
         inst_id == x86::Inst::kIdBlsic      ||
         inst_id == x86::Inst::kIdBlsmsk     ||
         inst_id == x86::Inst::kIdBlsr       ||
         inst_id == x86::Inst::kIdBsf        ||
         inst_id == x86::Inst::kIdBsr        ||
         inst_id == x86::Inst::kIdBswap      ||
         inst_id == x86::Inst::kIdBt         ||
         inst_id == x86::Inst::kIdBtc        ||
         inst_id == x86::Inst::kIdBtr        ||
         inst_id == x86::Inst::kIdBts        ||
         inst_id == x86::Inst::kIdBzhi       ||
         inst_id == x86::Inst::kIdCbw        ||
         inst_id == x86::Inst::kIdCdq        ||
         inst_id == x86::Inst::kIdCdqe       ||
         inst_id == x86::Inst::kIdCmp        ||
         inst_id == x86::Inst::kIdCrc32      ||
         inst_id == x86::Inst::kIdCqo        ||
         inst_id == x86::Inst::kIdCwd        ||
         inst_id == x86::Inst::kIdCwde       ||
         inst_id == x86::Inst::kIdDec        ||
         inst_id == x86::Inst::kIdDiv        ||
         inst_id == x86::Inst::kIdIdiv       ||
         inst_id == x86::Inst::kIdImul       ||
         inst_id == x86::Inst::kIdInc        ||
         inst_id == x86::Inst::kIdLzcnt      ||
         inst_id == x86::Inst::kIdMov        ||
         inst_id == x86::Inst::kIdMovbe      ||
         inst_id == x86::Inst::kIdMovsx      ||
         inst_id == x86::Inst::kIdMovsxd     ||
         inst_id == x86::Inst::kIdMovzx      ||
         inst_id == x86::Inst::kIdMul        ||
         inst_id == x86::Inst::kIdMulx       ||
         inst_id == x86::Inst::kIdNeg        ||
         inst_id == x86::Inst::kIdNop        ||
         inst_id == x86::Inst::kIdNot        ||
         inst_id == x86::Inst::kIdOr         ||
         inst_id == x86::Inst::kIdPdep       ||
         inst_id == x86::Inst::kIdPext       ||
         inst_id == x86::Inst::kIdPop        ||
         inst_id == x86::Inst::kIdPopcnt     ||
         inst_id == x86::Inst::kIdPush       ||
         inst_id == x86::Inst::kIdRcl        ||
         inst_id == x86::Inst::kIdRcr        ||
         inst_id == x86::Inst::kIdRdrand     ||
         inst_id == x86::Inst::kIdRdseed     ||
         inst_id == x86::Inst::kIdRol        ||
         inst_id == x86::Inst::kIdRor        ||
         inst_id == x86::Inst::kIdRorx       ||
         inst_id == x86::Inst::kIdSar        ||
         inst_id == x86::Inst::kIdSarx       ||
         inst_id == x86::Inst::kIdSbb        ||
         inst_id == x86::Inst::kIdShl        ||
         inst_id == x86::Inst::kIdShld       ||
         inst_id == x86::Inst::kIdShlx       ||
         inst_id == x86::Inst::kIdShr        ||
         inst_id == x86::Inst::kIdShrd       ||
         inst_id == x86::Inst::kIdShrx       ||
         inst_id == x86::Inst::kIdSub        ||
         inst_id == x86::Inst::kIdT1mskc     ||
         inst_id == x86::Inst::kIdTest       ||
         inst_id == x86::Inst::kIdTzcnt      ||
         inst_id == x86::Inst::kIdTzmsk      ||
         inst_id == x86::Inst::kIdXadd       ||
         inst_id == x86::Inst::kIdXchg       ||
         inst_id == x86::Inst::kIdXor        ;
}

static uint32_t gatherIndexSize(InstId inst_id) {
  switch (inst_id) {
    case x86::Inst::kIdVgatherdps: return 32;
    case x86::Inst::kIdVgatherdpd: return 32;
    case x86::Inst::kIdVgatherqps: return 64;
    case x86::Inst::kIdVgatherqpd: return 64;
    case x86::Inst::kIdVpgatherdd: return 32;
    case x86::Inst::kIdVpgatherdq: return 32;
    case x86::Inst::kIdVpgatherqd: return 64;
    case x86::Inst::kIdVpgatherqq: return 64;
    default:
      return 0;
  }
}

static uint32_t scatter_index_size(InstId inst_id) {
  switch (inst_id) {
    case x86::Inst::kIdVscatterdps: return 32;
    case x86::Inst::kIdVscatterdpd: return 32;
    case x86::Inst::kIdVscatterqps: return 64;
    case x86::Inst::kIdVscatterqpd: return 64;
    case x86::Inst::kIdVpscatterdd: return 32;
    case x86::Inst::kIdVpscatterdq: return 32;
    case x86::Inst::kIdVpscatterqd: return 64;
    case x86::Inst::kIdVpscatterqq: return 64;
    default:
      return 0;
  }
}

static uint32_t scatter_element_size(InstId inst_id) {
  switch (inst_id) {
    case x86::Inst::kIdVscatterdps: return 32;
    case x86::Inst::kIdVscatterdpd: return 64;
    case x86::Inst::kIdVscatterqps: return 32;
    case x86::Inst::kIdVscatterqpd: return 64;
    case x86::Inst::kIdVpscatterdd: return 32;
    case x86::Inst::kIdVpscatterdq: return 64;
    case x86::Inst::kIdVpscatterqd: return 32;
    case x86::Inst::kIdVpscatterqq: return 64;
    default:
      return 0;
  }
}

static bool is_gather_inst(InstId inst_id) {
  return gatherIndexSize(inst_id) != 0;
}

static bool is_scatter_inst(InstId inst_id) {
  return scatter_index_size(inst_id) != 0;
}

static bool is_safe_unaligned(InstId inst_id, uint32_t mem_op) {
  const x86::InstDB::InstInfo& inst = x86::InstDB::inst_info_by_id(inst_id);

  if (inst_id == x86::Inst::kIdNop)
    return false;

  if (inst.is_sse()) {
    return inst_id == x86::Inst::kIdMovdqu ||
           inst_id == x86::Inst::kIdMovupd ||
           inst_id == x86::Inst::kIdMovups ||
           mem_op != InstSpec::kOpMem128;
  }

  if (inst.is_avx() || inst.is_evex()) {
    return inst_id != x86::Inst::kIdVmovapd &&
           inst_id != x86::Inst::kIdVmovaps &&
           inst_id != x86::Inst::kIdVmovdqa &&
           inst_id != x86::Inst::kIdVmovdqa32 &&
           inst_id != x86::Inst::kIdVmovdqa64 &&
           inst_id != x86::Inst::kIdVmovntdq &&
           inst_id != x86::Inst::kIdVmovntdqa &&
           inst_id != x86::Inst::kIdVmovntpd &&
           inst_id != x86::Inst::kIdVmovntps;
  }

  return true;
}

static void inst_spec_to_operand_array(Arch arch, Operand* operands, InstSpec spec) {
  x86::Gp p;
  if (arch == Arch::kX86)
    p = x86::eax;
  else
    p = x86::rax;

  for (uint32_t i = 0; i < 6; i++) {
    switch (spec.get(i)) {
      case InstSpec::kOpRel: operands[i] = Label(1); break;
      case InstSpec::kOpGpb: operands[i] = x86::al; break;
      case InstSpec::kOpGpw: operands[i] = x86::ax; break;
      case InstSpec::kOpGpd: operands[i] = x86::eax; break;
      case InstSpec::kOpGpq: operands[i] = x86::rax; break;
      case InstSpec::kOpAl: operands[i] = x86::al; break;
      case InstSpec::kOpCl: operands[i] = x86::cl; break;
      case InstSpec::kOpDl: operands[i] = x86::dl; break;
      case InstSpec::kOpBl: operands[i] = x86::bl; break;
      case InstSpec::kOpAx: operands[i] = x86::ax; break;
      case InstSpec::kOpCx: operands[i] = x86::cx; break;
      case InstSpec::kOpDx: operands[i] = x86::dx; break;
      case InstSpec::kOpBx: operands[i] = x86::bx; break;
      case InstSpec::kOpEax: operands[i] = x86::eax; break;
      case InstSpec::kOpEcx: operands[i] = x86::ecx; break;
      case InstSpec::kOpEdx: operands[i] = x86::edx; break;
      case InstSpec::kOpEbx: operands[i] = x86::ebx; break;
      case InstSpec::kOpRax: operands[i] = x86::rax; break;
      case InstSpec::kOpRcx: operands[i] = x86::rcx; break;
      case InstSpec::kOpRdx: operands[i] = x86::rdx; break;
      case InstSpec::kOpRbx: operands[i] = x86::rbx; break;
      case InstSpec::kOpMm: operands[i] = x86::mm(i); break;
      case InstSpec::kOpXmm: operands[i] = x86::xmm(i); break;
      case InstSpec::kOpXmm0: operands[i] = x86::xmm0; break;
      case InstSpec::kOpYmm: operands[i] = x86::ymm(i); break;
      case InstSpec::kOpZmm: operands[i] = x86::zmm(i); break;
      case InstSpec::kOpKReg: operands[i] = x86::k(i); break;
      case InstSpec::kOpImm8: operands[i] = Imm(1); break;
      case InstSpec::kOpImm16: operands[i] = Imm(1); break;
      case InstSpec::kOpImm32: operands[i] = Imm(1); break;
      case InstSpec::kOpImm64: operands[i] = Imm(1); break;
      case InstSpec::kOpMem8: operands[i] = x86::byte_ptr(p); break;
      case InstSpec::kOpMem16: operands[i] = x86::word_ptr(p); break;
      case InstSpec::kOpMem32: operands[i] = x86::dword_ptr(p); break;
      case InstSpec::kOpMem64: operands[i] = x86::qword_ptr(p); break;
      case InstSpec::kOpMem128: operands[i] = x86::xmmword_ptr(p); break;
      case InstSpec::kOpMem256: operands[i] = x86::ymmword_ptr(p); break;
      case InstSpec::kOpMem512: operands[i] = x86::zmmword_ptr(p); break;
      case InstSpec::kOpVm32x: operands[i] = x86::ptr(p, x86::xmm7); break;
      case InstSpec::kOpVm32y: operands[i] = x86::ptr(p, x86::ymm7); break;
      case InstSpec::kOpVm32z: operands[i] = x86::ptr(p, x86::zmm7); break;
      case InstSpec::kOpVm64x: operands[i] = x86::ptr(p, x86::xmm7); break;
      case InstSpec::kOpVm64y: operands[i] = x86::ptr(p, x86::ymm7); break;
      case InstSpec::kOpVm64z: operands[i] = x86::ptr(p, x86::zmm7); break;
      default:
        break;
    }
  }
}

static bool is_write_only(Arch arch, InstId inst_id, InstSpec spec) {
  Operand operands[6] {};
  inst_spec_to_operand_array(arch, operands, spec);
  InstRWInfo rw_info {};

  InstAPI::query_rw_info(arch, BaseInst(inst_id), operands, spec.count(), &rw_info);
  if (rw_info.op_count() > 0 && rw_info.operands()[0].is_write_only())
    return true;

  return false;
}

static const char* inst_spec_op_as_string(uint32_t instSpecOp) {
  switch (instSpecOp) {
    case InstSpec::kOpNone : return "none";
    case InstSpec::kOpRel  : return "rel";

    case InstSpec::kOpAl   : return "al";
    case InstSpec::kOpBl   : return "bl";
    case InstSpec::kOpCl   : return "cl";
    case InstSpec::kOpDl   : return "dl";
    case InstSpec::kOpGpb  : return "r8";

    case InstSpec::kOpAx   : return "ax";
    case InstSpec::kOpBx   : return "bx";
    case InstSpec::kOpCx   : return "cx";
    case InstSpec::kOpDx   : return "dx";
    case InstSpec::kOpGpw  : return "r16";

    case InstSpec::kOpEax  : return "eax";
    case InstSpec::kOpEbx  : return "ebx";
    case InstSpec::kOpEcx  : return "ecx";
    case InstSpec::kOpEdx  : return "edx";
    case InstSpec::kOpGpd  : return "r32";

    case InstSpec::kOpRax  : return "rax";
    case InstSpec::kOpRbx  : return "rbx";
    case InstSpec::kOpRcx  : return "rcx";
    case InstSpec::kOpRdx  : return "rdx";
    case InstSpec::kOpGpq  : return "r64";

    case InstSpec::kOpMm   : return "mm";

    case InstSpec::kOpXmm0 : return "xmm0";
    case InstSpec::kOpXmm  : return "xmm";
    case InstSpec::kOpYmm  : return "ymm";
    case InstSpec::kOpZmm  : return "zmm";

    case InstSpec::kOpKReg : return "k";

    case InstSpec::kOpImm8 : return "i8";
    case InstSpec::kOpImm16: return "i16";
    case InstSpec::kOpImm32: return "i32";
    case InstSpec::kOpImm64: return "i64";

    case InstSpec::kOpMem8: return "m8";
    case InstSpec::kOpMem16: return "m16";
    case InstSpec::kOpMem32: return "m32";
    case InstSpec::kOpMem64: return "m64";
    case InstSpec::kOpMem128: return "m128";
    case InstSpec::kOpMem256: return "m256";
    case InstSpec::kOpMem512: return "m512";

    case InstSpec::kOpVm32x: return "vm32x";
    case InstSpec::kOpVm32y: return "vm32y";
    case InstSpec::kOpVm32z: return "vm32z";
    case InstSpec::kOpVm64x: return "vm64x";
    case InstSpec::kOpVm64y: return "vm64y";
    case InstSpec::kOpVm64z: return "vm64z";

    default:
      return "(invalid)";
  }
}

static void fillOpArray(Operand* dst, uint32_t count, const Operand_& op) {
  for (uint32_t i = 0; i < count; i++)
    dst[i] = op;
}

static void fillMemArray(Operand* dst, uint32_t count, const x86::Mem& op, uint32_t increment) {
  x86::Mem mem(op);
  for (uint32_t i = 0; i < count; i++) {
    dst[i] = mem;
    mem.add_offset(increment);
  }
}

static void fillRegArray(Operand* dst, uint32_t count, uint32_t rStart, uint32_t rInc, uint32_t reg_mask, uint32_t rSign) {
  uint32_t rIdCount = 0;
  uint8_t rIdArray[64];

  // Fill rIdArray[] array from the bits as specified by `reg_mask`.
  asmjit::Support::BitWordIterator<uint32_t> reg_mask_iterator(reg_mask);
  while (reg_mask_iterator.has_next()) {
    uint32_t id = reg_mask_iterator.next();
    rIdArray[rIdCount++] = uint8_t(id);
  }

  uint32_t rId = rStart % rIdCount;
  for (uint32_t i = 0; i < count; i++) {
    dst[i] = Reg(OperandSignature{rSign}, rIdArray[rId]);
    rId = (rId + rInc) % rIdCount;
  }
}

static void fillImmArray(Operand* dst, uint32_t count, uint64_t start, uint64_t inc, uint64_t maxValue) {
  uint64_t n = start;

  for (uint32_t i = 0; i < count; i++) {
    dst[i] = Imm(n);
    n = (n + inc) % (maxValue + 1);
  }
}

// Round the result (either cycles or latency) to something nicer than `0.8766`.
static double round_result(double x) {
  double n = double(int(x));
  double f = x - n;

  // Ceil if the number of cycles is greater than 50.
  if (n >= 50.0)
    f = (f > 0.12) ? 1.0 : 0.0;
  else if (f <= 0.12)
    f = 0.00;
  else if (f <= 0.22)
    f = n > 1.0 ? 0.00 : 0.20;
  else if (f >= 0.22 && f <= 0.28)
    f = 0.25;
  else if (f >= 0.27 && f <= 0.38)
    f = 0.33;
  else if (f <= 0.57)
    f = 0.50;
  else if (f <= 0.7)
    f = 0.66;
  else
    f = 1.00;

  return n + f;
}

// ============================================================================
// [cult::InstBench]
// ============================================================================

InstBench::InstBench(App* app)
  : BaseBench(app),
    _inst_id(0),
    _inst_spec(),
    _n_unroll(64),
    _n_parallel(0),
    _gather_data{},
    _gather_data_size(4096) {}

InstBench::~InstBench() {
  free_gather_data(32);
  free_gather_data(64);
}

const void* InstBench::ensure_gather_data(uint32_t element_size, bool is_aligned) {
  uint32_t index = element_size == 32 ? 0 : 1;

  size_t dataSize = _gather_data_size * (element_size / 8);

  if (!_gather_data[index]) {
    _gather_data[index] = malloc(dataSize * 2 + 8);

    Random rg(123456789);
    uint32_t mask = _gather_data_size - 1;

    if (element_size == 32) {
      uint32_t* d = static_cast<uint32_t*>(_gather_data[index]);
      for (uint32_t i = 0; i < _gather_data_size; i++) {
        d[i] = (rg.next_uint32() & mask) * 4;
      }

      // Unaligned data.
      memcpy(reinterpret_cast<char*>(d) + dataSize + 1, d, dataSize);
    }
    else {
      uint64_t* d = static_cast<uint64_t*>(_gather_data[index]);
      for (uint32_t i = 0; i < _gather_data_size; i++) {
        d[i] = (rg.next_uint32() & mask) * 8;
      }

      // Unaligned data.
      memcpy(reinterpret_cast<char*>(d) + dataSize + 1, d, dataSize);
    }
  }

  return is_aligned ? _gather_data[index] : static_cast<char*>(_gather_data[index]) + dataSize + 1;
}

void InstBench::free_gather_data(uint32_t element_size) {
  uint32_t index = element_size == 32 ? 0 : 1;

  free(_gather_data[index]);
  _gather_data[index] = nullptr;
}

uint32_t InstBench::local_stack_size() const {
  return 64 * 65;
}

void InstBench::run() {
  JSONBuilder& json = _app->json();

  if (_app->verbose()) {
    printf("Benchmark (latency & reciprocal throughput):\n");
    uint64_t tsc_freq = CpuUtils::get_tsc_freq();
    if (tsc_freq) {
      printf("TSC Frequency: %llu\n", (unsigned long long)(tsc_freq));
    }
  }

  json.before_record()
      .add_key("instructions")
      .open_array();

  uint32_t instStart = 1;
  uint32_t instEnd = x86::Inst::_kIdCount;

  if (_app->_single_inst_id) {
    instStart = _app->_single_inst_id;
    instEnd = instStart + 1;
  }

  for (InstId inst_id = instStart; inst_id < instEnd; inst_id++) {
    std::vector<InstSpec> specs;
    classify(specs, inst_id);

    /*
    if (specs.size() == 0) {
      asmjit::String name;
      InstAPI::instIdToString(Environment::kArchHost, inst_id, InstStringifyOptions::kNone, name);
      printf("MISSING SPEC: %s\n", name.data());
    }
    */

    for (size_t i = 0; i < specs.size(); i++) {
      InstSpec inst_spec = specs[i];
      uint32_t op_count = inst_spec.count();
      uint32_t mem_op = inst_spec.mem_op();

      std::vector<uint32_t> alignments;
      if (mem_op && mem_op != InstSpec::kOpMem8 && is_safe_unaligned(inst_id, mem_op)) {
        alignments.push_back(0u);
        alignments.push_back(1u);
      }
      else {
        alignments.push_back(0u);
      }

      for (uint32_t alignment : alignments) {
        StringTmp<256> sb;
        if (inst_id == x86::Inst::kIdCall) {
          sb.append("call+ret");
        }
        else {
          InstAPI::inst_id_to_string(Arch::kHost, inst_id, InstStringifyOptions::kNone, sb);
        }

        for (uint32_t i = 0; i < op_count; i++) {
          if (i == 0)
            sb.append(' ');
          else if (inst_id == x86::Inst::kIdLea)
            sb.append(i == 1 ? ", [" : " + ");
          else
            sb.append(", ");

          sb.append(inst_spec_op_as_string(inst_spec.get(i)));

          if (i == 0 && (is_gather_inst(inst_id) || is_scatter_inst(inst_id)) && op_count == 2) {
            sb.append(" {k}");
          }

          if (i == 2 && inst_spec.is_lea_scale())
            sb.append(" * N");

          if (inst_id == x86::Inst::kIdLea && i == op_count - 1)
            sb.append(']');

          if (alignments.size() != 1) {
            if (InstSpec::is_mem_op(inst_spec.get(i)) || InstSpec::is_vm_op(inst_spec.get(i))) {
              if (alignment == 0)
                sb.append(" {a}");
              else
                sb.append(" {u}");
            }
          }
        }

        double overheadLat = test_instruction(inst_id, inst_spec, 0, alignment, true);
        double overheadRcp = test_instruction(inst_id, inst_spec, 1, alignment, true);

        double lat = test_instruction(inst_id, inst_spec, 0, alignment, false);
        double rcp = test_instruction(inst_id, inst_spec, 1, alignment, false);

        lat = std::max<double>(lat - overheadLat, 0);
        rcp = std::max<double>(rcp - overheadRcp, 0);

        if (_app->_round) {
          lat = round_result(lat);
          rcp = round_result(rcp);
        }

        // Some tests are probably skewed. If this happens the latency is the throughput.
        if (rcp > lat)
          lat = rcp;

        if (_app->verbose())
          printf("  %-40s: Lat:%7.2f Rcp:%7.2f\n", sb.data(), lat, rcp);

        json.before_record()
            .open_object()
            .add_key("inst").add_string(sb.data()).align_to(54)
            .add_key("lat").add_doublef("%7.2f", lat)
            .add_key("rcp").add_doublef("%7.2f", rcp)
            .close_object();
      }
    }
  }

  if (_app->verbose())
    printf("\n");

  json.close_array(true);
}

void InstBench::classify(std::vector<InstSpec>& dst, InstId inst_id) {
  using namespace asmjit;

  if (is_ignored_inst(inst_id))
    return;

  // Special cases.
  if (inst_id == x86::Inst::kIdCpuid    ||
      inst_id == x86::Inst::kIdEmms     ||
      inst_id == x86::Inst::kIdFemms    ||
      inst_id == x86::Inst::kIdLfence   ||
      inst_id == x86::Inst::kIdMfence   ||
      inst_id == x86::Inst::kIdRdtsc    ||
      inst_id == x86::Inst::kIdRdtscp   ||
      inst_id == x86::Inst::kIdSfence   ||
      inst_id == x86::Inst::kIdXgetbv   ||
      inst_id == x86::Inst::kIdVzeroall ||
      inst_id == x86::Inst::kIdVzeroupper) {
    if (can_run(inst_id))
      dst.push_back(InstSpec::pack(0));
    return;
  }

  if (inst_id == x86::Inst::kIdCall) {
    dst.push_back(InstSpec::pack(InstSpec::kOpRel));
    if (is_64bit()) {
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq));
      dst.push_back(InstSpec::pack(InstSpec::kOpMem64));
    }
    else {
      dst.push_back(InstSpec::pack(InstSpec::kOpGpd));
      dst.push_back(InstSpec::pack(InstSpec::kOpMem32));
    }
    return;
  }

  if (inst_id == x86::Inst::kIdJmp) {
    dst.push_back(InstSpec::pack(InstSpec::kOpRel));
    return;
  }

  if (inst_id == x86::Inst::kIdLea) {
    dst.push_back(InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd));
    dst.push_back(InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpImm8));
    dst.push_back(InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpImm32));
    dst.push_back(InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpGpd));
    dst.push_back(InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpGpd).lea_scale());
    dst.push_back(InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpImm8));
    dst.push_back(InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpImm32));
    dst.push_back(InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpImm8).lea_scale());
    dst.push_back(InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpImm32).lea_scale());

    if (is_64bit()) {
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq));
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpImm8));
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpImm32));
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpGpq));
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpGpq).lea_scale());
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpImm8));
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpImm32));
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpImm8).lea_scale());
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpImm32).lea_scale());
    }
    return;
  }

  // Common cases based on instruction signatures.
  x86::InstDB::Mode mode = x86::InstDB::Mode::kNone;
  x86::InstDB::OpFlags op_filter =
    x86::InstDB::OpFlags::kRegGpbLo |
    x86::InstDB::OpFlags::kRegGpw   |
    x86::InstDB::OpFlags::kRegGpd   |
    x86::InstDB::OpFlags::kRegGpq   |
    x86::InstDB::OpFlags::kRegXmm   |
    x86::InstDB::OpFlags::kRegYmm   |
    x86::InstDB::OpFlags::kRegZmm   |
    x86::InstDB::OpFlags::kRegMm    |
    x86::InstDB::OpFlags::kRegKReg  |
    x86::InstDB::OpFlags::kImmMask  |
    x86::InstDB::OpFlags::kMemMask  |
    x86::InstDB::OpFlags::kVmMask;

  if (Arch::kHost == Arch::kX86) {
    mode = x86::InstDB::Mode::kX86;
    op_filter &= ~x86::InstDB::OpFlags::kRegGpq;
  }
  else {
    mode = x86::InstDB::Mode::kX64;
  }

  const x86::InstDB::InstInfo& inst_info = x86::InstDB::inst_info_by_id(inst_id);
  Span<const x86::InstDB::InstSignature> inst_signatures = inst_info.inst_signatures();
  std::set<InstSpec> known;

  // Iterate over all signatures and build the instruction we want to test.
  for (const x86::InstDB::InstSignature& inst_signature : inst_signatures) {
    if (!inst_signature.supports_mode(mode))
      continue;

    InstSignatureIterator it(inst_signature, op_filter);
    while (it.is_valid()) {
      uint32_t op_count = it.op_count();
      InstSpec inst_spec {};

      bool skip = false;
      bool vec = false;
      uint32_t immCount = 0;

      for (uint32_t op_index = 0; op_index < op_count; op_index++) {
        x86::InstDB::OpFlags op_flags = it.op_mask(op_index);
        const x86::InstDB::OpSignature* op_sgn = it.op_sgn(op_index);

        if (Support::test(op_flags, x86::InstDB::OpFlags::kRegMask)) {
          uint32_t regId = 0;
          if (Support::is_power_of_2(op_sgn->reg_mask()))
            regId = Support::ctz(op_sgn->reg_mask());

          switch (op_flags) {
            case x86::InstDB::OpFlags::kRegGpbLo: inst_spec.set(op_index, InstSpec::kOpGpb); break;
            case x86::InstDB::OpFlags::kRegGpbHi: inst_spec.set(op_index, InstSpec::kOpGpb); break;
            case x86::InstDB::OpFlags::kRegGpw  : inst_spec.set(op_index, InstSpec::kOpGpw); break;
            case x86::InstDB::OpFlags::kRegGpd  : inst_spec.set(op_index, InstSpec::kOpGpd); break;
            case x86::InstDB::OpFlags::kRegGpq  : inst_spec.set(op_index, InstSpec::kOpGpq); break;
            case x86::InstDB::OpFlags::kRegXmm  : inst_spec.set(op_index, InstSpec::kOpXmm); vec = true; break;
            case x86::InstDB::OpFlags::kRegYmm  : inst_spec.set(op_index, InstSpec::kOpYmm); vec = true; break;
            case x86::InstDB::OpFlags::kRegZmm  : inst_spec.set(op_index, InstSpec::kOpZmm); vec = true; break;
            case x86::InstDB::OpFlags::kRegMm   : inst_spec.set(op_index, InstSpec::kOpMm); vec = true; break;
            case x86::InstDB::OpFlags::kRegKReg : inst_spec.set(op_index, InstSpec::kOpKReg); vec = true; break;
            default:
              printf("[!!] Unknown register operand: OpMask=0x%016llX\n", (unsigned long long)op_flags);
              skip = true;
              break;
          }

          if (Support::is_power_of_2(op_sgn->reg_mask())) {
            switch (op_flags) {
              case x86::InstDB::OpFlags::kRegGpbLo: inst_spec.set(op_index, InstSpec::kOpAl + regId); break;
              case x86::InstDB::OpFlags::kRegGpbHi: inst_spec.set(op_index, InstSpec::kOpAl + regId); break;
              case x86::InstDB::OpFlags::kRegGpw  : inst_spec.set(op_index, InstSpec::kOpAx + regId); break;
              case x86::InstDB::OpFlags::kRegGpd  : inst_spec.set(op_index, InstSpec::kOpEax + regId); break;
              case x86::InstDB::OpFlags::kRegGpq  : inst_spec.set(op_index, InstSpec::kOpRax + regId); break;
              case x86::InstDB::OpFlags::kRegXmm  : inst_spec.set(op_index, InstSpec::kOpXmm0); break;
              default:
                printf("[!!] Unknown register operand: OpMask=0x%016llX\n", (unsigned long long)op_flags);
                skip = true;
                break;
            }
          }
        }
        else if (Support::test(op_flags, x86::InstDB::OpFlags::kMemMask)) {
          // The assembler would just swap the operands, so if memory is first or second it doesn't matter.
          if (op_index == 0 && inst_id == x86::Inst::kIdXchg)
            skip = true;

          switch (op_flags) {
            case x86::InstDB::OpFlags::kMem8: inst_spec.set(op_index, InstSpec::kOpMem8); break;
            case x86::InstDB::OpFlags::kMem16: inst_spec.set(op_index, InstSpec::kOpMem16); break;
            case x86::InstDB::OpFlags::kMem32: inst_spec.set(op_index, InstSpec::kOpMem32); break;
            case x86::InstDB::OpFlags::kMem64: inst_spec.set(op_index, InstSpec::kOpMem64); break;
            case x86::InstDB::OpFlags::kMem128: inst_spec.set(op_index, InstSpec::kOpMem128); break;
            case x86::InstDB::OpFlags::kMem256: inst_spec.set(op_index, InstSpec::kOpMem256); break;
            case x86::InstDB::OpFlags::kMem512: inst_spec.set(op_index, InstSpec::kOpMem512); break;
            default:
              skip = true;
              break;
          }
        }
        else if (Support::test(op_flags, x86::InstDB::OpFlags::kVmMask)) {
          switch (op_flags) {
            case x86::InstDB::OpFlags::kVm32x: inst_spec.set(op_index, InstSpec::kOpVm32x); break;
            case x86::InstDB::OpFlags::kVm32y: inst_spec.set(op_index, InstSpec::kOpVm32y); break;
            case x86::InstDB::OpFlags::kVm32z: inst_spec.set(op_index, InstSpec::kOpVm32z); break;
            case x86::InstDB::OpFlags::kVm64x: inst_spec.set(op_index, InstSpec::kOpVm64x); break;
            case x86::InstDB::OpFlags::kVm64y: inst_spec.set(op_index, InstSpec::kOpVm64y); break;
            case x86::InstDB::OpFlags::kVm64z: inst_spec.set(op_index, InstSpec::kOpVm64z); break;
            default:
              skip = true;
              break;
          }
        }
        else if (Support::test(op_flags, x86::InstDB::OpFlags::kImmMask)) {
          if (Support::test(op_flags, x86::InstDB::OpFlags::kImmI64 | x86::InstDB::OpFlags::kImmU64))
            inst_spec.set(op_index, InstSpec::kOpImm64);
          else if (Support::test(op_flags, x86::InstDB::OpFlags::kImmI32 | x86::InstDB::OpFlags::kImmU32))
            inst_spec.set(op_index, InstSpec::kOpImm32);
          else if (Support::test(op_flags, x86::InstDB::OpFlags::kImmI16 | x86::InstDB::OpFlags::kImmU16))
            inst_spec.set(op_index, InstSpec::kOpImm16);
          else
            inst_spec.set(op_index, InstSpec::kOpImm8);
        }
        else {
          skip = true;
        }
      }

      if (!skip) {
        if (vec || is_safe_gp_inst(inst_id)) {
          BaseInst base_inst(inst_id, InstOptions::kNone);
          Operand operands[6] {};

          inst_spec_to_operand_array(Arch::kHost, operands, inst_spec);
          if (_can_run(base_inst, operands, op_count)) {
            if (known.find(inst_spec) == known.end()) {
              known.insert(inst_spec);
              dst.push_back(inst_spec);
            }
          }
        }
      }

      it.next();
    }
  }
}

bool InstBench::is_implicit(InstId inst_id) {
  const x86::InstDB::InstInfo& inst_info = x86::InstDB::inst_info_by_id(inst_id);
  Span<const x86::InstDB::InstSignature> inst_signatures = inst_info.inst_signatures();

  for (const x86::InstDB::InstSignature& inst_signature : inst_signatures) {
    if (inst_signature.has_implicit_operands()) {
      return true;
    }
  }

  return false;
}

bool InstBench::_can_run(const BaseInst& inst, const Operand_* operands, uint32_t count) const {
  using namespace asmjit;

  if (inst.inst_id() == x86::Inst::kIdNone)
    return false;

  if (InstAPI::validate(Arch::kHost, inst, operands, count) != kErrorOk)
    return false;

  CpuFeatures features;
  if (InstAPI::query_features(Arch::kHost, inst, operands, count, &features) != kErrorOk)
    return false;

  if (!_cpuInfo.features().has_all(features))
    return false;

  return true;
}

uint32_t InstBench::num_iter_by_inst_id(InstId inst_id) const {
  switch (inst_id) {
    // Return low number for instructions that are really slow.
    case x86::Inst::kIdCpuid:
    case x86::Inst::kIdRdrand:
    case x86::Inst::kIdRdseed:
      return 4;

    default:
      return 160;
  }
}

double InstBench::test_instruction(InstId inst_id, InstSpec inst_spec, uint32_t parallel, uint32_t mem_alignment, bool overhead_only) {
  _inst_id = inst_id;
  _inst_spec = inst_spec;
  _n_parallel = parallel ? 6 : 1;
  _overhead_only = overhead_only;
  _mem_alignment = mem_alignment;

  Func func = compile_func();
  if (!func) {
    String name;
    InstAPI::inst_id_to_string(Arch::kHost, inst_id, InstStringifyOptions::kNone, name);
    printf("FAILED to compile function for '%s' instruction\n", name.data());
    return -1.0;
  }

  uint32_t nIter = num_iter_by_inst_id(_inst_id);

  // Consider a significant improvement 0.05 cycles per instruction (0.2 cycles in fast mode).
  uint32_t kSignificantImprovement = uint32_t(double(nIter) * (_app->_estimate ? 0.25 : 0.04));

  // If we called the function N times without a significant improvement we terminate the test.
  uint32_t kMaximumImprovementTries = _app->_estimate ? 1000 : 50000;

  constexpr uint32_t kMaxIterationCount = 5000000;

  uint64_t best;
  func(nIter, &best);

  uint64_t previousBest = best;
  uint32_t improvementTries = 0;

  for (uint32_t i = 0; i < kMaxIterationCount; i++) {
    uint64_t n;
    func(nIter, &n);

    best = std::min(best, n);
    if (n < previousBest) {
      if (previousBest - n >= kSignificantImprovement) {
        previousBest = n;
        improvementTries = 0;
      }
    }
    else {
      improvementTries++;
    }

    if (improvementTries >= kMaximumImprovementTries)
      break;
  }

  release_func(func);
  return double(best) / (double(nIter * _n_unroll));
}

void InstBench::before_body(x86::Assembler& a) {
  if (_inst_id == x86::Inst::kIdDiv || _inst_id == x86::Inst::kIdIdiv) {
    fill_memory_u32(a, a.zsp(), 0x03030303u, local_stack_size() / 4);
  }
  else if (is_gather_inst(_inst_id)) {
    x86::Gp gs_base = a.zdi();
    a.mov(gs_base, uintptr_t(ensure_gather_data(gatherIndexSize(_inst_id), _mem_alignment == 0)));

    if (_inst_spec.count() == 2) {
      // AVX-512 gather has only two operands, one operand must be {k} register passed as extraReg.
      a.kxnorq(x86::k7, x86::k7, x86::k7);

      uint32_t regCount = is_64bit() ? 32 : 8;
      for (uint32_t i = 0; i < regCount; i++) {
        if (i == 7)
          continue;
        a.vmovdqu32(x86::zmm(i), x86::ptr(gs_base, i * 32));
      }
    }
    else {
      a.vpcmpeqb(x86::ymm7, x86::ymm7, x86::ymm7);

      uint32_t regCount = is_64bit() ? 16 : 8;
      for (uint32_t i = 0; i < regCount; i++) {
        if (i == 6 || i == 7)
          continue;
        a.vmovdqu(x86::ymm(i), x86::ptr(gs_base, i * 32));
      }
    }
  }
  else if (is_scatter_inst(_inst_id)) {
    static const uint8_t sequence[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

    x86::Gp gs_base = a.zdi();
    a.mov(gs_base, a.zsp());
    a.kxnorq(x86::k7, x86::k7, x86::k7);
    a.mov(a.zsi(), uintptr_t(sequence));

    if (scatter_index_size(_inst_id) == 32) {
      a.vpmovzxbd(x86::zmm6, x86::ptr(a.zsi()));
      a.mov(x86::esi, 128);
      a.vpbroadcastd(x86::zmm7, x86::esi);
      a.vpmulld(x86::zmm7, x86::zmm7, x86::zmm6);
    }
    else {
      a.vpmovzxbq(x86::zmm6, x86::ptr(a.zsi()));
      a.mov(x86::esi, 128);
      a.movd(x86::xmm7, x86::esi);
      a.vpbroadcastq(x86::zmm7, x86::xmm7);
      a.vpmullq(x86::zmm7, x86::zmm7, x86::zmm6);
    }
    a.vpxord(x86::xmm6, x86::xmm6, x86::xmm6);
  }
  else {
    fill_memory_u32(a, a.zsp(), 0u, local_stack_size() / 4);
    if (x86::InstDB::inst_info_by_id(_inst_id).is_vec()) {
      uint32_t regs = Arch::kHost == Arch::kX86 ? 8 : 16;
      if (x86::InstDB::inst_info_by_id(_inst_id).is_sse()) {
        for (uint32_t i = 0; i < regs; i++)
          a.xorps(x86::xmm(i), x86::xmm(i));
      }
      else {
        for (uint32_t i = 0; i < regs; i++)
          a.vpxor(x86::xmm(i), x86::xmm(i), x86::xmm(i));
      }
    }
  }
}

void InstBench::compile_body(x86::Assembler& a, x86::Gp reg_cnt) {
  using namespace asmjit;

  InstId inst_id = _inst_id;
  const x86::InstDB::InstInfo& inst_info = x86::InstDB::inst_info_by_id(inst_id);

  uint32_t reg_mask[32] {};

  uint32_t generic_reg_mask = is_64bit() ? 0xFFFFu : 0xFFu;

  reg_mask[uint32_t(RegGroup::kGp)] = generic_reg_mask & ~Support::bit_mask<RegMask>(x86::Gp::kIdSp, reg_cnt.id());
  reg_mask[uint32_t(RegGroup::kVec)] = generic_reg_mask;
  reg_mask[uint32_t(RegGroup::kMask)] = 0xFE;
  reg_mask[uint32_t(RegGroup::kX86_MM)] = 0xFF;

  x86::Gp gs_base;
  x86::Vec gathereg_mask;

  // AVX-512 gather has only two operands, one operand must be {k} register passed as extraReg.
  bool is_gather = is_gather_inst(inst_id);
  bool is_gather_avx512 = is_gather && _inst_spec.count() == 2;
  bool is_scatter_avx512 = is_scatter_inst(inst_id);

  uint32_t gs_index_shift = 0;

  if (is_gather || is_scatter_avx512) {
    gs_base = a.zdi();
    reg_mask[uint32_t(RegGroup::kGp)] &= ~Support::bit_mask<RegMask>(gs_base.id());
    reg_mask[uint32_t(RegGroup::kVec)] &= ~Support::bit_mask<RegMask>(6, 7);
    reg_mask[uint32_t(RegGroup::kMask)] &= ~Support::bit_mask<RegMask>(6, 7);

    if (is_64bit())
      reg_mask[uint32_t(RegGroup::kVec)] |= is_gather_avx512 ? 0xFFFFFF00u : 0x0000FF00u;

    if (!is_gather_avx512 && !is_scatter_avx512) {
      gathereg_mask = x86::ymm7;
    }
  }

  Operand* ox = static_cast<Operand*>(::calloc(1, sizeof(Operand) * _n_unroll));
  Operand* o0 = static_cast<Operand*>(::calloc(1, sizeof(Operand) * _n_unroll));
  Operand* o1 = static_cast<Operand*>(::calloc(1, sizeof(Operand) * _n_unroll));
  Operand* o2 = static_cast<Operand*>(::calloc(1, sizeof(Operand) * _n_unroll));
  Operand* o3 = static_cast<Operand*>(::calloc(1, sizeof(Operand) * _n_unroll));
  Operand* o4 = static_cast<Operand*>(::calloc(1, sizeof(Operand) * _n_unroll));
  Operand* o5 = static_cast<Operand*>(::calloc(1, sizeof(Operand) * _n_unroll));

  bool is_parallel = _n_parallel > 1;
  uint32_t i;
  uint32_t op_count = _inst_spec.count();
  uint32_t regCount = op_count;

  int32_t misalignment = 0;

  if (_mem_alignment != 0)
    misalignment = 1;

  while (regCount && _inst_spec.get(regCount - 1) >= InstSpec::kOpImm8 && _inst_spec.get(regCount - 1) < InstSpec::kOpVm32x)
    regCount--;

  for (i = 0; i < regCount; i++) {
    switch (_inst_spec.get(i)) {
      case InstSpec::kOpAl:
      case InstSpec::kOpAx:
      case InstSpec::kOpEax:
      case InstSpec::kOpRax:
        reg_mask[uint32_t(RegGroup::kGp)] &= ~Support::bit_mask<RegMask>(x86::Gp::kIdAx);
        break;

      case InstSpec::kOpBl:
      case InstSpec::kOpBx:
      case InstSpec::kOpEbx:
      case InstSpec::kOpRbx:
        reg_mask[uint32_t(RegGroup::kGp)] &= ~Support::bit_mask<RegMask>(x86::Gp::kIdBx);
        break;

      case InstSpec::kOpCl:
      case InstSpec::kOpCx:
      case InstSpec::kOpEcx:
      case InstSpec::kOpRcx:
        reg_mask[uint32_t(RegGroup::kGp)] &= ~Support::bit_mask<RegMask>(x86::Gp::kIdCx);
        break;

      case InstSpec::kOpDl:
      case InstSpec::kOpDx:
      case InstSpec::kOpEdx:
      case InstSpec::kOpRdx:
        reg_mask[uint32_t(RegGroup::kGp)] &= ~Support::bit_mask<RegMask>(x86::Gp::kIdDx);
        break;
    }
  }

  if (is_gather_avx512 || is_scatter_avx512) {
    fillRegArray(ox, _n_unroll, 1, is_parallel ? 1 : 0, reg_mask[uint32_t(RegGroup::kMask)], RegTraits<RegType::kMask>::kSignature);
  }
  else if (is_gather) {
    // Don't count mask in AVX2 gather case.
    op_count--;
    regCount--;
  }

  for (i = 0; i < op_count; i++) {
    uint32_t spec = _inst_spec.get(i);
    Operand* dst = i == 0 ? o0 :
                   i == 1 ? o1 :
                   i == 2 ? o2 :
                   i == 3 ? o3 :
                   i == 4 ? o4 : o5;

    uint32_t rStart = 0;
    uint32_t rInc = 1;

    switch (regCount) {
      // Patterns we want to generate:
      //   - Sequential:
      //       INST v0
      //       INST v0
      //       INST v0
      //       ...
      //   - Parallel:
      //       INST v0
      //       INST v1
      //       INST v2
      //       ...
      case 1:
        if (!is_parallel)
          rInc = 0;
        break;

      // Patterns we want to generate:
      //   - Sequential:
      //       INST v1, v0
      //       INST v2, v1
      //       INST v3, v2
      //       ...
      //   - Parallel:
      //       INST v0, v1
      //       INST v1, v2
      //       INST v2, v3
      //       ...
      case 2:
        if (!is_parallel)
          rStart = (i == 0) ? 1 : 0;
        else
          rStart = (i == 0) ? 0 : 1;
        break;

      // Patterns we want to generate:
      //   - Sequential:
      //       INST v1, v1, v0
      //       INST v2, v2, v1
      //       INST v3, v3, v2
      //       ...
      //   - Parallel:
      //       INST v0, v0, v1
      //       INST v1, v1, v2
      //       INST v2, v2, v3
      //       ...
      case 3:
        if (inst_id == x86::Inst::kIdVfcmaddcph ||
            inst_id == x86::Inst::kIdVfmaddcph  ||
            inst_id == x86::Inst::kIdVfcmaddcsh ||
            inst_id == x86::Inst::kIdVfmaddcsh  ||
            inst_id == x86::Inst::kIdVfcmulcsh  ||
            inst_id == x86::Inst::kIdVfmulcsh   ||
            inst_id == x86::Inst::kIdVfcmulcph  ||
            inst_id == x86::Inst::kIdVfmulcph) {
          if (!is_parallel) {
            rStart = i == 0 ? 1 : 0;
          }
          else {
            rStart = i == 0 ? 0 : 1;
            rInc = 2;
          }
        }
        else {
          if (!is_parallel)
            rStart = (i < 2) ? 1 : 0;
          else
            rStart = (i < 2) ? 0 : 1;
        }
        break;

      // Patterns we want to generate:
      //   - Sequential:
      //       INST v2, v1, v1, v0, ...
      //       INST v3, v2, v2, v1, ...
      //       INST v4, v3, v3, v2, ...
      //       ...
      //   - Parallel:
      //       INST v0, v1, v1, v2, ...
      //       INST v1, v2, v2, v3, ...
      //       INST v2, v3, v3, v4, ...
      //       ...
      case 4:
      case 5:
      case 6:
        if (!is_parallel)
          rStart = (i < 1) ? 2 : (i < 3) ? 1 : 0;
        else
          rStart = (i < 1) ? 0 : (i < 3) ? 1 : 2;
        break;
    }

    switch (spec) {
      case InstSpec::kOpAl    : fillOpArray(dst, _n_unroll, x86::al); break;
      case InstSpec::kOpBl    : fillOpArray(dst, _n_unroll, x86::bl); break;
      case InstSpec::kOpCl    : fillOpArray(dst, _n_unroll, x86::cl); break;
      case InstSpec::kOpDl    : fillOpArray(dst, _n_unroll, x86::dl); break;
      case InstSpec::kOpAx    : fillOpArray(dst, _n_unroll, x86::ax); break;
      case InstSpec::kOpBx    : fillOpArray(dst, _n_unroll, x86::bx); break;
      case InstSpec::kOpCx    : fillOpArray(dst, _n_unroll, x86::cx); break;
      case InstSpec::kOpDx    : fillOpArray(dst, _n_unroll, x86::dx); break;
      case InstSpec::kOpEax   : fillOpArray(dst, _n_unroll, x86::eax); break;
      case InstSpec::kOpEbx   : fillOpArray(dst, _n_unroll, x86::ebx); break;
      case InstSpec::kOpEcx   : fillOpArray(dst, _n_unroll, x86::ecx); break;
      case InstSpec::kOpEdx   : fillOpArray(dst, _n_unroll, x86::edx); break;
      case InstSpec::kOpRax   : fillOpArray(dst, _n_unroll, x86::rax); break;
      case InstSpec::kOpRbx   : fillOpArray(dst, _n_unroll, x86::rbx); break;
      case InstSpec::kOpRcx   : fillOpArray(dst, _n_unroll, x86::rcx); break;
      case InstSpec::kOpRdx   : fillOpArray(dst, _n_unroll, x86::rdx); break;
      case InstSpec::kOpGpb   : fillRegArray(dst, _n_unroll, rStart, rInc, reg_mask[uint32_t(RegGroup::kGp)], RegTraits<RegType::kGp8Lo>::kSignature); break;
      case InstSpec::kOpGpw   : fillRegArray(dst, _n_unroll, rStart, rInc, reg_mask[uint32_t(RegGroup::kGp)], RegTraits<RegType::kGp16>::kSignature); break;
      case InstSpec::kOpGpd   : fillRegArray(dst, _n_unroll, rStart, rInc, reg_mask[uint32_t(RegGroup::kGp)], RegTraits<RegType::kGp32>::kSignature); break;
      case InstSpec::kOpGpq   : fillRegArray(dst, _n_unroll, rStart, rInc, reg_mask[uint32_t(RegGroup::kGp)], RegTraits<RegType::kGp64>::kSignature); break;

      case InstSpec::kOpXmm0  : fillOpArray(dst, _n_unroll, x86::xmm0); break;
      case InstSpec::kOpXmm   : fillRegArray(dst, _n_unroll, rStart, rInc, reg_mask[uint32_t(RegGroup::kVec)], RegTraits<RegType::kVec128>::kSignature); break;
      case InstSpec::kOpYmm   : fillRegArray(dst, _n_unroll, rStart, rInc, reg_mask[uint32_t(RegGroup::kVec)], RegTraits<RegType::kVec256>::kSignature); break;
      case InstSpec::kOpZmm   : fillRegArray(dst, _n_unroll, rStart, rInc, reg_mask[uint32_t(RegGroup::kVec)], RegTraits<RegType::kVec512>::kSignature); break;
      case InstSpec::kOpKReg  : fillRegArray(dst, _n_unroll, rStart, rInc, reg_mask[uint32_t(RegGroup::kMask)], RegTraits<RegType::kMask>::kSignature); break;
      case InstSpec::kOpMm    : fillRegArray(dst, _n_unroll, rStart, rInc, reg_mask[uint32_t(RegGroup::kX86_MM)], RegTraits<RegType::kX86_Mm >::kSignature); break;

      case InstSpec::kOpImm8  : fillImmArray(dst, _n_unroll, 0, 1    , 15        ); break;
      case InstSpec::kOpImm16 : fillImmArray(dst, _n_unroll, 1, 13099, 65535     ); break;
      case InstSpec::kOpImm32 : fillImmArray(dst, _n_unroll, 1, 19231, 2000000000); break;
      case InstSpec::kOpImm64 : fillImmArray(dst, _n_unroll, 1, 9876543219231, 0x0FFFFFFFFFFFFFFF); break;

      case InstSpec::kOpMem8  : fillMemArray(dst, _n_unroll, x86::byte_ptr(a.zsp(), misalignment), is_parallel ? 1 : 0); break;
      case InstSpec::kOpMem16 : fillMemArray(dst, _n_unroll, x86::word_ptr(a.zsp(), misalignment), is_parallel ? 2 : 0); break;
      case InstSpec::kOpMem32 : fillMemArray(dst, _n_unroll, x86::dword_ptr(a.zsp(), misalignment), is_parallel ? 4 : 0); break;
      case InstSpec::kOpMem64 : fillMemArray(dst, _n_unroll, x86::qword_ptr(a.zsp(), misalignment), is_parallel ? 8 : 0); break;
      case InstSpec::kOpMem128: fillMemArray(dst, _n_unroll, x86::xmmword_ptr(a.zsp(), misalignment), is_parallel ? 16 : 0); break;
      case InstSpec::kOpMem256: fillMemArray(dst, _n_unroll, x86::ymmword_ptr(a.zsp(), misalignment), is_parallel ? 32 : 0); break;
      case InstSpec::kOpMem512: fillMemArray(dst, _n_unroll, x86::zmmword_ptr(a.zsp(), misalignment), is_parallel ? 64 : 0); break;

      case InstSpec::kOpVm32x : fillRegArray(dst, _n_unroll, rStart, rInc, reg_mask[uint32_t(RegGroup::kVec)], RegTraits<RegType::kVec128>::kSignature); break;
      case InstSpec::kOpVm32y : fillRegArray(dst, _n_unroll, rStart, rInc, reg_mask[uint32_t(RegGroup::kVec)], RegTraits<RegType::kVec256>::kSignature); break;
      case InstSpec::kOpVm32z : fillRegArray(dst, _n_unroll, rStart, rInc, reg_mask[uint32_t(RegGroup::kVec)], RegTraits<RegType::kVec512>::kSignature); break;
      case InstSpec::kOpVm64x : fillRegArray(dst, _n_unroll, rStart, rInc, reg_mask[uint32_t(RegGroup::kVec)], RegTraits<RegType::kVec128>::kSignature); break;
      case InstSpec::kOpVm64y : fillRegArray(dst, _n_unroll, rStart, rInc, reg_mask[uint32_t(RegGroup::kVec)], RegTraits<RegType::kVec256>::kSignature); break;
      case InstSpec::kOpVm64z : fillRegArray(dst, _n_unroll, rStart, rInc, reg_mask[uint32_t(RegGroup::kVec)], RegTraits<RegType::kVec512>::kSignature); break;
    }
  }

  Label L_Body = a.new_label();
  Label L_End = a.new_label();
  Label L_SubFn = a.new_label();
  int stackOperationSize = 0;

  switch (inst_id) {
    case x86::Inst::kIdPush:
    case x86::Inst::kIdPop:
      // PUSH/POP modify the stack, we have to revert it in the inner loop.
      stackOperationSize = (_inst_spec.get(0) == InstSpec::kOpGpw ||
                            _inst_spec.get(0) == InstSpec::kOpMem16 ? 2 : int(a.register_size())) * int(_n_unroll);
      break;

    case x86::Inst::kIdCall:
      if (_inst_spec.get(0) == InstSpec::kOpGpd || _inst_spec.get(0) == InstSpec::kOpGpq) {
        a.lea(a.zax(), x86::ptr(L_SubFn));
      }
      else if (_inst_spec.get(0) == InstSpec::kOpMem32 || _inst_spec.get(0) == InstSpec::kOpMem64) {
        a.lea(a.zax(), x86::ptr(a.zsp(), misalignment));
        a.lea(a.zbx(), x86::ptr(L_SubFn));
        a.mov(x86::ptr(a.zax()), a.zbx());
      }
      break;

    case x86::Inst::kIdCpuid:
      a.xor_(x86::eax, x86::eax);
      a.xor_(x86::ecx, x86::ecx);
      break;

    case x86::Inst::kIdXgetbv:
      a.xor_(x86::ecx, x86::ecx);
      break;

    case x86::Inst::kIdBt:
    case x86::Inst::kIdBtc:
    case x86::Inst::kIdBtr:
    case x86::Inst::kIdBts:
      // Don't go beyond our buffer in mem case.
      a.mov(x86::eax, 3);
      a.mov(x86::ebx, 4);
      a.mov(x86::ecx, 5);
      a.mov(x86::edx, 6);
      a.mov(x86::esi, 7);
      a.mov(x86::edi, 1);

      if (is_64bit()) {
        a.mov(x86::r8, 2);
        a.mov(x86::r9, 3);
        a.mov(x86::r10, 4);
        a.mov(x86::r11, 5);
        a.mov(x86::r12, 6);
        a.mov(x86::r13, 7);
        a.mov(x86::r14, 0);
        a.mov(x86::r15, 1);
      }
      break;

    default:
      if (!is_gather && !is_scatter_avx512) {
        // This will cost us some cycles, however, we really want some predictable state.
        a.mov(x86::eax, 999);
        a.mov(x86::ebx, 49182);
        a.mov(x86::ecx, 3); // Used by divisions, should be a small number.
        a.mov(x86::edx, 1193833);
        a.mov(x86::esi, 192822);
        a.mov(x86::edi, 1);
      }
      break;
  }

  switch (inst_id) {
    case x86::Inst::kIdVmaskmovpd:
    case x86::Inst::kIdVmaskmovps:
    case x86::Inst::kIdVpmaskmovd:
    case x86::Inst::kIdVpmaskmovq:
      a.vpxor(x86::xmm0, x86::xmm0, x86::xmm0);
      a.vpcmpeqd(x86::ymm1, x86::ymm1, x86::ymm1);
      a.vpsrldq(x86::ymm1, x86::ymm1, 8);

      for (i = 0; i < _n_unroll; i++)
        o1[i].as<Reg>().set_id(1);
      break;

    case x86::Inst::kIdMaskmovq:
    case x86::Inst::kIdMaskmovdqu:
    case x86::Inst::kIdVmaskmovdqu:
      a.lea(a.zdi(), x86::ptr(a.zsp()));
      for (uint32_t i = 0; i < _n_unroll; i++)
        o2[i] = x86::ptr(a.zdi());
      break;
  }

  a.test(reg_cnt, reg_cnt);
  a.jz(L_End);

  a.align(AlignMode::kCode, 64);
  a.bind(L_Body);

  if (inst_id == x86::Inst::kIdPop && !_overhead_only)
    a.sub(a.zsp(), stackOperationSize);

  switch (inst_id) {
    case x86::Inst::kIdCall: {
      assert(op_count == 1);
      if (_overhead_only)
        break;

      for (uint32_t n = 0; n < _n_unroll; n++) {
        if (_inst_spec.get(0) == InstSpec::kOpRel)
          a.call(L_SubFn);
        else if (_inst_spec.get(0) == InstSpec::kOpGpd || _inst_spec.get(0) == InstSpec::kOpGpq)
          a.call(a.zax());
        else
          a.call(x86::ptr(a.zax()));
      }
      break;
    }

    case x86::Inst::kIdJmp: {
      assert(op_count == 1);
      if (_overhead_only)
        break;

      for (uint32_t n = 0; n < _n_unroll; n++) {
        Label x = a.new_label();
        a.jmp(x);
        a.bind(x);
      }
      break;
    }

    case x86::Inst::kIdDiv:
    case x86::Inst::kIdIdiv: {
      assert(op_count >= 2 && op_count <= 3);
      if (_overhead_only)
        break;

      if (op_count == 2) {
        for (uint32_t n = 0; n < _n_unroll; n++) {
          if (n == 0)
            a.mov(x86::eax, 127);
          a.emit(inst_id, x86::ax, x86::cl);

          if (n + 1 != _n_unroll)
            a.mov(x86::eax, 127);
        }
      }

      if (op_count == 3) {
        for (uint32_t n = 0; n < _n_unroll; n++) {
          a.xor_(x86::edx, x86::edx);
          if (n == 0)
            a.mov(x86::eax, 32123);

          if (o2[n].is_reg()) {
            x86::Gp r(o2[n].as<x86::Gp>());
            r.set_id(x86::Gp::kIdCx);
            a.emit(inst_id, o0[n], o1[n], r);
          }
          else {
            a.emit(inst_id, o0[n], o1[n], o2[n]);
          }

          if (n + 1 != _n_unroll) {
            a.xor_(x86::edx, x86::edx);
            if (is_parallel)
              a.mov(x86::eax, 32123);
          }
        }
      }

      break;
    }

    case x86::Inst::kIdMul:
    case x86::Inst::kIdImul: {
      assert(op_count >= 2 && op_count <= 3);
      if (_overhead_only)
        break;

      if (op_count == 2) {
        for (uint32_t n = 0; n < _n_unroll; n++) {
          if (is_parallel)
            a.mov(o0[n].as<x86::Gp>().r32(), o1[n].as<x86::Gp>().r32());
          a.emit(inst_id, o0[n], o1[n]);
        }
      }

      if (op_count == 3) {
        for (uint32_t n = 0; n < _n_unroll; n++) {
          if (is_parallel && InstSpec::is_implicit_op(_inst_spec.get(1)))
            a.mov(o1[n].as<x86::Gp>().r32(), o2[n].as<x86::Gp>().r32());
          a.emit(inst_id, o0[n], o1[n], o2[n]);
        }
      }

      break;
    }

    case x86::Inst::kIdLea: {
      assert(op_count >= 2 && op_count <= 4);
      if (_overhead_only)
        break;

      if (op_count == 2) {
        for (uint32_t n = 0; n < _n_unroll; n++) {
          a.emit(inst_id, o0[n], x86::ptr(o1[n].as<x86::Gp>()));
        }
      }

      if (op_count == 3) {
        for (uint32_t n = 0; n < _n_unroll; n++) {
          x86::Mem m;
          if (o2[n].is_reg())
            m = x86::ptr(o1[n].as<x86::Gp>(), o2[n].as<x86::Gp>());
          else
            m = x86::ptr(o1[n].as<x86::Gp>(), o2[n].as<Imm>().value_as<int32_t>());

          if (_inst_spec.is_lea_scale())
            m.set_shift(3);

          a.emit(inst_id, o0[n], m);

        }
      }

      if (op_count == 4) {
        for (uint32_t n = 0; n < _n_unroll; n++) {
          x86::Mem m = x86::ptr(o1[n].as<x86::Gp>(), o2[n].as<x86::Gp>(), 0, o3[n].as<Imm>().value_as<int32_t>());
          if (_inst_spec.is_lea_scale())
            m.set_shift(3);
          a.emit(inst_id, o0[n], m);
        }
      }

      break;
    }

    case x86::Inst::kIdVgatherdps:
    case x86::Inst::kIdVpgatherdd:
    case x86::Inst::kIdVgatherdpd:
    case x86::Inst::kIdVpgatherdq:
    case x86::Inst::kIdVgatherqps:
    case x86::Inst::kIdVpgatherqd:
    case x86::Inst::kIdVgatherqpd:
    case x86::Inst::kIdVpgatherqq: {
      if (is_gather_avx512) {
        for (uint32_t n = 0; n < _n_unroll; n++) {
          x86::Vec index = o1[n].as<x86::Vec>();
          x86::KReg pred = ox[n].as<x86::KReg>();
          x86::Vec dst = o0[n].as<x86::Vec>();

          a.kmovw(pred, x86::k7);
          if (!_overhead_only)
            a.k(pred).emit(inst_id, dst, x86::ptr(gs_base, index, gs_index_shift));

          if (inst_id == x86::Inst::kIdVgatherqps || inst_id == x86::Inst::kIdVpgatherqd)
            a.vpmovzxdq(dst.clone_as(index), dst);
        }
      }
      else {
        for (uint32_t n = 0; n < _n_unroll; n++) {
          x86::Vec index = o1[n].as<x86::Vec>();
          x86::Vec ones = gathereg_mask.clone_as(o0[n].as<x86::Vec>());
          x86::Vec dst = o0[n].as<x86::Vec>();

          x86::Vec pred = x86::xmm6.clone_as(ones);
          a.vmovdqa(pred, ones);

          if (!_overhead_only)
            a.emit(inst_id, dst, x86::ptr(gs_base, index, gs_index_shift), pred);

          if (inst_id == x86::Inst::kIdVgatherqps || inst_id == x86::Inst::kIdVpgatherqd)
            a.vpmovzxdq(dst.clone_as(index), dst);
        }
      }
      break;
    }

    case x86::Inst::kIdVscatterdps:
    case x86::Inst::kIdVpscatterdd:
    case x86::Inst::kIdVscatterdpd:
    case x86::Inst::kIdVpscatterdq:
    case x86::Inst::kIdVscatterqps:
    case x86::Inst::kIdVpscatterqd:
    case x86::Inst::kIdVscatterqpd:
    case x86::Inst::kIdVpscatterqq: {
      for (uint32_t n = 0; n < _n_unroll; n++) {
        x86::Vec index = x86::xmm7.clone_as(o0[0].as<x86::Vec>());
        x86::Vec src = x86::xmm6.clone_as(o1[0].as<x86::Vec>());
        x86::KReg pred = ox[n].as<x86::KReg>();

        a.kmovw(pred, x86::k7);
        if (!_overhead_only)
          a.k(pred).emit(inst_id, x86::ptr(gs_base, index, gs_index_shift, n * 8), src);

        uint32_t indexCount = index.size() / (scatter_index_size(inst_id) / 8u);
        uint32_t srcCount = src.size() / (scatter_element_size(inst_id) / 8u);
        uint32_t elementCount = asmjit::Support::min(indexCount, srcCount);

        // Read the last scattered write to create a dependency for another scatter.
        if (!is_parallel) {
          if (index.is_vec128())
            a.vpaddd(index, index, x86::ptr(gs_base, n * 8 + ((elementCount - 1) * 128))._1to4());
          else if (index.is_vec256())
            a.vpaddd(index, index, x86::ptr(gs_base, n * 8 + ((elementCount - 1) * 128))._1to8());
          else
            a.vpaddd(index, index, x86::ptr(gs_base, n * 8 + ((elementCount - 1) * 128))._1to16());
        }
      }

      break;
    }

    // Instructions that don't require special care.
    default: {
      assert(op_count <= 6);

      // Special case for instructions where destination register type doesn't appear anywhere in source.
      if (!is_parallel) {
        if (op_count >= 2 && o0[0].is_reg()) {
          bool sameKind = false;
          bool specialInst = false;

          const Reg& dst = o0[0].as<Reg>();

          if (op_count >= 2 && (o1[0].is_reg() && o1[0].as<Reg>().reg_group() == dst.reg_group()))
            sameKind = true;

          if (op_count >= 3 && (o2[0].is_reg() && o2[0].as<Reg>().reg_group() == dst.reg_group()))
            sameKind = true;

          if (op_count >= 4 && (o3[0].is_reg() && o3[0].as<Reg>().reg_group() == dst.reg_group()))
            sameKind = true;

          // These have the same kind in 'reg, reg' case, however, some registers are fixed so we workaround it this way.
          specialInst = (inst_id == x86::Inst::kIdCdq ||
                         inst_id == x86::Inst::kIdCdqe ||
                         inst_id == x86::Inst::kIdCqo ||
                         inst_id == x86::Inst::kIdCwd ||
                         inst_id == x86::Inst::kIdPop);

          if ((!sameKind && is_write_only(a.arch(), inst_id, _inst_spec)) || specialInst) {
            for (uint32_t n = 0; n < _n_unroll; n++) {
              if (!_overhead_only) {
                Operand ops[6] = { o0[0], o1[0], o2[0], o3[0], o4[0], o5[0] };
                a.emit_op_array(inst_id, ops, op_count);
              }

              auto emitSequentialOp = [&](const Reg& reg, bool is_dst) {
                if (reg.is_gp()) {
                  if (is_dst)
                    a.add(x86::eax, reg.as<x86::Gp>().r32());
                  else
                    a.add(reg.as<x86::Gp>().r32(), reg.as<x86::Gp>().r32());
                }
                else if (reg.is_mask_reg()) {
                  if (is_dst)
                    a.korw(x86::k7, x86::k7, reg.as<x86::KReg>());
                  else
                    a.korw(reg.as<x86::KReg>(), x86::k7, reg.as<x86::KReg>());
                }
                else if (reg.is_mm_reg()) {
                  if (is_dst)
                    a.paddb(x86::mm7, reg.as<x86::Mm>());
                  else
                    a.paddb(reg.as<x86::Mm>(), reg.as<x86::Mm>());
                }
                else if (reg.is_vec128() && !inst_info.is_vex_or_evex()) {
                  if (is_dst)
                    a.paddb(x86::xmm7, reg.as<x86::Vec>());
                  else
                    a.paddb(reg.as<x86::Vec>(), reg.as<x86::Vec>());
                }
                else if (reg.is_vec()) {
                  if (is_dst)
                    a.vpaddb(x86::xmm7, x86::xmm7, reg.as<x86::Vec>().xmm());
                  else
                    a.vpaddb(reg.as<x86::Vec>().xmm(), x86::xmm7, reg.as<x86::Vec>().xmm());
                }
              };

              emitSequentialOp(dst, true);
              if (o1[0].is_reg())
                emitSequentialOp(o1[0].as<Reg>(), false);
            }
            break;
          }
        }
      }

      if (_overhead_only)
        break;

      if (op_count == 0) {
        for (uint32_t n = 0; n < _n_unroll; n++)
          a.emit(inst_id);
      }

      if (op_count == 1) {
        for (uint32_t n = 0; n < _n_unroll; n++)
          a.emit(inst_id, o0[n]);
      }

      if (op_count == 2) {
        for (uint32_t n = 0; n < _n_unroll; n++)
          a.emit(inst_id, o0[n], o1[n]);
      }

      if (op_count == 3) {
        for (uint32_t n = 0; n < _n_unroll; n++)
          a.emit(inst_id, o0[n], o1[n], o2[n]);
      }

      if (op_count == 4) {
        for (uint32_t n = 0; n < _n_unroll; n++)
          a.emit(inst_id, o0[n], o1[n], o2[n], o3[n]);
      }

      if (op_count == 5) {
        for (uint32_t n = 0; n < _n_unroll; n++)
          a.emit(inst_id, o0[n], o1[n], o2[n], o3[n], o4[n]);
      }

      if (op_count == 6) {
        for (uint32_t n = 0; n < _n_unroll; n++)
          a.emit(inst_id, o0[n], o1[n], o2[n], o3[n], o4[n], o5[n]);
      }
      break;
    }
  }

  if (inst_id == x86::Inst::kIdPush && !_overhead_only)
    a.add(a.zsp(), stackOperationSize);

  a.sub(reg_cnt, 1);
  a.jnz(L_Body);
  a.bind(L_End);

  if (inst_id == x86::Inst::kIdCall) {
    Label L_RealEnd = a.new_label();
    a.jmp(L_RealEnd);
    a.bind(L_SubFn);
    a.ret();
    a.bind(L_RealEnd);
  }

  ::free(o0);
  ::free(o1);
  ::free(o2);
  ::free(o3);
  ::free(o4);
  ::free(o5);
}

void InstBench::after_body(x86::Assembler& a) {
  if (is_mmx(_inst_id, _inst_spec))
    a.emms();

  if (is_avx(_inst_id, _inst_spec))
    a.vzeroupper();
}

void InstBench::fill_memory_u32(x86::Assembler& a, x86::Gp base_address, uint32_t value, uint32_t n) {
  Label loop = a.new_label();
  x86::Gp cnt = x86::edi;

  a.xor_(cnt, cnt);
  a.bind(loop);
  a.mov(x86::dword_ptr(a.zsp(), cnt, 2), value);
  a.inc(cnt);
  a.cmp(cnt, n);
  a.jne(loop);
}

} // {cult} namespace
