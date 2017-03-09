#include "./benchcycles.h"

namespace cult {

static bool isSafeGp(uint32_t instId) {
  return instId == X86Inst::kIdAdd     ||
         instId == X86Inst::kIdAdc     ||
         instId == X86Inst::kIdAdcx    ||
         instId == X86Inst::kIdAdox    ||
         instId == X86Inst::kIdAnd     ||
         instId == X86Inst::kIdAndn    ||
         instId == X86Inst::kIdBextr   ||
         instId == X86Inst::kIdBlcfill ||
         instId == X86Inst::kIdBlci    ||
         instId == X86Inst::kIdBlcic   ||
         instId == X86Inst::kIdBlcmsk  ||
         instId == X86Inst::kIdBlcs    ||
         instId == X86Inst::kIdBlsfill ||
         instId == X86Inst::kIdBlsi    ||
         instId == X86Inst::kIdBlsic   ||
         instId == X86Inst::kIdBlsmsk  ||
         instId == X86Inst::kIdBlsr    ||
         instId == X86Inst::kIdBsf     ||
         instId == X86Inst::kIdBsr     ||
         instId == X86Inst::kIdBsr     ||
         instId == X86Inst::kIdBswap   ||
         instId == X86Inst::kIdBt      ||
         instId == X86Inst::kIdBtc     ||
         instId == X86Inst::kIdBtr     ||
         instId == X86Inst::kIdBts     ||
         instId == X86Inst::kIdBzhi    ||
         instId == X86Inst::kIdCrc32   ||
         instId == X86Inst::kIdDec     ||
         instId == X86Inst::kIdImul    ||
         instId == X86Inst::kIdInc     ||
         instId == X86Inst::kIdLzcnt   ||
         instId == X86Inst::kIdMov     ||
         instId == X86Inst::kIdMovbe   ||
         instId == X86Inst::kIdNeg     ||
         instId == X86Inst::kIdNot     ||
         instId == X86Inst::kIdOr      ||
         instId == X86Inst::kIdPdep    ||
         instId == X86Inst::kIdPext    ||
         instId == X86Inst::kIdPopcnt  ||
         instId == X86Inst::kIdRcl     ||
         instId == X86Inst::kIdRcr     ||
         instId == X86Inst::kIdRol     ||
         instId == X86Inst::kIdRor     ||
         instId == X86Inst::kIdRorx    ||
         instId == X86Inst::kIdSar     ||
         instId == X86Inst::kIdSarx    ||
         instId == X86Inst::kIdSbb     ||
         instId == X86Inst::kIdShl     ||
         instId == X86Inst::kIdShlx    ||
         instId == X86Inst::kIdShr     ||
         instId == X86Inst::kIdShrx    ||
         instId == X86Inst::kIdSub     ||
         instId == X86Inst::kIdT1mskc  ||
         instId == X86Inst::kIdTest    ||
         instId == X86Inst::kIdTzcnt   ||
         instId == X86Inst::kIdTzmsk   ||
         instId == X86Inst::kIdXchg    ||
         instId == X86Inst::kIdXor     ;
}

static const char* instSpecOpAsString(uint32_t instSpecOp) {
  switch (instSpecOp) {
    case InstSpec::kOpNone : return "none";

    case InstSpec::kOpAl   :
    case InstSpec::kOpBl   :
    case InstSpec::kOpCl   :
    case InstSpec::kOpDl   :
    case InstSpec::kOpGpb  : return "r8";

    case InstSpec::kOpAx   :
    case InstSpec::kOpBx   :
    case InstSpec::kOpCx   :
    case InstSpec::kOpDx   :
    case InstSpec::kOpGpw  : return "r16";

    case InstSpec::kOpEax  :
    case InstSpec::kOpEbx  :
    case InstSpec::kOpEcx  :
    case InstSpec::kOpEdx  :
    case InstSpec::kOpGpd  : return "r32";

    case InstSpec::kOpRax  :
    case InstSpec::kOpRbx  :
    case InstSpec::kOpRcx  :
    case InstSpec::kOpRdx  :
    case InstSpec::kOpGpq  : return "r64";

    case InstSpec::kOpMm   : return "mm";

    case InstSpec::kOpXmm0 :
    case InstSpec::kOpXmm  : return "xmm";
    case InstSpec::kOpYmm  : return "ymm";
    case InstSpec::kOpZmm  : return "zmm";

    case InstSpec::kOpImm8 : return "i8";
    case InstSpec::kOpImm16: return "i16";
    case InstSpec::kOpImm32: return "i32";
    case InstSpec::kOpImm64: return "i64";

    default:
      return "(invalid)";
  }
}

static void fillRegArray(asmjit::Operand* dst, uint32_t count, uint32_t rStart, uint32_t rInc, uint32_t rMask, uint32_t rSign) {
  uint32_t i;
  uint32_t rId = 0;

  while (!((1 << rId) & rMask) || rStart) {
    if (rStart && ((1 << rId) & rMask))
      rStart--;

    if (++rId >= 32)
      rId = 0;
  }

  for (i = 0; i < count; i++) {
    dst[i] = asmjit::Reg(asmjit::Init, rSign, rId);

    rId = (rId + rInc) % 32;
    while (!((1 << rId) & rMask))
      if (++rId >= 32)
        rId = 0;
  }
}

static void fillImmArray(asmjit::Operand* dst, uint32_t count, uint64_t start, uint64_t inc, uint64_t maxValue) {
  uint64_t n = start;

  for (uint32_t i = 0; i < count; i++) {
    dst[i] = asmjit::Imm(n);
    n = (n + inc) % (maxValue + 1);
  }
}

// Round the result (either cycles or latency) to something more logical than `0.8766`.
static double roundResult(double x) noexcept {
  double n = static_cast<double>(static_cast<int>(x));
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
// [cult::BenchCycles]
// ============================================================================

BenchCycles::BenchCycles(App* app) noexcept
  : BenchBase(app),
    _instId(0),
    _instSpec(),
    _nUnroll(64),
    _nParallel(0) {}

BenchCycles::~BenchCycles() noexcept {
}

void BenchCycles::run() {
  JSONBuilder& json = _app->json();

  if (_app->verbose())
    printf("Benchmark:\n");

  json.beforeRecord()
      .addKey("instructions")
      .openArray();


  for (uint32_t instId = 1; instId < X86Inst::_kIdCount; instId++) {
    ZoneVector<InstSpec> specs;
    classify(specs, instId);

    /*
    if (specs.getLength() == 0) {
      printf("MISSING SPEC: %s\n", X86Inst::getNameById(instId));
    }
    */

    for (size_t i = 0; i < specs.getLength(); i++) {
      InstSpec instSpec = specs[i];

      StringBuilderTmp<256> sb;
      sb.appendString(X86Inst::getNameById(instId));

      for (uint32_t i = 0; i < instSpec.count(); i++) {
        sb.appendString(i == 0 ? " " : ", ");
        sb.appendString(instSpecOpAsString(instSpec.get(i)));
      }

      double lat = testInstruction(instId, instSpec, 0);
      double rcp = testInstruction(instId, instSpec, 1);

      if (_app->_round) {
        lat = roundResult(lat);
        rcp = roundResult(rcp);
      }

      if (_app->verbose())
        printf("  %-32s: Lat:%6.2f Rcp:%6.2f\n", sb.getData(), lat, rcp);

      json.beforeRecord()
          .openObject()
          .addKey("inst").addString(sb.getData()).alignTo(48)
          .addKey("lat").addDoublef("%6.2f", lat)
          .addKey("rcp").addDoublef("%6.2f", rcp)
          .closeObject();
    }

    specs.release(&_app->_heap);
  }

  if (_app->verbose())
    printf("\n");

  json.closeArray();
}

void BenchCycles::classify(ZoneVector<InstSpec>& dst, uint32_t instId) {
  using namespace asmjit;

  ZoneHeap* heap = &_app->_heap;

  // Handle special cases.
  if (instId == X86Inst::kIdCpuid    ||
      instId == X86Inst::kIdEmms     ||
      instId == X86Inst::kIdFemms    ||
      instId == X86Inst::kIdLfence   ||
      instId == X86Inst::kIdMfence   ||
      instId == X86Inst::kIdRdtsc    ||
      instId == X86Inst::kIdRdtscp   ||
      instId == X86Inst::kIdSfence   ||
      instId == X86Inst::kIdVzeroall ||
      instId == X86Inst::kIdVzeroupper) {
    if (canRun(instId))
      dst.append(heap, InstSpec::pack(0));
    return;
  }

  if (instId == X86Inst::kIdLea) {
    dst.append(heap, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd));
    dst.append(heap, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpImm8));
    dst.append(heap, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpImm32));
    dst.append(heap, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpGpd));
    dst.append(heap, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpImm8));
    dst.append(heap, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpImm32));

    if (is64Bit()) {
      dst.append(heap, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq));
      dst.append(heap, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpImm8));
      dst.append(heap, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpImm32));
      dst.append(heap, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpGpq));
      dst.append(heap, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpImm8));
      dst.append(heap, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpImm32));
    }
  }

  // Handle instructions that uses implicit register(s) here.
  if (isImplicit(instId)) {
    if (instId == X86Inst::kIdBlendvpd ||
        instId == X86Inst::kIdBlendvps ||
        instId == X86Inst::kIdSha256rnds2 ||
        instId == X86Inst::kIdPblendvb) {
      if (canRun(instId, x86::xmm2, x86::xmm1, x86::xmm0))
        dst.append(heap, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpXmm, InstSpec::kOpXmm0));
    }

    if (instId == X86Inst::kIdDiv ||
        instId == X86Inst::kIdIdiv) {
      dst.append(heap, InstSpec::pack(InstSpec::kOpAx, InstSpec::kOpCl));
      dst.append(heap, InstSpec::pack(InstSpec::kOpDx, InstSpec::kOpAx, InstSpec::kOpCx));
      dst.append(heap, InstSpec::pack(InstSpec::kOpEdx, InstSpec::kOpEax, InstSpec::kOpEcx));

      if (is64Bit())
        dst.append(heap, InstSpec::pack(InstSpec::kOpRdx, InstSpec::kOpRax, InstSpec::kOpRcx));
    }

    return;
  }

  if (isSafeGp(instId)) {
    if (canRun(instId, x86::bl)) dst.append(heap, InstSpec::pack(InstSpec::kOpGpb));
    if (canRun(instId, x86::bx)) dst.append(heap, InstSpec::pack(InstSpec::kOpGpw));
    if (canRun(instId, x86::ebx)) dst.append(heap, InstSpec::pack(InstSpec::kOpGpd));
    if (canRun(instId, x86::rbx)) dst.append(heap, InstSpec::pack(InstSpec::kOpGpq));

    if (canRun(instId, x86::bl, x86::al))
      dst.append(heap, InstSpec::pack(InstSpec::kOpGpb, InstSpec::kOpGpb));

    if (canRun(instId, x86::bl, imm(6)))
      dst.append(heap, InstSpec::pack(InstSpec::kOpGpb, InstSpec::kOpImm8));

    if (canRun(instId, x86::bx, x86::di))
      dst.append(heap, InstSpec::pack(InstSpec::kOpGpw, InstSpec::kOpGpw));

    if (canRun(instId, x86::bx, imm(10000)))
      dst.append(heap, InstSpec::pack(InstSpec::kOpGpw, InstSpec::kOpImm16));
    else if (canRun(instId, x86::bx, imm(10)))
      dst.append(heap, InstSpec::pack(InstSpec::kOpGpw, InstSpec::kOpImm8));

    if (canRun(instId, x86::ebx, x86::edi))
      dst.append(heap, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd));

    if (canRun(instId, x86::ebx, imm(100000)))
      dst.append(heap, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpImm32));
    else if (canRun(instId, x86::ebx, imm(10)))
      dst.append(heap, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpImm8));

    if (canRun(instId, x86::rbx, x86::rdi))
      dst.append(heap, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq));

    if (canRun(instId, x86::rbx, imm(100000)))
      dst.append(heap, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpImm32));
    else if (canRun(instId, x86::rbx, imm(10)))
      dst.append(heap, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpImm8));

    if (canRun(instId, x86::rbx, imm(10000000000ull)))
      dst.append(heap, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpImm64));

    if (canRun(instId, x86::bl, x86::dl, x86::al)) dst.append(heap, InstSpec::pack(InstSpec::kOpGpb, InstSpec::kOpGpb, InstSpec::kOpGpb));
    if (canRun(instId, x86::bx, x86::di, x86::si)) dst.append(heap, InstSpec::pack(InstSpec::kOpGpw, InstSpec::kOpGpw, InstSpec::kOpGpw));
    if (canRun(instId, x86::ebx, x86::edi, x86::esi)) dst.append(heap, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpGpd));
    if (canRun(instId, x86::rbx, x86::rdi, x86::rsi)) dst.append(heap, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpGpq));
  }

  if (canRun(instId, x86::mm1, imm(1))) dst.append(heap, InstSpec::pack(InstSpec::kOpMm, InstSpec::kOpImm8));
  if (canRun(instId, x86::mm1, x86::mm2)) dst.append(heap, InstSpec::pack(InstSpec::kOpMm, InstSpec::kOpMm));
  if (canRun(instId, x86::mm1, x86::mm2, imm(1))) dst.append(heap, InstSpec::pack(InstSpec::kOpMm, InstSpec::kOpMm, InstSpec::kOpImm8));

  if (canRun(instId, x86::xmm3, imm(1))) dst.append(heap, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpImm8));
  if (canRun(instId, x86::xmm3, x86::xmm5)) dst.append(heap, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpXmm));
  if (canRun(instId, x86::xmm3, x86::ymm5)) dst.append(heap, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpYmm));
  if (canRun(instId, x86::ymm3, x86::xmm5)) dst.append(heap, InstSpec::pack(InstSpec::kOpYmm, InstSpec::kOpXmm));
  if (canRun(instId, x86::ymm3, x86::ymm5)) dst.append(heap, InstSpec::pack(InstSpec::kOpYmm, InstSpec::kOpYmm));

  if (canRun(instId, x86::xmm3, x86::xmm5, imm(1))) dst.append(heap, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpXmm, InstSpec::kOpImm8));
  if (canRun(instId, x86::xmm3, x86::ymm5, imm(1))) dst.append(heap, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpYmm, InstSpec::kOpImm8));
  if (canRun(instId, x86::ymm3, x86::xmm5, imm(1))) dst.append(heap, InstSpec::pack(InstSpec::kOpYmm, InstSpec::kOpXmm, InstSpec::kOpImm8));
  if (canRun(instId, x86::ymm3, x86::ymm5, imm(1))) dst.append(heap, InstSpec::pack(InstSpec::kOpYmm, InstSpec::kOpYmm, InstSpec::kOpImm8));
  if (canRun(instId, x86::xmm3, x86::xmm5, x86::xmm2)) dst.append(heap, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpXmm, InstSpec::kOpXmm));
  if (canRun(instId, x86::ymm3, x86::ymm5, x86::xmm2)) dst.append(heap, InstSpec::pack(InstSpec::kOpYmm, InstSpec::kOpYmm, InstSpec::kOpXmm));
  if (canRun(instId, x86::ymm3, x86::ymm5, x86::ymm2)) dst.append(heap, InstSpec::pack(InstSpec::kOpYmm, InstSpec::kOpYmm, InstSpec::kOpYmm));

  if (canRun(instId, x86::xmm3, x86::xmm5, x86::xmm2, imm(1))) dst.append(heap, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpXmm, InstSpec::kOpXmm, InstSpec::kOpImm8));
  if (canRun(instId, x86::ymm3, x86::ymm5, x86::xmm2, imm(1))) dst.append(heap, InstSpec::pack(InstSpec::kOpYmm, InstSpec::kOpYmm, InstSpec::kOpXmm, InstSpec::kOpImm8));
  if (canRun(instId, x86::ymm3, x86::ymm5, x86::ymm2, imm(1))) dst.append(heap, InstSpec::pack(InstSpec::kOpYmm, InstSpec::kOpYmm, InstSpec::kOpYmm, InstSpec::kOpImm8));

  if (canRun(instId, x86::xmm3, x86::xmm5, x86::xmm2, x86::xmm6)) dst.append(heap, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpXmm, InstSpec::kOpXmm, InstSpec::kOpXmm));
  if (canRun(instId, x86::ymm3, x86::ymm5, x86::ymm2, x86::ymm6)) dst.append(heap, InstSpec::pack(InstSpec::kOpYmm, InstSpec::kOpYmm, InstSpec::kOpYmm, InstSpec::kOpYmm));
}

