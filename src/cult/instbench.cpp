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

  inline uint32_t nextUInt32() noexcept {
    return uint32_t(nextUInt64() >> 32);
  }

  inline uint64_t nextUInt64() noexcept {
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

  const InstSignature* _instSignature;
  const OpSignature* _opSigArray[asmjit::Globals::kMaxOpCount];
  x86::InstDB::OpFlags _opMaskArray[asmjit::Globals::kMaxOpCount];
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
  inline InstSignatureIterator(const InstSignature* instSignature, x86::InstDB::OpFlags filter = kDefaultFilter) { init(instSignature, filter); }
  inline InstSignatureIterator(const InstSignatureIterator& other) { init(other); }

  inline void reset() { ::memset(this, 0, sizeof(*this)); }
  inline void init(const InstSignatureIterator& other) { ::memcpy(this, &other, sizeof(*this)); }

  void init(const InstSignature* instSignature, x86::InstDB::OpFlags filter = kDefaultFilter) {
    const OpSignature* opSigArray = asmjit::x86::InstDB::_opSignatureTable;
    uint32_t opCount = instSignature->opCount();

    _instSignature = instSignature;
    _opCount = opCount;
    _filter = filter;

    uint32_t i;
    x86::InstDB::OpFlags flags = x86::InstDB::OpFlags::kNone;

    for (i = 0; i < opCount; i++) {
      const OpSignature& opSig = instSignature->opSignature(i);
      flags = opSig.flags() & _filter;

      if (flags == x86::InstDB::OpFlags::kNone)
        break;

      _opSigArray[i] = &opSig;
      _opMaskArray[i] = x86::InstDB::OpFlags(asmjit::Support::blsi(uint64_t(flags)));
    }

    while (i < kMaxOpCount) {
      _opSigArray[i] = &opSigArray[0];
      _opMaskArray[i] = x86::InstDB::OpFlags::kNone;
      i++;
    }

    _isValid = opCount == 0 || flags != x86::InstDB::OpFlags::kNone;
  }

  inline bool isValid() const { return _isValid; }
  inline uint32_t opCount() const { return _opCount; }

  inline const x86::InstDB::OpFlags* opMaskArray() const { return _opMaskArray; }
  inline const OpSignature* const* opSigArray() const { return _opSigArray; }

  inline x86::InstDB::OpFlags opMask(uint32_t i) const { return _opMaskArray[i]; }
  inline const OpSignature* opSig(uint32_t i) const { return _opSigArray[i]; }

  bool next() {
    uint32_t i = _opCount - 1u;
    for (;;) {
      if (i == 0xFFFFFFFFu) {
        _isValid = false;
        return false;
      }

      // Iterate over OpFlags.
      x86::InstDB::OpFlags prevBit = _opMaskArray[i];
      x86::InstDB::OpFlags allFlags = _opSigArray[i]->flags() & _filter;

      x86::InstDB::OpFlags bitsToClear = (x86::InstDB::OpFlags)(uint64_t(prevBit) | (uint64_t(prevBit) - 1u));
      x86::InstDB::OpFlags remainingBits = allFlags & ~bitsToClear;

      if (remainingBits != x86::InstDB::OpFlags::kNone) {
        _opMaskArray[i] = (x86::InstDB::OpFlags)asmjit::Support::blsi(uint64_t(remainingBits));
        return true;
      }
      else {
        _opMaskArray[i--] = (x86::InstDB::OpFlags)asmjit::Support::blsi(uint64_t(allFlags));
      }
    }
  }
};

// TODO: These require pretty special register pattern - not tested yet.
static bool isIgnoredInst(InstId instId) {
  return instId == x86::Inst::kIdVp2intersectd ||
         instId == x86::Inst::kIdVp2intersectq;
}

// Returns true when the instruction is safe to be benchmarked.
//
// There is many general purpose instructions including system ones. We only
// benchmark those that may appear commonly in user code, but not in kernel.
static bool isSafeGpInst(InstId instId) {
  return instId == x86::Inst::kIdAdc        ||
         instId == x86::Inst::kIdAdcx       ||
         instId == x86::Inst::kIdAdd        ||
         instId == x86::Inst::kIdAdox       ||
         instId == x86::Inst::kIdAnd        ||
         instId == x86::Inst::kIdAndn       ||
         instId == x86::Inst::kIdBextr      ||
         instId == x86::Inst::kIdBlcfill    ||
         instId == x86::Inst::kIdBlci       ||
         instId == x86::Inst::kIdBlcic      ||
         instId == x86::Inst::kIdBlcmsk     ||
         instId == x86::Inst::kIdBlcs       ||
         instId == x86::Inst::kIdBlsfill    ||
         instId == x86::Inst::kIdBlsi       ||
         instId == x86::Inst::kIdBlsic      ||
         instId == x86::Inst::kIdBlsmsk     ||
         instId == x86::Inst::kIdBlsr       ||
         instId == x86::Inst::kIdBsf        ||
         instId == x86::Inst::kIdBsr        ||
         instId == x86::Inst::kIdBswap      ||
         instId == x86::Inst::kIdBt         ||
         instId == x86::Inst::kIdBtc        ||
         instId == x86::Inst::kIdBtr        ||
         instId == x86::Inst::kIdBts        ||
         instId == x86::Inst::kIdBzhi       ||
         instId == x86::Inst::kIdCbw        ||
         instId == x86::Inst::kIdCdq        ||
         instId == x86::Inst::kIdCdqe       ||
         instId == x86::Inst::kIdCmp        ||
         instId == x86::Inst::kIdCrc32      ||
         instId == x86::Inst::kIdCqo        ||
         instId == x86::Inst::kIdCwd        ||
         instId == x86::Inst::kIdCwde       ||
         instId == x86::Inst::kIdDec        ||
         instId == x86::Inst::kIdDiv        ||
         instId == x86::Inst::kIdIdiv       ||
         instId == x86::Inst::kIdImul       ||
         instId == x86::Inst::kIdInc        ||
         instId == x86::Inst::kIdLzcnt      ||
         instId == x86::Inst::kIdMov        ||
         instId == x86::Inst::kIdMovbe      ||
         instId == x86::Inst::kIdMovsx      ||
         instId == x86::Inst::kIdMovsxd     ||
         instId == x86::Inst::kIdMovzx      ||
         instId == x86::Inst::kIdMul        ||
         instId == x86::Inst::kIdMulx       ||
         instId == x86::Inst::kIdNeg        ||
         instId == x86::Inst::kIdNop        ||
         instId == x86::Inst::kIdNot        ||
         instId == x86::Inst::kIdOr         ||
         instId == x86::Inst::kIdPdep       ||
         instId == x86::Inst::kIdPext       ||
         instId == x86::Inst::kIdPop        ||
         instId == x86::Inst::kIdPopcnt     ||
         instId == x86::Inst::kIdPush       ||
         instId == x86::Inst::kIdRcl        ||
         instId == x86::Inst::kIdRcr        ||
         instId == x86::Inst::kIdRdrand     ||
         instId == x86::Inst::kIdRdseed     ||
         instId == x86::Inst::kIdRol        ||
         instId == x86::Inst::kIdRor        ||
         instId == x86::Inst::kIdRorx       ||
         instId == x86::Inst::kIdSar        ||
         instId == x86::Inst::kIdSarx       ||
         instId == x86::Inst::kIdSbb        ||
         instId == x86::Inst::kIdShl        ||
         instId == x86::Inst::kIdShld       ||
         instId == x86::Inst::kIdShlx       ||
         instId == x86::Inst::kIdShr        ||
         instId == x86::Inst::kIdShrd       ||
         instId == x86::Inst::kIdShrx       ||
         instId == x86::Inst::kIdSub        ||
         instId == x86::Inst::kIdT1mskc     ||
         instId == x86::Inst::kIdTest       ||
         instId == x86::Inst::kIdTzcnt      ||
         instId == x86::Inst::kIdTzmsk      ||
         instId == x86::Inst::kIdXadd       ||
         instId == x86::Inst::kIdXchg       ||
         instId == x86::Inst::kIdXor        ;
}

