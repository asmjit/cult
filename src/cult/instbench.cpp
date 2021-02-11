#include "instbench.h"
#include "cpuutils.h"

#include <set>

namespace cult {

class InstSignatureIterator {
public:
  typedef asmjit::x86::InstDB::InstSignature InstSignature;
  typedef asmjit::x86::InstDB::OpSignature OpSignature;

  const InstSignature* _instSignature;
  const OpSignature* _opSigArray[asmjit::Globals::kMaxOpCount];
  uint32_t _opMaskArray[asmjit::Globals::kMaxOpCount];
  uint32_t _memMaskArray[asmjit::Globals::kMaxOpCount];
  uint32_t _opCount;
  uint32_t _filter;
  bool _isValid;

  static constexpr uint32_t kMaxOpCount = 6;

  static constexpr uint32_t kMemFilter =
    x86::InstDB::kMemOpAny   |
    x86::InstDB::kMemOpM8    |
    x86::InstDB::kMemOpM16   |
    x86::InstDB::kMemOpM32   |
    x86::InstDB::kMemOpM48   |
    x86::InstDB::kMemOpM64   |
    x86::InstDB::kMemOpM80   |
    x86::InstDB::kMemOpM128  |
    x86::InstDB::kMemOpM256  |
    x86::InstDB::kMemOpM512  |
    x86::InstDB::kMemOpM1024 |
    x86::InstDB::kMemOpVm32x |
    x86::InstDB::kMemOpVm32y |
    x86::InstDB::kMemOpVm32z |
    x86::InstDB::kMemOpVm64x |
    x86::InstDB::kMemOpVm64y |
    x86::InstDB::kMemOpVm64z ;


  inline InstSignatureIterator() { reset(); }
  inline InstSignatureIterator(const InstSignature* instSignature, uint32_t filter = 0xFFFFFFFFu) { init(instSignature, filter); }
  inline InstSignatureIterator(const InstSignatureIterator& other) { init(other); }

  inline void reset() { ::memset(this, 0, sizeof(*this)); }
  inline void init(const InstSignatureIterator& other) { ::memcpy(this, &other, sizeof(*this)); }

  void init(const InstSignature* instSignature, uint32_t filter = 0xFFFFFFFFu) {
    const OpSignature* opSigArray = asmjit::x86::InstDB::_opSignatureTable;
    uint32_t opCount = instSignature->opCount;

    _instSignature = instSignature;
    _opCount = opCount;
    _filter = filter;

    uint32_t i;
    uint32_t flags = 0u;

    for (i = 0; i < opCount; i++) {
      const OpSignature* opSig = &opSigArray[instSignature->operands[i]];
      flags = opSig->opFlags & _filter;
      if (!flags)
        break;
      _opSigArray[i] = opSig;
      _opMaskArray[i] = asmjit::Support::blsi(flags);

      if (opSig->memFlags & kMemFilter)
        _memMaskArray[i] = asmjit::Support::blsi(opSig->memFlags & kMemFilter) | (opSig->memFlags & ~kMemFilter);
      else
        _memMaskArray[i] = 0;
    }

    while (i < kMaxOpCount) {
      _opSigArray[i] = &opSigArray[0];
      _opMaskArray[i] = 0u;
      _memMaskArray[i] = 0u;
      i++;
    }

    _isValid = opCount == 0 || flags != 0;
  }

  inline bool isValid() const { return _isValid; }
  inline uint32_t opCount() const { return _opCount; }

  inline const uint32_t* opMaskArray() const { return _opMaskArray; }
  inline const OpSignature* const* opSigArray() const { return _opSigArray; }

  inline uint32_t opMask(uint32_t i) const { return _opMaskArray[i]; }
  inline uint32_t memMask(uint32_t i) const { return _memMaskArray[i]; }
  inline const OpSignature* opSig(uint32_t i) const { return _opSigArray[i]; }