bool BenchCycles::isImplicit(uint32_t instId) noexcept {
  const X86Inst& inst = X86Inst::getInst(instId);

  const X86Inst::ISignature* iSig = inst.getISignatureData();
  const X86Inst::ISignature* iEnd = inst.getISignatureEnd();

  while (iSig != iEnd) {
    if (iSig->implicit)
      return true;
    iSig++;
  }

  return false;
}

bool BenchCycles::_canRun(const Inst::Detail& detail, const Operand_* operands, uint32_t count) const noexcept {
  using namespace asmjit;

  if (detail.instId == X86Inst::kIdNone)
    return false;

  if (Inst::validate(ArchInfo::kTypeHost, detail, operands, count) != kErrorOk)
    return false;

  CpuFeatures featuresRequired;
  if (Inst::checkFeatures(ArchInfo::kTypeHost, detail, operands, count, featuresRequired) != kErrorOk)
    return false;

  if (!_cpuInfo.getFeatures().hasAll(featuresRequired))
    return false;

  return true;
}

uint32_t BenchCycles::getNumIters(uint32_t instId) noexcept {
  switch (instId) {
    // Return low number for instructions that are pretty slow.
    case X86Inst::kIdCpuid:
      return 10;

    default:
      return 200;
  }
}

double BenchCycles::testInstruction(uint32_t instId, InstSpec instSpec, uint32_t parallel) {
  _instId = instId;
  _instSpec = instSpec;
  _nParallel = parallel ? 6 : 1;

  Func func = compileFunc();
  if (!func) {
    printf("FAILED to compile function for '%s' instruction\n", X86Inst::getNameById(instId));
    return -1.0;
  }

  uint32_t nIter = getNumIters(_instId);
  uint64_t cycles;

  func(nIter, &cycles);

  uint64_t localWorse = cycles;

  uint64_t nAcceptThreshold = cycles / 1024;
  uint64_t nSimilarThreshold = cycles / 2048;

  uint32_t nSimilar = 0;
  for (uint32_t i = 0; i < 1000000; i++) {
    uint64_t local;
    func(nIter, &local);

    if (local < cycles) {
      if (cycles - localWorse >= nSimilarThreshold) {
        nSimilar = 0;
        localWorse = local;
      }

      cycles = local;
      nAcceptThreshold = cycles / 1024;
      nSimilarThreshold = cycles / 2048;

      // printf("Best: %u: %u\n", i, (unsigned int)cycles);
    }
    else if ((local - cycles) <= nAcceptThreshold) {
      if (++nSimilar == 1000)
        break;
    }
  }

  releaseFunc(func);
  return double(cycles) / (double(nIter * _nUnroll));
}