static uint32_t gatherIndexSize(InstId instId) {
  switch (instId) {
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

static uint32_t scatterIndexSize(InstId instId) {
  switch (instId) {
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

static uint32_t scatterElementSize(InstId instId) {
  switch (instId) {
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

static bool isGatherInst(InstId instId) {
  return gatherIndexSize(instId) != 0;
}

static bool isScatterInst(InstId instId) {
  return scatterIndexSize(instId) != 0;
}

static bool isSafeUnaligned(InstId instId, uint32_t memOp) {
  const x86::InstDB::InstInfo& inst = x86::InstDB::infoById(instId);

  if (instId == x86::Inst::kIdNop)
    return false;

  if (inst.isSse()) {
    return instId == x86::Inst::kIdMovdqu ||
           instId == x86::Inst::kIdMovupd ||
           instId == x86::Inst::kIdMovups ||
           memOp != InstSpec::kOpMem128;
  }

  if (inst.isAvx() || inst.isEvex()) {
    return instId != x86::Inst::kIdVmovapd &&
           instId != x86::Inst::kIdVmovaps &&
           instId != x86::Inst::kIdVmovdqa &&
           instId != x86::Inst::kIdVmovdqa32 &&
           instId != x86::Inst::kIdVmovdqa64 &&
           instId != x86::Inst::kIdVmovntdq &&
           instId != x86::Inst::kIdVmovntdqa &&
           instId != x86::Inst::kIdVmovntpd &&
           instId != x86::Inst::kIdVmovntps;
  }

  return true;
}

static void instSpecToOperandArray(Arch arch, Operand* operands, InstSpec spec) {
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

static bool isWriteOnly(Arch arch, InstId instId, InstSpec spec) {
  Operand operands[6] {};
  instSpecToOperandArray(arch, operands, spec);
  InstRWInfo rwInfo {};

  InstAPI::queryRWInfo(arch, BaseInst(instId), operands, spec.count(), &rwInfo);
  if (rwInfo.opCount() > 0 && rwInfo.operands()[0].isWriteOnly())
    return true;

  return false;
}

static const char* instSpecOpAsString(uint32_t instSpecOp) {
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
    mem.addOffset(increment);
  }
}

static void fillRegArray(Operand* dst, uint32_t count, uint32_t rStart, uint32_t rInc, uint32_t rMask, uint32_t rSign) {
  uint32_t rIdCount = 0;
  uint8_t rIdArray[64];

  // Fill rIdArray[] array from the bits as specified by `rMask`.
  asmjit::Support::BitWordIterator<uint32_t> rMaskIterator(rMask);
  while (rMaskIterator.hasNext()) {
    uint32_t id = rMaskIterator.next();
    rIdArray[rIdCount++] = uint8_t(id);
  }

  uint32_t rId = rStart % rIdCount;
  for (uint32_t i = 0; i < count; i++) {
    dst[i] = BaseReg(OperandSignature{rSign}, rIdArray[rId]);
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
static double roundResult(double x) {
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
    _instId(0),
    _instSpec(),
    _nUnroll(64),
    _nParallel(0),
    _gatherData{},
    _gatherDataSize(4096) {}

InstBench::~InstBench() {
  freeGatherData(32);
  freeGatherData(64);
}

const void* InstBench::ensureGatherData(uint32_t elementSize, bool isAligned) {
  uint32_t index = elementSize == 32 ? 0 : 1;

  size_t dataSize = _gatherDataSize * (elementSize / 8);

  if (!_gatherData[index]) {
    _gatherData[index] = malloc(dataSize * 2 + 8);

    Random rg(123456789);
    uint32_t mask = _gatherDataSize - 1;

    if (elementSize == 32) {
      uint32_t* d = static_cast<uint32_t*>(_gatherData[index]);
      for (uint32_t i = 0; i < _gatherDataSize; i++) {
        d[i] = (rg.nextUInt32() & mask) * 4;
      }

      // Unaligned data.
      memcpy(reinterpret_cast<char*>(d) + dataSize + 1, d, dataSize);
    }
    else {
      uint64_t* d = static_cast<uint64_t*>(_gatherData[index]);
      for (uint32_t i = 0; i < _gatherDataSize; i++) {
        d[i] = (rg.nextUInt32() & mask) * 8;
      }

      // Unaligned data.
      memcpy(reinterpret_cast<char*>(d) + dataSize + 1, d, dataSize);
    }
  }

  return isAligned ? _gatherData[index] : static_cast<char*>(_gatherData[index]) + dataSize + 1;
}

void InstBench::freeGatherData(uint32_t elementSize) {
  uint32_t index = elementSize == 32 ? 0 : 1;

  free(_gatherData[index]);
  _gatherData[index] = nullptr;
}

uint32_t InstBench::localStackSize() const {
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

  json.beforeRecord()
      .addKey("instructions")
      .openArray();

  uint32_t instStart = 1;
  uint32_t instEnd = x86::Inst::_kIdCount;

  if (_app->_singleInstId) {
    instStart = _app->_singleInstId;
    instEnd = instStart + 1;
  }

  for (InstId instId = instStart; instId < instEnd; instId++) {
    std::vector<InstSpec> specs;
    classify(specs, instId);

    /*
    if (specs.size() == 0) {
      asmjit::String name;
      InstAPI::instIdToString(Environment::kArchHost, instId, name);
      printf("MISSING SPEC: %s\n", name.data());
    }
    */

    for (size_t i = 0; i < specs.size(); i++) {
      InstSpec instSpec = specs[i];
      uint32_t opCount = instSpec.count();
      uint32_t memOp = instSpec.memOp();

      std::vector<uint32_t> alignments;
      if (memOp && memOp != InstSpec::kOpMem8 && isSafeUnaligned(instId, memOp)) {
        alignments.push_back(0u);
        alignments.push_back(1u);
      }
      else {
        alignments.push_back(0u);
      }

      for (uint32_t alignment : alignments) {
        StringTmp<256> sb;
        if (instId == x86::Inst::kIdCall) {
          sb.append("call+ret");
        }
        else {
          InstAPI::instIdToString(Arch::kHost, instId, sb);
        }

        for (uint32_t i = 0; i < opCount; i++) {
          if (i == 0)
            sb.append(' ');
          else if (instId == x86::Inst::kIdLea)
            sb.append(i == 1 ? ", [" : " + ");
          else
            sb.append(", ");

          sb.append(instSpecOpAsString(instSpec.get(i)));

          if (i == 0 && (isGatherInst(instId) || isScatterInst(instId)) && opCount == 2) {
            sb.append(" {k}");
          }

          if (i == 2 && instSpec.isLeaScale())
            sb.append(" * N");

          if (instId == x86::Inst::kIdLea && i == opCount - 1)
            sb.append(']');

          if (alignments.size() != 1) {
            if (InstSpec::isMemOp(instSpec.get(i)) || InstSpec::isVmOp(instSpec.get(i))) {
              if (alignment == 0)
                sb.append(" {a}");
              else
                sb.append(" {u}");
            }
          }
        }

        double overheadLat = testInstruction(instId, instSpec, 0, alignment, true);
        double overheadRcp = testInstruction(instId, instSpec, 1, alignment, true);

        double lat = testInstruction(instId, instSpec, 0, alignment, false);
        double rcp = testInstruction(instId, instSpec, 1, alignment, false);

        lat = std::max<double>(lat - overheadLat, 0);
        rcp = std::max<double>(rcp - overheadRcp, 0);

        if (_app->_round) {
          lat = roundResult(lat);
          rcp = roundResult(rcp);
        }

        // Some tests are probably skewed. If this happens the latency is the throughput.
        if (rcp > lat)
          lat = rcp;

        if (_app->verbose())
          printf("  %-40s: Lat:%7.2f Rcp:%7.2f\n", sb.data(), lat, rcp);

        json.beforeRecord()
            .openObject()
            .addKey("inst").addString(sb.data()).alignTo(54)
            .addKey("lat").addDoublef("%7.2f", lat)
            .addKey("rcp").addDoublef("%7.2f", rcp)
            .closeObject();
      }
    }
  }

  if (_app->verbose())
    printf("\n");

  json.closeArray(true);
}

void InstBench::classify(std::vector<InstSpec>& dst, InstId instId) {
  using namespace asmjit;

  if (isIgnoredInst(instId))
    return;

  // Special cases.
  if (instId == x86::Inst::kIdCpuid    ||
      instId == x86::Inst::kIdEmms     ||
      instId == x86::Inst::kIdFemms    ||
      instId == x86::Inst::kIdLfence   ||
      instId == x86::Inst::kIdMfence   ||
      instId == x86::Inst::kIdRdtsc    ||
      instId == x86::Inst::kIdRdtscp   ||
      instId == x86::Inst::kIdSfence   ||
      instId == x86::Inst::kIdXgetbv   ||
      instId == x86::Inst::kIdVzeroall ||
      instId == x86::Inst::kIdVzeroupper) {
    if (canRun(instId))
      dst.push_back(InstSpec::pack(0));
    return;
  }

  if (instId == x86::Inst::kIdCall) {
    dst.push_back(InstSpec::pack(InstSpec::kOpRel));
    if (is64Bit()) {
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq));
      dst.push_back(InstSpec::pack(InstSpec::kOpMem64));
    }
    else {
      dst.push_back(InstSpec::pack(InstSpec::kOpGpd));
      dst.push_back(InstSpec::pack(InstSpec::kOpMem32));
    }
    return;
  }

  if (instId == x86::Inst::kIdJmp) {
    dst.push_back(InstSpec::pack(InstSpec::kOpRel));
    return;
  }

  if (instId == x86::Inst::kIdLea) {
    dst.push_back(InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd));
    dst.push_back(InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpImm8));
    dst.push_back(InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpImm32));
    dst.push_back(InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpGpd));
    dst.push_back(InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpGpd).leaScale());
    dst.push_back(InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpImm8));
    dst.push_back(InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpImm32));
    dst.push_back(InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpImm8).leaScale());
    dst.push_back(InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpImm32).leaScale());

    if (is64Bit()) {
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq));
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpImm8));
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpImm32));
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpGpq));
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpGpq).leaScale());
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpImm8));
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpImm32));
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpImm8).leaScale());
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpImm32).leaScale());
    }
    return;
  }

  // Common cases based on instruction signatures.
  x86::InstDB::Mode mode = x86::InstDB::Mode::kNone;
  x86::InstDB::OpFlags opFilter =
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
    opFilter &= ~x86::InstDB::OpFlags::kRegGpq;
  }
  else {
    mode = x86::InstDB::Mode::kX64;
  }

  const x86::InstDB::InstInfo& instInfo = x86::InstDB::infoById(instId);
  const x86::InstDB::CommonInfo& commonInfo = instInfo.commonInfo();

  const x86::InstDB::InstSignature* instSignature = commonInfo.signatureData();
  const x86::InstDB::InstSignature* iEnd = commonInfo.signatureEnd();

  // Iterate over all signatures and build the instruction we want to test.
  std::set<InstSpec> known;
  for (; instSignature != iEnd; instSignature++) {
    if (!instSignature->supportsMode(mode))
      continue;

    InstSignatureIterator it(instSignature, opFilter);
    while (it.isValid()) {
      uint32_t opCount = it.opCount();
      InstSpec instSpec {};

      bool skip = false;
      bool vec = false;
      uint32_t immCount = 0;

      for (uint32_t opIndex = 0; opIndex < opCount; opIndex++) {
        x86::InstDB::OpFlags opFlags = it.opMask(opIndex);
        const x86::InstDB::OpSignature* opSig = it.opSig(opIndex);

        if (Support::test(opFlags, x86::InstDB::OpFlags::kRegMask)) {
          uint32_t regId = 0;
          if (Support::isPowerOf2(opSig->regMask()))
            regId = Support::ctz(opSig->regMask());

          switch (opFlags) {
            case x86::InstDB::OpFlags::kRegGpbLo: instSpec.set(opIndex, InstSpec::kOpGpb); break;
            case x86::InstDB::OpFlags::kRegGpbHi: instSpec.set(opIndex, InstSpec::kOpGpb); break;
            case x86::InstDB::OpFlags::kRegGpw  : instSpec.set(opIndex, InstSpec::kOpGpw); break;
            case x86::InstDB::OpFlags::kRegGpd  : instSpec.set(opIndex, InstSpec::kOpGpd); break;
            case x86::InstDB::OpFlags::kRegGpq  : instSpec.set(opIndex, InstSpec::kOpGpq); break;
            case x86::InstDB::OpFlags::kRegXmm  : instSpec.set(opIndex, InstSpec::kOpXmm); vec = true; break;
            case x86::InstDB::OpFlags::kRegYmm  : instSpec.set(opIndex, InstSpec::kOpYmm); vec = true; break;
            case x86::InstDB::OpFlags::kRegZmm  : instSpec.set(opIndex, InstSpec::kOpZmm); vec = true; break;
            case x86::InstDB::OpFlags::kRegMm   : instSpec.set(opIndex, InstSpec::kOpMm); vec = true; break;
            case x86::InstDB::OpFlags::kRegKReg : instSpec.set(opIndex, InstSpec::kOpKReg); vec = true; break;
            default:
              printf("[!!] Unknown register operand: OpMask=0x%016llX\n", (unsigned long long)opFlags);
              skip = true;
              break;
          }

          if (Support::isPowerOf2(opSig->regMask())) {
            switch (opFlags) {
              case x86::InstDB::OpFlags::kRegGpbLo: instSpec.set(opIndex, InstSpec::kOpAl + regId); break;
              case x86::InstDB::OpFlags::kRegGpbHi: instSpec.set(opIndex, InstSpec::kOpAl + regId); break;
              case x86::InstDB::OpFlags::kRegGpw  : instSpec.set(opIndex, InstSpec::kOpAx + regId); break;
              case x86::InstDB::OpFlags::kRegGpd  : instSpec.set(opIndex, InstSpec::kOpEax + regId); break;
              case x86::InstDB::OpFlags::kRegGpq  : instSpec.set(opIndex, InstSpec::kOpRax + regId); break;
              case x86::InstDB::OpFlags::kRegXmm  : instSpec.set(opIndex, InstSpec::kOpXmm0); break;
              default:
                printf("[!!] Unknown register operand: OpMask=0x%016llX\n", (unsigned long long)opFlags);
                skip = true;
                break;
            }
          }
        }
        else if (Support::test(opFlags, x86::InstDB::OpFlags::kMemMask)) {
          // The assembler would just swap the operands, so if memory is first or second it doesn't matter.
          if (opIndex == 0 && instId == x86::Inst::kIdXchg)
            skip = true;

          switch (opFlags) {
            case x86::InstDB::OpFlags::kMem8: instSpec.set(opIndex, InstSpec::kOpMem8); break;
            case x86::InstDB::OpFlags::kMem16: instSpec.set(opIndex, InstSpec::kOpMem16); break;
            case x86::InstDB::OpFlags::kMem32: instSpec.set(opIndex, InstSpec::kOpMem32); break;
            case x86::InstDB::OpFlags::kMem64: instSpec.set(opIndex, InstSpec::kOpMem64); break;
            case x86::InstDB::OpFlags::kMem128: instSpec.set(opIndex, InstSpec::kOpMem128); break;
            case x86::InstDB::OpFlags::kMem256: instSpec.set(opIndex, InstSpec::kOpMem256); break;
            case x86::InstDB::OpFlags::kMem512: instSpec.set(opIndex, InstSpec::kOpMem512); break;
            default:
              skip = true;
              break;
          }
        }
        else if (Support::test(opFlags, x86::InstDB::OpFlags::kVmMask)) {
          switch (opFlags) {
            case x86::InstDB::OpFlags::kVm32x: instSpec.set(opIndex, InstSpec::kOpVm32x); break;
            case x86::InstDB::OpFlags::kVm32y: instSpec.set(opIndex, InstSpec::kOpVm32y); break;
            case x86::InstDB::OpFlags::kVm32z: instSpec.set(opIndex, InstSpec::kOpVm32z); break;
            case x86::InstDB::OpFlags::kVm64x: instSpec.set(opIndex, InstSpec::kOpVm64x); break;
            case x86::InstDB::OpFlags::kVm64y: instSpec.set(opIndex, InstSpec::kOpVm64y); break;
            case x86::InstDB::OpFlags::kVm64z: instSpec.set(opIndex, InstSpec::kOpVm64z); break;
            default:
              skip = true;
              break;
          }
        }
        else if (Support::test(opFlags, x86::InstDB::OpFlags::kImmMask)) {
          if (Support::test(opFlags, x86::InstDB::OpFlags::kImmI64 | x86::InstDB::OpFlags::kImmU64))
            instSpec.set(opIndex, InstSpec::kOpImm64);
          else if (Support::test(opFlags, x86::InstDB::OpFlags::kImmI32 | x86::InstDB::OpFlags::kImmU32))
            instSpec.set(opIndex, InstSpec::kOpImm32);
          else if (Support::test(opFlags, x86::InstDB::OpFlags::kImmI16 | x86::InstDB::OpFlags::kImmU16))
            instSpec.set(opIndex, InstSpec::kOpImm16);
          else
            instSpec.set(opIndex, InstSpec::kOpImm8);
        }
        else {
          skip = true;
        }
      }

      if (!skip) {
        if (vec || isSafeGpInst(instId)) {
          BaseInst baseInst(instId, InstOptions::kNone);
          Operand operands[6] {};

          instSpecToOperandArray(Arch::kHost, operands, instSpec);
          if (_canRun(baseInst, operands, opCount)) {
            if (known.find(instSpec) == known.end()) {
              known.insert(instSpec);
              dst.push_back(instSpec);
            }
          }
        }
      }

      it.next();
    }
  }
}