  bool next() {
    uint32_t i = _opCount - 1u;
    for (;;) {
      if (i == 0xFFFFFFFFu) {
        _isValid = false;
        return false;
      }

      // Iterate over MemFlags.
      if (_memMaskArray[i]) {
        uint32_t prevBit = _memMaskArray[i] & kMemFilter;
        uint32_t allFlags = _opSigArray[i]->memFlags;

        uint32_t bitsToClear = prevBit | (prevBit - 1u);
        uint32_t remainingBits = allFlags & kMemFilter & ~bitsToClear;

        if (remainingBits) {
          _memMaskArray[i] = asmjit::Support::blsi(remainingBits) | (allFlags & ~kMemFilter);
          return true;
        }
        else {
          _memMaskArray[i] = asmjit::Support::blsi(allFlags & kMemFilter) | (allFlags & ~kMemFilter);
        }
      }

      // Iterate over OpFlags.
      {
        uint32_t prevBit = _opMaskArray[i];
        uint32_t allFlags = _opSigArray[i]->opFlags & _filter;

        uint32_t bitsToClear = prevBit | (prevBit - 1u);
        uint32_t remainingBits = allFlags & ~bitsToClear;

        if (remainingBits) {
          _opMaskArray[i] = asmjit::Support::blsi(remainingBits);
          return true;
        }
        else {
          _opMaskArray[i--] = asmjit::Support::blsi(allFlags);
        }
      }
    }
  }
};

// TODO: These require pretty special register pattern - not tested yet.
static bool isIgnoredInst(uint32_t instId) {
  return instId == x86::Inst::kIdVp4dpwssd ||
         instId == x86::Inst::kIdVp4dpwssds ||
         instId == x86::Inst::kIdV4fmaddps ||
         instId == x86::Inst::kIdV4fmaddss ||
         instId == x86::Inst::kIdV4fnmaddps ||
         instId == x86::Inst::kIdV4fnmaddss ||
         instId == x86::Inst::kIdVp2intersectd ||
         instId == x86::Inst::kIdVp2intersectq;
}

// Returns true when the instruciton is safe to be benchmarked.
//
// There is many general purpose instructions including system ones. We only
// benchmark those that may appear commonly in user code, but not in kernel.
static bool isSafeGpInst(uint32_t instId) {
  return instId == x86::Inst::kIdAdc      ||
         instId == x86::Inst::kIdAdcx     ||
         instId == x86::Inst::kIdAdd      ||
         instId == x86::Inst::kIdAdox     ||
         instId == x86::Inst::kIdAnd      ||
         instId == x86::Inst::kIdAndn     ||
         instId == x86::Inst::kIdBextr    ||
         instId == x86::Inst::kIdBlcfill  ||
         instId == x86::Inst::kIdBlci     ||
         instId == x86::Inst::kIdBlcic    ||
         instId == x86::Inst::kIdBlcmsk   ||
         instId == x86::Inst::kIdBlcs     ||
         instId == x86::Inst::kIdBlsfill  ||
         instId == x86::Inst::kIdBlsi     ||
         instId == x86::Inst::kIdBlsic    ||
         instId == x86::Inst::kIdBlsmsk   ||
         instId == x86::Inst::kIdBlsr     ||
         instId == x86::Inst::kIdBsf      ||
         instId == x86::Inst::kIdBsr      ||
         instId == x86::Inst::kIdBswap    ||
         instId == x86::Inst::kIdBt       ||
         instId == x86::Inst::kIdBtc      ||
         instId == x86::Inst::kIdBtr      ||
         instId == x86::Inst::kIdBts      ||
         instId == x86::Inst::kIdBzhi     ||
         instId == x86::Inst::kIdCbw      ||
         instId == x86::Inst::kIdCdq      ||
         instId == x86::Inst::kIdCdqe     ||
         instId == x86::Inst::kIdCmp      ||
         instId == x86::Inst::kIdCrc32    ||
         instId == x86::Inst::kIdCqo      ||
         instId == x86::Inst::kIdCwd      ||
         instId == x86::Inst::kIdCwde     ||
         instId == x86::Inst::kIdDec      ||
         instId == x86::Inst::kIdImul     ||
         instId == x86::Inst::kIdInc      ||
         instId == x86::Inst::kIdLzcnt    ||
         instId == x86::Inst::kIdMov      ||
         instId == x86::Inst::kIdMovbe    ||
         instId == x86::Inst::kIdMovsx    ||
         instId == x86::Inst::kIdMovsxd   ||
         instId == x86::Inst::kIdMovzx    ||
         instId == x86::Inst::kIdNeg      ||
         instId == x86::Inst::kIdNop      ||
         instId == x86::Inst::kIdNot      ||
         instId == x86::Inst::kIdOr       ||
         instId == x86::Inst::kIdPdep     ||
         instId == x86::Inst::kIdPext     ||
         instId == x86::Inst::kIdPop      ||
         instId == x86::Inst::kIdPopcnt   ||
         instId == x86::Inst::kIdPush     ||
         instId == x86::Inst::kIdRcl      ||
         instId == x86::Inst::kIdRcr      ||
         instId == x86::Inst::kIdRdrand   ||
         instId == x86::Inst::kIdRdseed   ||
         instId == x86::Inst::kIdRol      ||
         instId == x86::Inst::kIdRor      ||
         instId == x86::Inst::kIdRorx     ||
         instId == x86::Inst::kIdSar      ||
         instId == x86::Inst::kIdSarx     ||
         instId == x86::Inst::kIdSbb      ||
         instId == x86::Inst::kIdShl      ||
         instId == x86::Inst::kIdShld     ||
         instId == x86::Inst::kIdShlx     ||
         instId == x86::Inst::kIdShr      ||
         instId == x86::Inst::kIdShrd     ||
         instId == x86::Inst::kIdShrx     ||
         instId == x86::Inst::kIdSub      ||
         instId == x86::Inst::kIdT1mskc   ||
         instId == x86::Inst::kIdTest     ||
         instId == x86::Inst::kIdTzcnt    ||
         instId == x86::Inst::kIdTzmsk    ||
         instId == x86::Inst::kIdXadd     ||
         instId == x86::Inst::kIdXchg     ||
         instId == x86::Inst::kIdXor      ;
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

    default:
      return "(invalid)";
  }
}