void BenchCycles::beforeBody(X86Assembler& a) {
  if (isVec(_instId, _instSpec)) {
    // TODO: Need to know if the instruction works with ints/floats/doubles.
  }
}

void BenchCycles::compileBody(X86Assembler& a, X86Gp rCnt) {
  using namespace asmjit;

  uint32_t instId = _instId;
  uint32_t rMask[32] = { 0 };

  rMask[X86Reg::kKindGp ] = asmjit::Utils::bits(a.getGpCount()) &
                           ~asmjit::Utils::mask(X86Gp::kIdSp)   &
                           ~asmjit::Utils::mask(rCnt.getId())   ;
  rMask[X86Reg::kKindVec] = asmjit::Utils::bits(a.getGpCount());
  rMask[X86Reg::kKindMm ] = 0xFF;
  rMask[X86Reg::kKindK  ] = 0xFE;

  Operand* o0 = static_cast<Operand*>(::calloc(1, sizeof(Operand) * _nUnroll));
  Operand* o1 = static_cast<Operand*>(::calloc(1, sizeof(Operand) * _nUnroll));
  Operand* o2 = static_cast<Operand*>(::calloc(1, sizeof(Operand) * _nUnroll));
  Operand* o3 = static_cast<Operand*>(::calloc(1, sizeof(Operand) * _nUnroll));

  bool isParallel = _nParallel > 1;
  uint32_t numOps = _instSpec.count();
  uint32_t numRegs = numOps;

  while (numRegs && _instSpec.get(numRegs - 1) >= InstSpec::kOpImm8)
    numRegs--;

  for (uint32_t i = 0; i < numOps; i++) {
    uint32_t spec = _instSpec.get(i);
    Operand* dst = i == 0 ? o0 :
                   i == 1 ? o1 :
                   i == 2 ? o2 : o3;

    uint32_t rStart = 0;
    uint32_t rInc = 1;

    switch (numRegs) {
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
      //       INST v6, v0
      //       INST v7, v1
      //       INST v8, v2
      //       ...
      case 2:
        if (i == 0)
          rStart = isParallel ? _nParallel : uint32_t(1);
        else
          rStart = 0;
        break;

      // Patterns we want to generate:
      //   - Sequential:
      //       INST v1, v1, v0
      //       INST v2, v2, v1
      //       INST v3, v3, v2
      //       ...
      //   - Parallel:
      //       INST v6, v6, v0
      //       INST v7, v7, v1
      //       INST v8, v8, v2
      //       ...
      case 3:
        if (i <= 1)
          rStart = isParallel ? _nParallel : uint32_t(1);
        else
          rStart = 0;
        break;
    }

    switch (spec) {
      case InstSpec::kOpGpb  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[X86Reg::kKindGp ], X86RegTraits<X86Reg::kRegGpbLo>::kSignature); break;
      case InstSpec::kOpGpw  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[X86Reg::kKindGp ], X86RegTraits<X86Reg::kRegGpw>::kSignature); break;
      case InstSpec::kOpGpd  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[X86Reg::kKindGp ], X86RegTraits<X86Reg::kRegGpd>::kSignature); break;
      case InstSpec::kOpGpq  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[X86Reg::kKindGp ], X86RegTraits<X86Reg::kRegGpq>::kSignature); break;

      case InstSpec::kOpAl   : fillRegArray(dst, _nUnroll, X86Gp::kIdAx, 0, 0xFFFF, X86RegTraits<X86Reg::kRegGpbLo>::kSignature); break;
      case InstSpec::kOpBl   : fillRegArray(dst, _nUnroll, X86Gp::kIdBx, 0, 0xFFFF, X86RegTraits<X86Reg::kRegGpbLo>::kSignature); break;
      case InstSpec::kOpCl   : fillRegArray(dst, _nUnroll, X86Gp::kIdCx, 0, 0xFFFF, X86RegTraits<X86Reg::kRegGpbLo>::kSignature); break;
      case InstSpec::kOpDl   : fillRegArray(dst, _nUnroll, X86Gp::kIdDx, 0, 0xFFFF, X86RegTraits<X86Reg::kRegGpbLo>::kSignature); break;

      case InstSpec::kOpAx   : fillRegArray(dst, _nUnroll, X86Gp::kIdAx, 0, 0xFFFF, X86RegTraits<X86Reg::kRegGpw>::kSignature); break;
      case InstSpec::kOpBx   : fillRegArray(dst, _nUnroll, X86Gp::kIdBx, 0, 0xFFFF, X86RegTraits<X86Reg::kRegGpw>::kSignature); break;
      case InstSpec::kOpCx   : fillRegArray(dst, _nUnroll, X86Gp::kIdCx, 0, 0xFFFF, X86RegTraits<X86Reg::kRegGpw>::kSignature); break;
      case InstSpec::kOpDx   : fillRegArray(dst, _nUnroll, X86Gp::kIdDx, 0, 0xFFFF, X86RegTraits<X86Reg::kRegGpw>::kSignature); break;

      case InstSpec::kOpEax  : fillRegArray(dst, _nUnroll, X86Gp::kIdAx, 0, 0xFFFF, X86RegTraits<X86Reg::kRegGpd>::kSignature); break;
      case InstSpec::kOpEbx  : fillRegArray(dst, _nUnroll, X86Gp::kIdBx, 0, 0xFFFF, X86RegTraits<X86Reg::kRegGpd>::kSignature); break;
      case InstSpec::kOpEcx  : fillRegArray(dst, _nUnroll, X86Gp::kIdCx, 0, 0xFFFF, X86RegTraits<X86Reg::kRegGpd>::kSignature); break;
      case InstSpec::kOpEdx  : fillRegArray(dst, _nUnroll, X86Gp::kIdDx, 0, 0xFFFF, X86RegTraits<X86Reg::kRegGpd>::kSignature); break;

      case InstSpec::kOpRax  : fillRegArray(dst, _nUnroll, X86Gp::kIdAx, 0, 0xFFFF, X86RegTraits<X86Reg::kRegGpq>::kSignature); break;
      case InstSpec::kOpRbx  : fillRegArray(dst, _nUnroll, X86Gp::kIdBx, 0, 0xFFFF, X86RegTraits<X86Reg::kRegGpq>::kSignature); break;
      case InstSpec::kOpRcx  : fillRegArray(dst, _nUnroll, X86Gp::kIdCx, 0, 0xFFFF, X86RegTraits<X86Reg::kRegGpq>::kSignature); break;
      case InstSpec::kOpRdx  : fillRegArray(dst, _nUnroll, X86Gp::kIdDx, 0, 0xFFFF, X86RegTraits<X86Reg::kRegGpq>::kSignature); break;

      case InstSpec::kOpMm   : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[X86Reg::kKindMm ], X86RegTraits<X86Reg::kRegMm >::kSignature); break;
      case InstSpec::kOpXmm  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[X86Reg::kKindVec], X86RegTraits<X86Reg::kRegXmm>::kSignature); break;
      case InstSpec::kOpXmm0 : fillRegArray(dst, _nUnroll, 0     , 0   , rMask[X86Reg::kKindVec], X86RegTraits<X86Reg::kRegXmm>::kSignature); break;
      case InstSpec::kOpYmm  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[X86Reg::kKindVec], X86RegTraits<X86Reg::kRegYmm>::kSignature); break;
      case InstSpec::kOpZmm  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[X86Reg::kKindVec], X86RegTraits<X86Reg::kRegZmm>::kSignature); break;

      case InstSpec::kOpImm8 : fillImmArray(dst, _nUnroll, 0, 1    , 15        ); break;
      case InstSpec::kOpImm16: fillImmArray(dst, _nUnroll, 1, 13099, 65535     ); break;
      case InstSpec::kOpImm32: fillImmArray(dst, _nUnroll, 1, 19231, 2000000000); break;
      case InstSpec::kOpImm64: fillImmArray(dst, _nUnroll, 1, 9876543219231llu, 0x0FFFFFFFFFFFFFFFllu); break;
    }
  }

  Label L_Body = a.newLabel();
  Label L_End = a.newLabel();

  a.test(rCnt, rCnt);
  a.jz(L_End);

  a.align(kAlignCode, 16);
  a.bind(L_Body);

  assert(numOps <= 4);
  switch (numOps) {
    case 0: {
      for (uint32_t n = 0; n < _nUnroll; n++) {
        a.emit(instId);
      }
      break;
    }

    case 1: {
      for (uint32_t n = 0; n < _nUnroll; n++) {
        a.emit(instId, o0[n]);
      }
      break;
    }

    case 2: {
      for (uint32_t n = 0; n < _nUnroll; n++) {
        if (instId == X86Inst::kIdDiv || instId == X86Inst::kIdIdiv) {
          a.emit(X86Inst::kIdXor, x86::ah, x86::ah);
          if (n == 0)
            a.emit(X86Inst::kIdMov, x86::ecx, 3);

          if (n == 0)
            a.emit(X86Inst::kIdMov, x86::al, 133);
          else if (n % 3 == 0)
            a.emit(X86Inst::kIdAdd, x86::al, 133);
          else if (isParallel)
            a.emit(X86Inst::kIdMov, x86::eax, 133);
        }

        if (instId == X86Inst::kIdLea)
          a.emit(instId, o0[n], x86::ptr(o1[n].as<X86Gp>()));
        else
          a.emit(instId, o0[n], o1[n]);
      }
      break;
    }

    case 3: {
      for (uint32_t n = 0; n < _nUnroll; n++) {
        if (instId == X86Inst::kIdDiv || instId == X86Inst::kIdIdiv) {
          a.emit(X86Inst::kIdXor, x86::edx, x86::edx);
          if (n == 0)
            a.emit(X86Inst::kIdMov, x86::ecx, 3);

          if (n == 0)
            a.emit(X86Inst::kIdMov, x86::eax, 133);
          else if (n % 3 == 0)
            a.emit(X86Inst::kIdAdd, x86::eax, 133);
          else if (isParallel)
            a.emit(X86Inst::kIdMov, x86::eax, static_cast<int64_t>(123456789));
        }

        if (instId == X86Inst::kIdLea && o2[n].isReg())
          a.emit(instId, o0[n], x86::ptr(o1[n].as<X86Gp>(), o2[n].as<X86Gp>()));
        else if (instId == X86Inst::kIdLea && o2[n].isImm())
          a.emit(instId, o0[n], x86::ptr(o1[n].as<X86Gp>(), o2[n].as<Imm>().getInt32()));
        else
          a.emit(instId, o0[n], o1[n], o2[n]);
      }
      break;
    }

    case 4: {
      for (uint32_t n = 0; n < _nUnroll; n++) {
        if (instId == X86Inst::kIdLea)
          a.emit(instId, o0[n], x86::ptr(o1[n].as<X86Gp>(), o2[n].as<X86Gp>(), 0, o3[n].as<Imm>().getInt32()));
        else
          a.emit(instId, o0[n], o1[n], o2[n], o3[n]);
      }
      break;
    }
  }

  a.sub(rCnt, 1);
  a.jnz(L_Body);
  a.bind(L_End);

  ::free(o0);
  ::free(o1);
  ::free(o2);
  ::free(o3);
}

void BenchCycles::afterBody(X86Assembler& a) {
  if (isMMX(_instId, _instSpec))
    a.emms();
}

} // cult namespace