bool InstBench::isImplicit(InstId instId) {
  const x86::InstDB::InstInfo& instInfo = x86::InstDB::infoById(instId);
  const x86::InstDB::InstSignature* iSig = instInfo.signatureData();
  const x86::InstDB::InstSignature* iEnd = instInfo.signatureEnd();

  while (iSig != iEnd) {
    if (iSig->hasImplicitOperands())
      return true;
    iSig++;
  }

  return false;
}

bool InstBench::_canRun(const BaseInst& inst, const Operand_* operands, uint32_t count) const {
  using namespace asmjit;

  if (inst.id() == x86::Inst::kIdNone)
    return false;

  if (InstAPI::validate(Arch::kHost, inst, operands, count) != kErrorOk)
    return false;

  CpuFeatures features;
  if (InstAPI::queryFeatures(Arch::kHost, inst, operands, count, &features) != kErrorOk)
    return false;

  if (!_cpuInfo.features().hasAll(features))
    return false;

  return true;
}

uint32_t InstBench::numIterByInstId(InstId instId) const {
  switch (instId) {
    // Return low number for instructions that are really slow.
    case x86::Inst::kIdCpuid:
    case x86::Inst::kIdRdrand:
    case x86::Inst::kIdRdseed:
      return 4;

    default:
      return 160;
  }
}