static void fillRegScalar(Operand* dst, uint32_t count, const Operand_& op) {
  for (uint32_t i = 0; i < count; i++)
    dst[i] = op;
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
    dst[i] = BaseReg::fromSignatureAndId(rSign, rIdArray[rId]);
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
    _nParallel(0) {}

InstBench::~InstBench() {
}

void InstBench::run() {
  JSONBuilder& json = _app->json();

  uint64_t tsc_freq = CpuUtils::get_tsc_freq();

  if (_app->verbose()) {
    if (tsc_freq)
      printf("Detected TSC frequency: %llu\n", (unsigned long long)tsc_freq);
    printf("Benchmark (latency & reciprocal throughput):\n");
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

  for (uint32_t instId = instStart; instId < instEnd; instId++) {
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

      StringTmp<256> sb;
      if (instId == x86::Inst::kIdCall)
        sb.append("call+ret");
      else
        InstAPI::instIdToString(Environment::kArchHost, instId, sb);

      for (uint32_t i = 0; i < opCount; i++) {
        if (i == 0)
          sb.append(' ');
        else if (instId == x86::Inst::kIdLea)
          sb.append(i == 1 ? ", [" : " + ");
        else
          sb.append(", ");

        sb.append(instSpecOpAsString(instSpec.get(i)));
        if (instId == x86::Inst::kIdLea && i == opCount - 1)
          sb.append(']');
      }

      double overheadLat = testInstruction(instId, instSpec, 0, true);
      double overheadRcp = testInstruction(instId, instSpec, 1, true);

      double lat = testInstruction(instId, instSpec, 0, false);
      double rcp = testInstruction(instId, instSpec, 1, false);

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

  if (_app->verbose())
    printf("\n");

  json.closeArray(true);
}

void InstBench::classify(std::vector<InstSpec>& dst, uint32_t instId) {
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
    if (is64Bit())
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq));
    else
      dst.push_back(InstSpec::pack(InstSpec::kOpGpd));
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
    dst.push_back(InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpImm8));
    dst.push_back(InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpImm32));

    if (is64Bit()) {
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq));
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpImm8));
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpImm32));
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpGpq));
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpImm8));
      dst.push_back(InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpImm32));
    }
    return;
  }

  // Common cases based on instruction signatures.
  uint32_t modeMask = 0;
  uint32_t opFilter = x86::InstDB::kOpGpbLo   |
                      x86::InstDB::kOpGpw     |
                      x86::InstDB::kOpGpd     |
                      x86::InstDB::kOpGpq     |
                      x86::InstDB::kOpXmm     |
                      x86::InstDB::kOpYmm     |
                      x86::InstDB::kOpZmm     |
                      x86::InstDB::kOpMm      |
                      x86::InstDB::kOpKReg    |
                      x86::InstDB::kOpMem     |
                      x86::InstDB::kOpVm      |
                      x86::InstDB::kOpAllImm  ;

  if (Environment::kArchHost == Environment::kArchX86) {
    modeMask = x86::InstDB::kModeX86;
    opFilter &= ~x86::InstDB::kOpGpq;
  }
  else {
    modeMask = x86::InstDB::kModeX64;
  }

  const x86::InstDB::InstInfo& instInfo = x86::InstDB::infoById(instId);
  const x86::InstDB::CommonInfo& commonInfo = instInfo.commonInfo();

  const x86::InstDB::InstSignature* instSignature = commonInfo.signatureData();
  const x86::InstDB::InstSignature* iEnd = commonInfo.signatureEnd();

  // Iterate over all signatures and build the instruction we want to test.
  std::set<uint64_t> known;
  for (; instSignature != iEnd; instSignature++) {
    if (!(instSignature->modes & modeMask))
      continue;

    InstSignatureIterator it(instSignature, opFilter);
    while (it.isValid()) {
      Operand operands[6];
      uint32_t opCount = it.opCount();
      uint32_t instSpec[6] {};

      bool skip = false;
      bool vec = false;
      uint32_t immCount = 0;

      for (uint32_t opIndex = 0; opIndex < opCount; opIndex++) {
        uint32_t opMask = it.opMask(opIndex);
        uint32_t memMask = it.memMask(opIndex);

        const x86::InstDB::OpSignature* opSig = it.opSig(opIndex);

        if (opMask & x86::InstDB::kOpAllRegs) {
          x86::Reg reg;
          uint32_t regId = 0;

          if (Support::isPowerOf2(opSig->regMask))
            regId = Support::ctz(opSig->regMask);

          switch (opMask) {
            case x86::InstDB::kOpGpbLo: reg._initReg(x86::GpbLo::kSignature, regId); instSpec[opIndex] = InstSpec::kOpGpb; break;
            case x86::InstDB::kOpGpbHi: reg._initReg(x86::GpbHi::kSignature, regId); instSpec[opIndex] = InstSpec::kOpGpb; break;
            case x86::InstDB::kOpGpw  : reg._initReg(x86::Gpw  ::kSignature, regId); instSpec[opIndex] = InstSpec::kOpGpw; break;
            case x86::InstDB::kOpGpd  : reg._initReg(x86::Gpd  ::kSignature, regId); instSpec[opIndex] = InstSpec::kOpGpd; break;
            case x86::InstDB::kOpGpq  : reg._initReg(x86::Gpq  ::kSignature, regId); instSpec[opIndex] = InstSpec::kOpGpq; break;
            case x86::InstDB::kOpXmm  : reg._initReg(x86::Xmm  ::kSignature, regId); instSpec[opIndex] = InstSpec::kOpXmm; vec = true; break;
            case x86::InstDB::kOpYmm  : reg._initReg(x86::Ymm  ::kSignature, regId); instSpec[opIndex] = InstSpec::kOpYmm; vec = true; break;
            case x86::InstDB::kOpZmm  : reg._initReg(x86::Zmm  ::kSignature, regId); instSpec[opIndex] = InstSpec::kOpZmm; vec = true; break;
            case x86::InstDB::kOpMm   : reg._initReg(x86::Mm   ::kSignature, regId); instSpec[opIndex] = InstSpec::kOpMm; vec = true; break;
            case x86::InstDB::kOpKReg : reg._initReg(x86::KReg ::kSignature, 1    ); instSpec[opIndex] = InstSpec::kOpKReg; vec = true; break;
            default:
              printf("[!!] Unknown register operand: OpMask=0x%08X\n", opMask);
              skip = true;
              break;
          }

          if (Support::isPowerOf2(opSig->regMask)) {
            switch (opMask) {
              case x86::InstDB::kOpGpbLo: instSpec[opIndex] = InstSpec::kOpAl + regId; break;
              case x86::InstDB::kOpGpbHi: instSpec[opIndex] = InstSpec::kOpAl + regId; break;
              case x86::InstDB::kOpGpw  : instSpec[opIndex] = InstSpec::kOpAx + regId; break;
              case x86::InstDB::kOpGpd  : instSpec[opIndex] = InstSpec::kOpEax + regId; break;
              case x86::InstDB::kOpGpq  : instSpec[opIndex] = InstSpec::kOpRax + regId; break;
              case x86::InstDB::kOpXmm  : instSpec[opIndex] = InstSpec::kOpXmm0; break;
              default:
                printf("[!!] Unknown register operand: OpMask=0x%08X\n", opMask);
                skip = true;
                break;
            }
          }


          operands[opIndex] = reg;
        }
        else if (opMask & x86::InstDB::kOpMem) {
          // TODO:
          skip = true;
        }
        else if (opMask & x86::InstDB::kOpVm) {
          // TODO:
          skip = true;
        }
        else if (opMask & x86::InstDB::kOpAllImm) {
          operands[opIndex] = Imm(++immCount);
          if (opMask & (x86::InstDB::kOpI64 | x86::InstDB::kOpU64))
            instSpec[opIndex] = InstSpec::kOpImm64;
          else if (opMask & (x86::InstDB::kOpI32 | x86::InstDB::kOpU32))
            instSpec[opIndex] = InstSpec::kOpImm32;
          else if (opMask & (x86::InstDB::kOpI16 | x86::InstDB::kOpU16))
            instSpec[opIndex] = InstSpec::kOpImm16;
          else
            instSpec[opIndex] = InstSpec::kOpImm8;
        }
        else {
          skip = true;
        }
      }

      if (!skip) {
        if (vec || isSafeGpInst(instId)) {
          BaseInst baseInst(instId, 0);
          if (_canRun(baseInst, operands, opCount)) {
            InstSpec spec = InstSpec::pack(instSpec[0], instSpec[1], instSpec[2], instSpec[3], instSpec[4], instSpec[5]);
            if (known.find(spec.value) == known.end()) {
              known.insert(spec.value);
              dst.push_back(spec);
            }
          }
        }
      }

      it.next();
    }
  }
}

bool InstBench::isImplicit(uint32_t instId) {
  const x86::InstDB::InstInfo& instInfo = x86::InstDB::infoById(instId);
  const x86::InstDB::InstSignature* iSig = instInfo.signatureData();
  const x86::InstDB::InstSignature* iEnd = instInfo.signatureEnd();

  while (iSig != iEnd) {
    if (iSig->implicit)
      return true;
    iSig++;
  }

  return false;
}

bool InstBench::_canRun(const BaseInst& inst, const Operand_* operands, uint32_t count) const {
  using namespace asmjit;

  if (inst.id() == x86::Inst::kIdNone)
    return false;

  if (InstAPI::validate(Environment::kArchHost, inst, operands, count) != kErrorOk)
    return false;

  BaseFeatures features;
  if (InstAPI::queryFeatures(Environment::kArchHost, inst, operands, count, &features) != kErrorOk)
    return false;

  if (!_cpuInfo.features().hasAll(features))
    return false;

  return true;
}

uint32_t InstBench::numIterByInstId(uint32_t instId) {
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

double InstBench::testInstruction(uint32_t instId, InstSpec instSpec, uint32_t parallel, bool overheadOnly) {
  _instId = instId;
  _instSpec = instSpec;
  _nParallel = parallel ? 6 : 1;
  _overheadOnly = overheadOnly;

  Func func = compileFunc();
  if (!func) {
    String name;
    InstAPI::instIdToString(Environment::kArchHost, instId, name);
    printf("FAILED to compile function for '%s' instruction\n", name.data());
    return -1.0;
  }

  uint32_t nIter = numIterByInstId(_instId);

  // Consider a significant improvement 0.08 cycles per instruction (0.2 cycles in fast mode).
  uint32_t kSignificantImprovement = uint32_t(double(nIter) * (_app->_estimate ? 0.2 : 0.08));

  // If we called the function N times without a significant improvement we terminate the test.
  uint32_t kMaximumImprovementTries = _app->_estimate ? 1000 : 50000;

  uint32_t kMaxIterationCount = 1000000;

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
  if (isVec(_instId, _instSpec)) {
    // TODO: Need to know if the instruction works with ints/floats/doubles.
  }
}

void InstBench::compileBody(x86::Assembler& a, x86::Gp rCnt) {
  using namespace asmjit;

  uint32_t instId = _instId;
  const x86::InstDB::InstInfo& instInfo = x86::InstDB::infoById(instId);

  uint32_t rMask[32] = { 0 };

  rMask[x86::Reg::kGroupGp  ] = 0xFF & ~Support::bitMask(x86::Gp::kIdSp, rCnt.id());
  rMask[x86::Reg::kGroupVec ] = 0xFF;
  rMask[x86::Reg::kGroupMm  ] = 0xFF;
  rMask[x86::Reg::kGroupKReg] = 0xFE;

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

  while (regCount && _instSpec.get(regCount - 1) >= InstSpec::kOpImm8)
    regCount--;

  for (i = 0; i < regCount; i++) {
    switch (_instSpec.get(i)) {
      case InstSpec::kOpAl:
      case InstSpec::kOpAx:
      case InstSpec::kOpEax:
      case InstSpec::kOpRax:
        rMask[x86::Reg::kGroupGp] &= ~Support::bitMask(x86::Gp::kIdAx);
        break;

      case InstSpec::kOpBl:
      case InstSpec::kOpBx:
      case InstSpec::kOpEbx:
      case InstSpec::kOpRbx:
        rMask[x86::Reg::kGroupGp] &= ~Support::bitMask(x86::Gp::kIdBx);
        break;

      case InstSpec::kOpCl:
      case InstSpec::kOpCx:
      case InstSpec::kOpEcx:
      case InstSpec::kOpRcx:
        rMask[x86::Reg::kGroupGp] &= ~Support::bitMask(x86::Gp::kIdCx);
        break;

      case InstSpec::kOpDl:
      case InstSpec::kOpDx:
      case InstSpec::kOpEdx:
      case InstSpec::kOpRdx:
        rMask[x86::Reg::kGroupGp] &= ~Support::bitMask(x86::Gp::kIdDx);
        break;
    }
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
        if (!isParallel)
          rStart = (i < 2) ? 1 : 0;
        else
          rStart = (i < 2) ? 0 : 1;
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
      case InstSpec::kOpAl   : fillRegScalar(dst, _nUnroll, x86::al); break;
      case InstSpec::kOpBl   : fillRegScalar(dst, _nUnroll, x86::bl); break;
      case InstSpec::kOpCl   : fillRegScalar(dst, _nUnroll, x86::cl); break;
      case InstSpec::kOpDl   : fillRegScalar(dst, _nUnroll, x86::dl); break;

      case InstSpec::kOpAx   : fillRegScalar(dst, _nUnroll, x86::ax); break;
      case InstSpec::kOpBx   : fillRegScalar(dst, _nUnroll, x86::bx); break;
      case InstSpec::kOpCx   : fillRegScalar(dst, _nUnroll, x86::cx); break;
      case InstSpec::kOpDx   : fillRegScalar(dst, _nUnroll, x86::dx); break;

      case InstSpec::kOpEax  : fillRegScalar(dst, _nUnroll, x86::eax); break;
      case InstSpec::kOpEbx  : fillRegScalar(dst, _nUnroll, x86::ebx); break;
      case InstSpec::kOpEcx  : fillRegScalar(dst, _nUnroll, x86::ecx); break;
      case InstSpec::kOpEdx  : fillRegScalar(dst, _nUnroll, x86::edx); break;

      case InstSpec::kOpRax  : fillRegScalar(dst, _nUnroll, x86::rax); break;
      case InstSpec::kOpRbx  : fillRegScalar(dst, _nUnroll, x86::rbx); break;
      case InstSpec::kOpRcx  : fillRegScalar(dst, _nUnroll, x86::rcx); break;
      case InstSpec::kOpRdx  : fillRegScalar(dst, _nUnroll, x86::rdx); break;

      case InstSpec::kOpXmm0 : fillRegScalar(dst, _nUnroll, x86::xmm0); break;

      case InstSpec::kOpGpb  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[x86::Reg::kGroupGp  ], x86::RegTraits<x86::Reg::kTypeGpbLo>::kSignature); break;
      case InstSpec::kOpGpw  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[x86::Reg::kGroupGp  ], x86::RegTraits<x86::Reg::kTypeGpw>::kSignature); break;
      case InstSpec::kOpGpd  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[x86::Reg::kGroupGp  ], x86::RegTraits<x86::Reg::kTypeGpd>::kSignature); break;
      case InstSpec::kOpGpq  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[x86::Reg::kGroupGp  ], x86::RegTraits<x86::Reg::kTypeGpq>::kSignature); break;
      case InstSpec::kOpMm   : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[x86::Reg::kGroupMm  ], x86::RegTraits<x86::Reg::kTypeMm >::kSignature); break;
      case InstSpec::kOpXmm  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[x86::Reg::kGroupVec ], x86::RegTraits<x86::Reg::kTypeXmm>::kSignature); break;
      case InstSpec::kOpYmm  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[x86::Reg::kGroupVec ], x86::RegTraits<x86::Reg::kTypeYmm>::kSignature); break;
      case InstSpec::kOpZmm  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[x86::Reg::kGroupVec ], x86::RegTraits<x86::Reg::kTypeZmm>::kSignature); break;
      case InstSpec::kOpKReg : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[x86::Reg::kGroupKReg], x86::RegTraits<x86::Reg::kTypeKReg>::kSignature); break;

      case InstSpec::kOpImm8 : fillImmArray(dst, _nUnroll, 0, 1    , 15        ); break;
      case InstSpec::kOpImm16: fillImmArray(dst, _nUnroll, 1, 13099, 65535     ); break;
      case InstSpec::kOpImm32: fillImmArray(dst, _nUnroll, 1, 19231, 2000000000); break;
      case InstSpec::kOpImm64: fillImmArray(dst, _nUnroll, 1, 9876543219231, 0x0FFFFFFFFFFFFFFF); break;
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
      stackOperationSize = (_instSpec.get(0) == InstSpec::kOpGpw ? 2 : int(a.registerSize())) * int(_nUnroll);
      break;

    case x86::Inst::kIdCall:
      if (_instSpec.get(0) != InstSpec::kOpRel)
        a.lea(a.zax(), x86::ptr(L_SubFn));
      break;

    case x86::Inst::kIdCpuid:
      a.xor_(x86::eax, x86::eax);
      a.xor_(x86::ecx, x86::ecx);
      break;

    case x86::Inst::kIdXgetbv:
      a.xor_(x86::ecx, x86::ecx);
      break;

    default:
      // This will cost us some cycles, however, we really want some predictable state.
      a.mov(x86::eax, 999);
      a.mov(x86::ebx, 49182);
      a.mov(x86::ecx, 3); // Used by divisions, should be a small number.
      a.mov(x86::edx, 1193833);
      a.mov(x86::esi, 192822);
      a.mov(x86::edi, 1);
      break;
  }

  a.test(rCnt, rCnt);
  a.jz(L_End);

  a.align(kAlignCode, 64);
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
        else
          a.call(a.zax());
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

          x86::Reg r(o2[n].as<x86::Gp>());
          r.setId(x86::Gp::kIdCx);

          a.emit(instId, o0[n], o1[n], r);

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
          if (o2[n].isReg())
            a.emit(instId, o0[n], x86::ptr(o1[n].as<x86::Gp>(), o2[n].as<x86::Gp>()));
          else
            a.emit(instId, o0[n], x86::ptr(o1[n].as<x86::Gp>(), o2[n].as<Imm>().valueAs<int32_t>()));
        }
      }

      if (opCount == 4) {
        for (uint32_t n = 0; n < _nUnroll; n++) {
          a.emit(instId, o0[n], x86::ptr(o1[n].as<x86::Gp>(), o2[n].as<x86::Gp>(), 0, o3[n].as<Imm>().valueAs<int32_t>()));
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
                         instId == x86::Inst::kIdPop ||
                         instId == x86::Inst::kIdRol ||
                         instId == x86::Inst::kIdRor ||
                         instId == x86::Inst::kIdSar ||
                         instId == x86::Inst::kIdShl ||
                         instId == x86::Inst::kIdShr);

          if (!sameKind || specialInst) {
            for (uint32_t n = 0; n < _nUnroll; n++) {
              if (!_overheadOnly) {
                Operand ops[6] = { o0[0], o1[0], o2[0], o3[0], o4[0], o5[0] };
                a.emitOpArray(instId, ops, opCount);
              }

              if (x86::Reg::isGp(dst)) {
                a.add(x86::eax, dst.as<x86::Gp>().r32());
              }
              else if (x86::Reg::isKReg(dst)) {
                a.kaddb(x86::k7, dst.as<x86::KReg>(), dst.as<x86::KReg>());
              }
              else if (x86::Reg::isMm(dst)) {
                a.paddb(x86::mm7, dst.as<x86::Mm>());
              }
              else if (x86::Reg::isXmm(dst) && !instInfo.isVexOrEvex()) {
                a.paddb(x86::xmm7, dst.as<x86::Xmm>());
              }
              else if (x86::Reg::isVec(dst)) {
                a.vpaddb(x86::xmm7, dst.as<x86::Vec>().xmm(), dst.as<x86::Vec>().xmm());
              }
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
}

} // cult namespace