double InstBench::testInstruction(InstId instId, InstSpec instSpec, uint32_t parallel, uint32_t memAlignment, bool overheadOnly) {
  _instId = instId;
  _instSpec = instSpec;
  _nParallel = parallel ? 6 : 1;
  _overheadOnly = overheadOnly;
  _memAlignment = memAlignment;

  Func func = compileFunc();
  if (!func) {
    String name;
    InstAPI::instIdToString(Arch::kHost, instId, name);
    printf("FAILED to compile function for '%s' instruction\n", name.data());
    return -1.0;
  }

  uint32_t nIter = numIterByInstId(_instId);

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

  releaseFunc(func);
  return double(best) / (double(nIter * _nUnroll));
}

void InstBench::beforeBody(x86::Assembler& a) {
  if (_instId == x86::Inst::kIdDiv || _instId == x86::Inst::kIdIdiv) {
    fillMemory32(a, a.zsp(), 0x03030303u, localStackSize() / 4);
  }
  else if (isGatherInst(_instId)) {
    x86::Gp gsBase = a.zdi();
    a.mov(gsBase, uintptr_t(ensureGatherData(gatherIndexSize(_instId), _memAlignment == 0)));

    if (_instSpec.count() == 2) {
      // AVX-512 gather has only two operands, one operand must be {k} register passed as extraReg.
      a.kxnorq(x86::k7, x86::k7, x86::k7);

      uint32_t regCount = is64Bit() ? 32 : 8;
      for (uint32_t i = 0; i < regCount; i++) {
        if (i == 7)
          continue;
        a.vmovdqu32(x86::zmm(i), x86::ptr(gsBase, i * 32));
      }
    }
    else {
      a.vpcmpeqb(x86::ymm7, x86::ymm7, x86::ymm7);

      uint32_t regCount = is64Bit() ? 16 : 8;
      for (uint32_t i = 0; i < regCount; i++) {
        if (i == 6 || i == 7)
          continue;
        a.vmovdqu(x86::ymm(i), x86::ptr(gsBase, i * 32));
      }
    }
  }
  else if (isScatterInst(_instId)) {
    static const uint8_t sequence[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

    x86::Gp gsBase = a.zdi();
    a.mov(gsBase, a.zsp());
    a.kxnorq(x86::k7, x86::k7, x86::k7);
    a.mov(a.zsi(), uintptr_t(sequence));

    if (scatterIndexSize(_instId) == 32) {
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
    fillMemory32(a, a.zsp(), 0u, localStackSize() / 4);
    if (x86::InstDB::infoById(_instId).isVec()) {
      uint32_t regs = Arch::kHost == Arch::kX86 ? 8 : 16;
      if (x86::InstDB::infoById(_instId).isSse()) {
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

void InstBench::compileBody(x86::Assembler& a, x86::Gp rCnt) {
  using namespace asmjit;

  InstId instId = _instId;
  const x86::InstDB::InstInfo& instInfo = x86::InstDB::infoById(instId);

  uint32_t rMask[32] {};

  uint32_t genericRegMask = is64Bit() ? 0xFFFFu : 0xFFu;

  rMask[uint32_t(RegGroup::kGp)] = genericRegMask & ~Support::bitMask(x86::Gp::kIdSp, rCnt.id());
  rMask[uint32_t(RegGroup::kVec)] = genericRegMask;
  rMask[uint32_t(RegGroup::kX86_K)] = 0xFE;
  rMask[uint32_t(RegGroup::kX86_MM)] = 0xFF;

  x86::Gp gsBase;
  x86::Vec gatherMask;

  // AVX-512 gather has only two operands, one operand must be {k} register passed as extraReg.
  bool isGather = isGatherInst(instId);
  bool isGatherAVX512 = isGather && _instSpec.count() == 2;
  bool isScatterAVX512 = isScatterInst(instId);

  uint32_t gsIndexShift = 0;

  if (isGather || isScatterAVX512) {
    gsBase = a.zdi();
    rMask[uint32_t(RegGroup::kGp)] &= ~Support::bitMask(gsBase.id());
    rMask[uint32_t(RegGroup::kVec)] &= ~Support::bitMask(6, 7);
    rMask[uint32_t(RegGroup::kX86_K)] &= ~Support::bitMask(6, 7);

    if (is64Bit())
      rMask[uint32_t(RegGroup::kVec)] |= isGatherAVX512 ? 0xFFFFFF00u : 0x0000FF00u;

    if (!isGatherAVX512 && !isScatterAVX512) {
      gatherMask = x86::ymm7;
    }
  }

  Operand* ox = static_cast<Operand*>(::calloc(1, sizeof(Operand) * _nUnroll));
  Operand* o0 = static_cast<Operand*>(::calloc(1, sizeof(Operand) * _nUnroll));
  Operand* o1 = static_cast<Operand*>(::calloc(1, sizeof(Operand) * _nUnroll));
  Operand* o2 = static_cast<Operand*>(::calloc(1, sizeof(Operand) * _nUnroll));
  Operand* o3 = static_cast<Operand*>(::calloc(1, sizeof(Operand) * _nUnroll));
  Operand* o4 = static_cast<Operand*>(::calloc(1, sizeof(Operand) * _nUnroll));
  Operand* o5 = static_cast<Operand*>(::calloc(1, sizeof(Operand) * _nUnroll));

  bool isParallel = _nParallel > 1;
  uint32_t i;
  uint32_t opCount = _instSpec.count();
  uint32_t regCount = opCount;

  int32_t misalignment = 0;

  if (_memAlignment != 0)
    misalignment = 1;

  while (regCount && _instSpec.get(regCount - 1) >= InstSpec::kOpImm8 && _instSpec.get(regCount - 1) < InstSpec::kOpVm32x)
    regCount--;

  for (i = 0; i < regCount; i++) {
    switch (_instSpec.get(i)) {
      case InstSpec::kOpAl:
      case InstSpec::kOpAx:
      case InstSpec::kOpEax:
      case InstSpec::kOpRax:
        rMask[uint32_t(RegGroup::kGp)] &= ~Support::bitMask(x86::Gp::kIdAx);
        break;

      case InstSpec::kOpBl:
      case InstSpec::kOpBx:
      case InstSpec::kOpEbx:
      case InstSpec::kOpRbx:
        rMask[uint32_t(RegGroup::kGp)] &= ~Support::bitMask(x86::Gp::kIdBx);
        break;

      case InstSpec::kOpCl:
      case InstSpec::kOpCx:
      case InstSpec::kOpEcx:
      case InstSpec::kOpRcx:
        rMask[uint32_t(RegGroup::kGp)] &= ~Support::bitMask(x86::Gp::kIdCx);
        break;

      case InstSpec::kOpDl:
      case InstSpec::kOpDx:
      case InstSpec::kOpEdx:
      case InstSpec::kOpRdx:
        rMask[uint32_t(RegGroup::kGp)] &= ~Support::bitMask(x86::Gp::kIdDx);
        break;
    }
  }

  if (isGatherAVX512 || isScatterAVX512) {
    fillRegArray(ox, _nUnroll, 1, isParallel ? 1 : 0, rMask[uint32_t(RegGroup::kX86_K)], x86::RegTraits<RegType::kX86_KReg>::kSignature);
  }
  else if (isGather) {
    // Don't count mask in AVX2 gather case.
    opCount--;
    regCount--;
  }

  for (i = 0; i < opCount; i++) {
    uint32_t spec = _instSpec.get(i);
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
        if (!isParallel)
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
        if (!isParallel)
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
        if (instId == x86::Inst::kIdVfcmaddcph ||
            instId == x86::Inst::kIdVfmaddcph  ||
            instId == x86::Inst::kIdVfcmaddcsh ||
            instId == x86::Inst::kIdVfmaddcsh  ||
            instId == x86::Inst::kIdVfcmulcsh  ||
            instId == x86::Inst::kIdVfmulcsh   ||
            instId == x86::Inst::kIdVfcmulcph  ||
            instId == x86::Inst::kIdVfmulcph) {
          if (!isParallel) {
            rStart = i == 0 ? 1 : 0;
          }
          else {
            rStart = i == 0 ? 0 : 1;
            rInc = 2;
          }
        }
        else {
          if (!isParallel)
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
        if (!isParallel)
          rStart = (i < 1) ? 2 : (i < 3) ? 1 : 0;
        else
          rStart = (i < 1) ? 0 : (i < 3) ? 1 : 2;
        break;
    }

    switch (spec) {
      case InstSpec::kOpAl    : fillOpArray(dst, _nUnroll, x86::al); break;
      case InstSpec::kOpBl    : fillOpArray(dst, _nUnroll, x86::bl); break;
      case InstSpec::kOpCl    : fillOpArray(dst, _nUnroll, x86::cl); break;
      case InstSpec::kOpDl    : fillOpArray(dst, _nUnroll, x86::dl); break;
      case InstSpec::kOpAx    : fillOpArray(dst, _nUnroll, x86::ax); break;
      case InstSpec::kOpBx    : fillOpArray(dst, _nUnroll, x86::bx); break;
      case InstSpec::kOpCx    : fillOpArray(dst, _nUnroll, x86::cx); break;
      case InstSpec::kOpDx    : fillOpArray(dst, _nUnroll, x86::dx); break;
      case InstSpec::kOpEax   : fillOpArray(dst, _nUnroll, x86::eax); break;
      case InstSpec::kOpEbx   : fillOpArray(dst, _nUnroll, x86::ebx); break;
      case InstSpec::kOpEcx   : fillOpArray(dst, _nUnroll, x86::ecx); break;
      case InstSpec::kOpEdx   : fillOpArray(dst, _nUnroll, x86::edx); break;
      case InstSpec::kOpRax   : fillOpArray(dst, _nUnroll, x86::rax); break;
      case InstSpec::kOpRbx   : fillOpArray(dst, _nUnroll, x86::rbx); break;
      case InstSpec::kOpRcx   : fillOpArray(dst, _nUnroll, x86::rcx); break;
      case InstSpec::kOpRdx   : fillOpArray(dst, _nUnroll, x86::rdx); break;
      case InstSpec::kOpGpb   : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[uint32_t(RegGroup::kGp)], x86::RegTraits<RegType::kX86_GpbLo>::kSignature); break;
      case InstSpec::kOpGpw   : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[uint32_t(RegGroup::kGp)], x86::RegTraits<RegType::kX86_Gpw>::kSignature); break;
      case InstSpec::kOpGpd   : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[uint32_t(RegGroup::kGp)], x86::RegTraits<RegType::kX86_Gpd>::kSignature); break;
      case InstSpec::kOpGpq   : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[uint32_t(RegGroup::kGp)], x86::RegTraits<RegType::kX86_Gpq>::kSignature); break;

      case InstSpec::kOpXmm0  : fillOpArray(dst, _nUnroll, x86::xmm0); break;
      case InstSpec::kOpXmm   : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[uint32_t(RegGroup::kVec)], x86::RegTraits<RegType::kX86_Xmm>::kSignature); break;
      case InstSpec::kOpYmm   : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[uint32_t(RegGroup::kVec)], x86::RegTraits<RegType::kX86_Ymm>::kSignature); break;
      case InstSpec::kOpZmm   : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[uint32_t(RegGroup::kVec)], x86::RegTraits<RegType::kX86_Zmm>::kSignature); break;
      case InstSpec::kOpKReg  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[uint32_t(RegGroup::kX86_K)], x86::RegTraits<RegType::kX86_KReg>::kSignature); break;
      case InstSpec::kOpMm    : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[uint32_t(RegGroup::kX86_MM)], x86::RegTraits<RegType::kX86_Mm >::kSignature); break;

      case InstSpec::kOpImm8  : fillImmArray(dst, _nUnroll, 0, 1    , 15        ); break;
      case InstSpec::kOpImm16 : fillImmArray(dst, _nUnroll, 1, 13099, 65535     ); break;
      case InstSpec::kOpImm32 : fillImmArray(dst, _nUnroll, 1, 19231, 2000000000); break;
      case InstSpec::kOpImm64 : fillImmArray(dst, _nUnroll, 1, 9876543219231, 0x0FFFFFFFFFFFFFFF); break;

      case InstSpec::kOpMem8  : fillMemArray(dst, _nUnroll, x86::byte_ptr(a.zsp(), misalignment), isParallel ? 1 : 0); break;
      case InstSpec::kOpMem16 : fillMemArray(dst, _nUnroll, x86::word_ptr(a.zsp(), misalignment), isParallel ? 2 : 0); break;
      case InstSpec::kOpMem32 : fillMemArray(dst, _nUnroll, x86::dword_ptr(a.zsp(), misalignment), isParallel ? 4 : 0); break;
      case InstSpec::kOpMem64 : fillMemArray(dst, _nUnroll, x86::qword_ptr(a.zsp(), misalignment), isParallel ? 8 : 0); break;
      case InstSpec::kOpMem128: fillMemArray(dst, _nUnroll, x86::xmmword_ptr(a.zsp(), misalignment), isParallel ? 16 : 0); break;
      case InstSpec::kOpMem256: fillMemArray(dst, _nUnroll, x86::ymmword_ptr(a.zsp(), misalignment), isParallel ? 32 : 0); break;
      case InstSpec::kOpMem512: fillMemArray(dst, _nUnroll, x86::zmmword_ptr(a.zsp(), misalignment), isParallel ? 64 : 0); break;

      case InstSpec::kOpVm32x : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[uint32_t(RegGroup::kVec)], x86::RegTraits<RegType::kX86_Xmm>::kSignature); break;
      case InstSpec::kOpVm32y : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[uint32_t(RegGroup::kVec)], x86::RegTraits<RegType::kX86_Ymm>::kSignature); break;
      case InstSpec::kOpVm32z : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[uint32_t(RegGroup::kVec)], x86::RegTraits<RegType::kX86_Zmm>::kSignature); break;
      case InstSpec::kOpVm64x : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[uint32_t(RegGroup::kVec)], x86::RegTraits<RegType::kX86_Xmm>::kSignature); break;
      case InstSpec::kOpVm64y : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[uint32_t(RegGroup::kVec)], x86::RegTraits<RegType::kX86_Ymm>::kSignature); break;
      case InstSpec::kOpVm64z : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[uint32_t(RegGroup::kVec)], x86::RegTraits<RegType::kX86_Zmm>::kSignature); break;
    }
  }

  Label L_Body = a.newLabel();
  Label L_End = a.newLabel();
  Label L_SubFn = a.newLabel();
  int stackOperationSize = 0;

  switch (instId) {
    case x86::Inst::kIdPush:
    case x86::Inst::kIdPop:
      // PUSH/POP modify the stack, we have to revert it in the inner loop.
      stackOperationSize = (_instSpec.get(0) == InstSpec::kOpGpw ||
                            _instSpec.get(0) == InstSpec::kOpMem16 ? 2 : int(a.registerSize())) * int(_nUnroll);
      break;

    case x86::Inst::kIdCall:
      if (_instSpec.get(0) == InstSpec::kOpGpd || _instSpec.get(0) == InstSpec::kOpGpq) {
        a.lea(a.zax(), x86::ptr(L_SubFn));
      }
      else if (_instSpec.get(0) == InstSpec::kOpMem32 || _instSpec.get(0) == InstSpec::kOpMem64) {
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

      if (is64Bit()) {
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
      if (!isGather && !isScatterAVX512) {
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

  switch (instId) {
    case x86::Inst::kIdVmaskmovpd:
    case x86::Inst::kIdVmaskmovps:
    case x86::Inst::kIdVpmaskmovd:
    case x86::Inst::kIdVpmaskmovq:
      a.vpxor(x86::xmm0, x86::xmm0, x86::xmm0);
      a.vpcmpeqd(x86::ymm1, x86::ymm1, x86::ymm1);
      a.vpsrldq(x86::ymm1, x86::ymm1, 8);

      for (i = 0; i < _nUnroll; i++)
        o1[i].as<x86::Reg>().setId(1);
      break;

    case x86::Inst::kIdMaskmovq:
    case x86::Inst::kIdMaskmovdqu:
    case x86::Inst::kIdVmaskmovdqu:
      a.lea(a.zdi(), x86::ptr(a.zsp()));
      for (uint32_t i = 0; i < _nUnroll; i++)
        o2[i] = x86::ptr(a.zdi());
      break;
  }

  a.test(rCnt, rCnt);
  a.jz(L_End);

  a.align(AlignMode::kCode, 64);
  a.bind(L_Body);

  if (instId == x86::Inst::kIdPop && !_overheadOnly)
    a.sub(a.zsp(), stackOperationSize);

  switch (instId) {
    case x86::Inst::kIdCall: {
      assert(opCount == 1);
      if (_overheadOnly)
        break;

      for (uint32_t n = 0; n < _nUnroll; n++) {
        if (_instSpec.get(0) == InstSpec::kOpRel)
          a.call(L_SubFn);
        else if (_instSpec.get(0) == InstSpec::kOpGpd || _instSpec.get(0) == InstSpec::kOpGpq)
          a.call(a.zax());
        else
          a.call(x86::ptr(a.zax()));
      }
      break;
    }

    case x86::Inst::kIdJmp: {
      assert(opCount == 1);
      if (_overheadOnly)
        break;

      for (uint32_t n = 0; n < _nUnroll; n++) {
        Label x = a.newLabel();
        a.jmp(x);
        a.bind(x);
      }
      break;
    }

    case x86::Inst::kIdDiv:
    case x86::Inst::kIdIdiv: {
      assert(opCount >= 2 && opCount <= 3);
      if (_overheadOnly)
        break;

      if (opCount == 2) {
        for (uint32_t n = 0; n < _nUnroll; n++) {
          if (n == 0)
            a.mov(x86::eax, 127);
          a.emit(instId, x86::ax, x86::cl);

          if (n + 1 != _nUnroll)
            a.mov(x86::eax, 127);
        }
      }

      if (opCount == 3) {
        for (uint32_t n = 0; n < _nUnroll; n++) {
          a.xor_(x86::edx, x86::edx);
          if (n == 0)
            a.mov(x86::eax, 32123);

          if (o2[n].isReg()) {
            x86::Reg r(o2[n].as<x86::Gp>());
            r.setId(x86::Gp::kIdCx);
            a.emit(instId, o0[n], o1[n], r);
          }
          else {
            a.emit(instId, o0[n], o1[n], o2[n]);
          }

          if (n + 1 != _nUnroll) {
            a.xor_(x86::edx, x86::edx);
            if (isParallel)
              a.mov(x86::eax, 32123);
          }
        }
      }

      break;
    }

    case x86::Inst::kIdMul:
    case x86::Inst::kIdImul: {
      assert(opCount >= 2 && opCount <= 3);
      if (_overheadOnly)
        break;

      if (opCount == 2) {
        for (uint32_t n = 0; n < _nUnroll; n++) {
          if (isParallel)
            a.mov(o0[n].as<x86::Gp>().r32(), o1[n].as<x86::Gp>().r32());
          a.emit(instId, o0[n], o1[n]);
        }
      }

      if (opCount == 3) {
        for (uint32_t n = 0; n < _nUnroll; n++) {
          if (isParallel && InstSpec::isImplicitOp(_instSpec.get(1)))
            a.mov(o1[n].as<x86::Gp>().r32(), o2[n].as<x86::Gp>().r32());
          a.emit(instId, o0[n], o1[n], o2[n]);
        }
      }

      break;
    }

    case x86::Inst::kIdLea: {
      assert(opCount >= 2 && opCount <= 4);
      if (_overheadOnly)
        break;

      if (opCount == 2) {
        for (uint32_t n = 0; n < _nUnroll; n++) {
          a.emit(instId, o0[n], x86::ptr(o1[n].as<x86::Gp>()));
        }
      }

      if (opCount == 3) {
        for (uint32_t n = 0; n < _nUnroll; n++) {
          x86::Mem m;
          if (o2[n].isReg())
            m = x86::ptr(o1[n].as<x86::Gp>(), o2[n].as<x86::Gp>());
          else
            m = x86::ptr(o1[n].as<x86::Gp>(), o2[n].as<Imm>().valueAs<int32_t>());

          if (_instSpec.isLeaScale())
            m.setShift(3);

          a.emit(instId, o0[n], m);

        }
      }

      if (opCount == 4) {
        for (uint32_t n = 0; n < _nUnroll; n++) {
          x86::Mem m = x86::ptr(o1[n].as<x86::Gp>(), o2[n].as<x86::Gp>(), 0, o3[n].as<Imm>().valueAs<int32_t>());
          if (_instSpec.isLeaScale())
            m.setShift(3);
          a.emit(instId, o0[n], m);
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
      if (isGatherAVX512) {
        for (uint32_t n = 0; n < _nUnroll; n++) {
          x86::Vec index = o1[n].as<x86::Vec>();
          x86::KReg pred = ox[n].as<x86::KReg>();
          x86::Vec dst = o0[n].as<x86::Vec>();

          a.kmovw(pred, x86::k7);
          if (!_overheadOnly)
            a.k(pred).emit(instId, dst, x86::ptr(gsBase, index, gsIndexShift));

          if (instId == x86::Inst::kIdVgatherqps || instId == x86::Inst::kIdVpgatherqd)
            a.vpmovzxdq(dst.cloneAs(index), dst);
        }
      }
      else {
        for (uint32_t n = 0; n < _nUnroll; n++) {
          x86::Vec index = o1[n].as<x86::Vec>();
          x86::Vec ones = gatherMask.cloneAs(o0[n].as<x86::Vec>());
          x86::Vec dst = o0[n].as<x86::Vec>();

          x86::Vec pred = x86::xmm6.cloneAs(ones);
          a.vmovdqa(pred, ones);

          if (!_overheadOnly)
            a.emit(instId, dst, x86::ptr(gsBase, index, gsIndexShift), pred);

          if (instId == x86::Inst::kIdVgatherqps || instId == x86::Inst::kIdVpgatherqd)
            a.vpmovzxdq(dst.cloneAs(index), dst);
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
      for (uint32_t n = 0; n < _nUnroll; n++) {
        x86::Vec index = x86::xmm7.cloneAs(o0[0].as<x86::Vec>());
        x86::Vec src = x86::xmm6.cloneAs(o1[0].as<x86::Vec>());
        x86::KReg pred = ox[n].as<x86::KReg>();

        a.kmovw(pred, x86::k7);
        if (!_overheadOnly)
          a.k(pred).emit(instId, x86::ptr(gsBase, index, gsIndexShift, n * 8), src);

        uint32_t indexCount = index.size() / (scatterIndexSize(instId) / 8u);
        uint32_t srcCount = src.size() / (scatterElementSize(instId) / 8u);
        uint32_t elementCount = asmjit::Support::min(indexCount, srcCount);

        // Read the last scattered write to create a dependency for another scatter.
        if (!isParallel) {
          if (index.isXmm())
            a.vpaddd(index, index, x86::ptr(gsBase, n * 8 + ((elementCount - 1) * 128))._1to4());
          else if (index.isYmm())
            a.vpaddd(index, index, x86::ptr(gsBase, n * 8 + ((elementCount - 1) * 128))._1to8());
          else
            a.vpaddd(index, index, x86::ptr(gsBase, n * 8 + ((elementCount - 1) * 128))._1to16());
        }
      }

      break;
    }

    // Instructions that don't require special care.
    default: {
      assert(opCount <= 6);

      // Special case for instructions where destination register type doesn't appear anywhere in source.
      if (!isParallel) {
        if (opCount >= 2 && o0[0].isReg()) {
          bool sameKind = false;
          bool specialInst = false;

          const x86::Reg& dst = o0[0].as<x86::Reg>();

          if (opCount >= 2 && (o1[0].isReg() && o1[0].as<BaseReg>().group() == dst.group()))
            sameKind = true;

          if (opCount >= 3 && (o2[0].isReg() && o2[0].as<BaseReg>().group() == dst.group()))
            sameKind = true;

          if (opCount >= 4 && (o3[0].isReg() && o3[0].as<BaseReg>().group() == dst.group()))
            sameKind = true;

          // These have the same kind in 'reg, reg' case, however, some registers are fixed so we workaround it this way.
          specialInst = (instId == x86::Inst::kIdCdq ||
                         instId == x86::Inst::kIdCdqe ||
                         instId == x86::Inst::kIdCqo ||
                         instId == x86::Inst::kIdCwd ||
                         instId == x86::Inst::kIdPop);

          if ((!sameKind && isWriteOnly(a.arch(), instId, _instSpec)) || specialInst) {
            for (uint32_t n = 0; n < _nUnroll; n++) {
              if (!_overheadOnly) {
                Operand ops[6] = { o0[0], o1[0], o2[0], o3[0], o4[0], o5[0] };
                a.emitOpArray(instId, ops, opCount);
              }

              auto emitSequentialOp = [&](const BaseReg& reg, bool isDst) {
                if (x86::Reg::isGp(reg)) {
                  if (isDst)
                    a.add(x86::eax, reg.as<x86::Gp>().r32());
                  else
                    a.add(reg.as<x86::Gp>().r32(), reg.as<x86::Gp>().r32());
                }
                else if (x86::Reg::isKReg(reg)) {
                  if (isDst)
                    a.korw(x86::k7, x86::k7, reg.as<x86::KReg>());
                  else
                    a.korw(reg.as<x86::KReg>(), x86::k7, reg.as<x86::KReg>());
                }
                else if (x86::Reg::isMm(reg)) {
                  if (isDst)
                    a.paddb(x86::mm7, reg.as<x86::Mm>());
                  else
                    a.paddb(reg.as<x86::Mm>(), reg.as<x86::Mm>());
                }
                else if (x86::Reg::isXmm(reg) && !instInfo.isVexOrEvex()) {
                  if (isDst)
                    a.paddb(x86::xmm7, reg.as<x86::Xmm>());
                  else
                    a.paddb(reg.as<x86::Xmm>(), reg.as<x86::Xmm>());
                }
                else if (x86::Reg::isVec(reg)) {
                  if (isDst)
                    a.vpaddb(x86::xmm7, x86::xmm7, reg.as<x86::Vec>().xmm());
                  else
                    a.vpaddb(reg.as<x86::Vec>().xmm(), x86::xmm7, reg.as<x86::Vec>().xmm());
                }
              };

              emitSequentialOp(dst, true);
              if (o1[0].isReg())
                emitSequentialOp(o1[0].as<BaseReg>(), false);
            }
            break;
          }
        }
      }

      if (_overheadOnly)
        break;

      if (opCount == 0) {
        for (uint32_t n = 0; n < _nUnroll; n++)
          a.emit(instId);
      }

      if (opCount == 1) {
        for (uint32_t n = 0; n < _nUnroll; n++)
          a.emit(instId, o0[n]);
      }

      if (opCount == 2) {
        for (uint32_t n = 0; n < _nUnroll; n++)
          a.emit(instId, o0[n], o1[n]);
      }

      if (opCount == 3) {
        for (uint32_t n = 0; n < _nUnroll; n++)
          a.emit(instId, o0[n], o1[n], o2[n]);
      }

      if (opCount == 4) {
        for (uint32_t n = 0; n < _nUnroll; n++)
          a.emit(instId, o0[n], o1[n], o2[n], o3[n]);
      }

      if (opCount == 5) {
        for (uint32_t n = 0; n < _nUnroll; n++)
          a.emit(instId, o0[n], o1[n], o2[n], o3[n], o4[n]);
      }

      if (opCount == 6) {
        for (uint32_t n = 0; n < _nUnroll; n++)
          a.emit(instId, o0[n], o1[n], o2[n], o3[n], o4[n], o5[n]);
      }
      break;
    }
  }

  if (instId == x86::Inst::kIdPush && !_overheadOnly)
    a.add(a.zsp(), stackOperationSize);

  a.sub(rCnt, 1);
  a.jnz(L_Body);
  a.bind(L_End);

  if (instId == x86::Inst::kIdCall) {
    Label L_RealEnd = a.newLabel();
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

void InstBench::afterBody(x86::Assembler& a) {
  if (isMMX(_instId, _instSpec))
    a.emms();

  if (isAVX(_instId, _instSpec))
    a.vzeroupper();
}

void InstBench::fillMemory32(x86::Assembler& a, x86::Gp baseAddress, uint32_t value, uint32_t n) {
  Label loop = a.newLabel();
  x86::Gp cnt = x86::edi;

  a.xor_(cnt, cnt);
  a.bind(loop);
  a.mov(x86::dword_ptr(a.zsp(), cnt, 2), value);
  a.inc(cnt);
  a.cmp(cnt, n);
  a.jne(loop);
}

} // {cult} namespace
