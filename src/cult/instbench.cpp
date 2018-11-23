#include "./instbench.h"

namespace cult {

static bool isSafeGp(uint32_t instId) {
  return instId == x86::Inst::kIdAdd     ||
         instId == x86::Inst::kIdAdc     ||
         instId == x86::Inst::kIdAdcx    ||
         instId == x86::Inst::kIdAdox    ||
         instId == x86::Inst::kIdAnd     ||
         instId == x86::Inst::kIdAndn    ||
         instId == x86::Inst::kIdBextr   ||
         instId == x86::Inst::kIdBlcfill ||
         instId == x86::Inst::kIdBlci    ||
         instId == x86::Inst::kIdBlcic   ||
         instId == x86::Inst::kIdBlcmsk  ||
         instId == x86::Inst::kIdBlcs    ||
         instId == x86::Inst::kIdBlsfill ||
         instId == x86::Inst::kIdBlsi    ||
         instId == x86::Inst::kIdBlsic   ||
         instId == x86::Inst::kIdBlsmsk  ||
         instId == x86::Inst::kIdBlsr    ||
         instId == x86::Inst::kIdBsf     ||
         instId == x86::Inst::kIdBsr     ||
         instId == x86::Inst::kIdBsr     ||
         instId == x86::Inst::kIdBswap   ||
         instId == x86::Inst::kIdBt      ||
         instId == x86::Inst::kIdBtc     ||
         instId == x86::Inst::kIdBtr     ||
         instId == x86::Inst::kIdBts     ||
         instId == x86::Inst::kIdBzhi    ||
         instId == x86::Inst::kIdCmp     ||
         instId == x86::Inst::kIdCrc32   ||
         instId == x86::Inst::kIdDec     ||
         instId == x86::Inst::kIdImul    ||
         instId == x86::Inst::kIdInc     ||
         instId == x86::Inst::kIdLzcnt   ||
         instId == x86::Inst::kIdMov     ||
         instId == x86::Inst::kIdMovsx   ||
         instId == x86::Inst::kIdMovsxd  ||
         instId == x86::Inst::kIdMovzx   ||
         instId == x86::Inst::kIdMovbe   ||
         instId == x86::Inst::kIdNeg     ||
         instId == x86::Inst::kIdNot     ||
         instId == x86::Inst::kIdOr      ||
         instId == x86::Inst::kIdPdep    ||
         instId == x86::Inst::kIdPext    ||
         instId == x86::Inst::kIdPopcnt  ||
         instId == x86::Inst::kIdRcl     ||
         instId == x86::Inst::kIdRcr     ||
         instId == x86::Inst::kIdRol     ||
         instId == x86::Inst::kIdRor     ||
         instId == x86::Inst::kIdRorx    ||
         instId == x86::Inst::kIdSar     ||
         instId == x86::Inst::kIdSarx    ||
         instId == x86::Inst::kIdSbb     ||
         instId == x86::Inst::kIdShl     ||
         instId == x86::Inst::kIdShld    ||
         instId == x86::Inst::kIdShlx    ||
         instId == x86::Inst::kIdShr     ||
         instId == x86::Inst::kIdShrd    ||
         instId == x86::Inst::kIdShrx    ||
         instId == x86::Inst::kIdSub     ||
         instId == x86::Inst::kIdT1mskc  ||
         instId == x86::Inst::kIdTest    ||
         instId == x86::Inst::kIdTzcnt   ||
         instId == x86::Inst::kIdTzmsk   ||
         instId == x86::Inst::kIdXadd    ||
         instId == x86::Inst::kIdXchg    ||
         instId == x86::Inst::kIdXor     ;
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

static void fillRegArray(Operand* dst, uint32_t count, uint32_t rStart, uint32_t rInc, uint32_t rMask, uint32_t rSign) {
  uint32_t i;
  uint32_t rId = 0;

  while (!((1 << rId) & rMask) || rStart) {
    if (rStart && ((1 << rId) & rMask))
      rStart--;

    if (++rId >= 32)
      rId = 0;
  }

  for (i = 0; i < count; i++) {
    dst[i] = BaseReg(rSign, rId);

    rId = (rId + rInc) % 32;
    while (!((1 << rId) & rMask))
      if (++rId >= 32)
        rId = 0;
  }
}

static void fillImmArray(Operand* dst, uint32_t count, uint64_t start, uint64_t inc, uint64_t maxValue) {
  uint64_t n = start;

  for (uint32_t i = 0; i < count; i++) {
    dst[i] = Imm(n);
    n = (n + inc) % (maxValue + 1);
  }
}

// Round the result (either cycles or latency) to something more logical than `0.8766`.
static double roundResult(double x) noexcept {
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

InstBench::InstBench(App* app) noexcept
  : BaseBench(app),
    _instId(0),
    _instSpec(),
    _nUnroll(64),
    _nParallel(0) {}

InstBench::~InstBench() noexcept {
}

void InstBench::run() {
  JSONBuilder& json = _app->json();

  if (_app->verbose())
    printf("Benchmark:\n");

  json.beforeRecord()
      .addKey("instructions")
      .openArray();


  for (uint32_t instId = 1; instId < x86::Inst::_kIdCount; instId++) {
    ZoneVector<InstSpec> specs;
    classify(specs, instId);

    /*
    if (specs.size() == 0) {
      printf("MISSING SPEC: %s\n", x86::InstDB::nameById(instId));
    }
    */

    for (size_t i = 0; i < specs.size(); i++) {
      InstSpec instSpec = specs[i];

      StringTmp<256> sb;
      sb.appendString(x86::InstDB::nameById(instId));

      for (uint32_t i = 0; i < instSpec.count(); i++) {
        if (i == 0)
          sb.appendString(" ");
        else if (instId == x86::Inst::kIdLea)
          sb.appendString(i == 1 ? ", [" : " + ");
        else
          sb.appendString(", ");

        sb.appendString(instSpecOpAsString(instSpec.get(i)));
        if (instId == x86::Inst::kIdLea && i == instSpec.count() - 1)
          sb.appendString("]");
      }

      double lat = testInstruction(instId, instSpec, 0);
      double rcp = testInstruction(instId, instSpec, 1);

      if (_app->_round) {
        lat = roundResult(lat);
        rcp = roundResult(rcp);
      }

      if (_app->verbose())
        printf("  %-32s: Lat:%6.2f Rcp:%6.2f\n", sb.data(), lat, rcp);

      json.beforeRecord()
          .openObject()
          .addKey("inst").addString(sb.data()).alignTo(48)
          .addKey("lat").addDoublef("%6.2f", lat)
          .addKey("rcp").addDoublef("%6.2f", rcp)
          .closeObject();
    }

    specs.release(_app->allocator());
  }

  if (_app->verbose())
    printf("\n");

  json.closeArray(true);
}

void InstBench::classify(ZoneVector<InstSpec>& dst, uint32_t instId) {
  using namespace asmjit;

  ZoneAllocator* allocator = _app->allocator();

  // Handle special cases.
  if (instId == x86::Inst::kIdCpuid    ||
      instId == x86::Inst::kIdEmms     ||
      instId == x86::Inst::kIdFemms    ||
      instId == x86::Inst::kIdLfence   ||
      instId == x86::Inst::kIdMfence   ||
      instId == x86::Inst::kIdRdtsc    ||
      instId == x86::Inst::kIdRdtscp   ||
      instId == x86::Inst::kIdSfence   ||
      instId == x86::Inst::kIdVzeroall ||
      instId == x86::Inst::kIdVzeroupper) {
    if (canRun(instId))
      dst.append(allocator, InstSpec::pack(0));
    return;
  }

  if (instId == x86::Inst::kIdLea) {
    dst.append(allocator, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd));
    dst.append(allocator, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpImm8));
    dst.append(allocator, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpImm32));
    dst.append(allocator, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpGpd));
    dst.append(allocator, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpImm8));
    dst.append(allocator, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpImm32));

    if (is64Bit()) {
      dst.append(allocator, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq));
      dst.append(allocator, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpImm8));
      dst.append(allocator, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpImm32));
      dst.append(allocator, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpGpq));
      dst.append(allocator, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpImm8));
      dst.append(allocator, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpImm32));
    }
  }

  // Handle instructions that uses implicit register(s) here.
  if (isImplicit(instId)) {
    if (instId == x86::Inst::kIdBlendvpd ||
        instId == x86::Inst::kIdBlendvps ||
        instId == x86::Inst::kIdSha256rnds2 ||
        instId == x86::Inst::kIdPblendvb) {
      if (canRun(instId, x86::xmm2, x86::xmm1, x86::xmm0))
        dst.append(allocator, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpXmm, InstSpec::kOpXmm0));
    }

    if (instId == x86::Inst::kIdDiv ||
        instId == x86::Inst::kIdIdiv) {
      dst.append(allocator, InstSpec::pack(InstSpec::kOpAx, InstSpec::kOpCl));
      dst.append(allocator, InstSpec::pack(InstSpec::kOpDx, InstSpec::kOpAx, InstSpec::kOpCx));
      dst.append(allocator, InstSpec::pack(InstSpec::kOpEdx, InstSpec::kOpEax, InstSpec::kOpEcx));

      if (is64Bit())
        dst.append(allocator, InstSpec::pack(InstSpec::kOpRdx, InstSpec::kOpRax, InstSpec::kOpRcx));
    }

    return;
  }

  if (isSafeGp(instId)) {
    if (canRun(instId, x86::bl)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpb));
    if (canRun(instId, x86::bx)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpw));
    if (canRun(instId, x86::ebx)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpd));
    if (canRun(instId, x86::rbx)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpq));

    if (canRun(instId, x86::bl, x86::al)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpb, InstSpec::kOpGpb));
    if (canRun(instId, x86::bl, imm(6))) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpb, InstSpec::kOpImm8));

    if (canRun(instId, x86::bx, x86::di)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpw, InstSpec::kOpGpw));
    if (canRun(instId, x86::bx, imm(10000))) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpw, InstSpec::kOpImm16));
    else if (canRun(instId, x86::bx, imm(10)))  dst.append(allocator, InstSpec::pack(InstSpec::kOpGpw, InstSpec::kOpImm8));

    if (canRun(instId, x86::bx, x86::dl)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpw, InstSpec::kOpGpb));
    if (canRun(instId, x86::ebx, x86::dl)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpb));
    if (canRun(instId, x86::ebx, x86::dx)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpw));
    if (canRun(instId, x86::rbx, x86::dl)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpb));
    if (canRun(instId, x86::rbx, x86::dx)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpw));
    if (canRun(instId, x86::rbx, x86::edx)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpd));

    if (canRun(instId, x86::ebx, x86::edi)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd));
    if (canRun(instId, x86::ebx, imm(100000))) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpImm32));
    else if (canRun(instId, x86::ebx, imm(10))) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpImm8));

    if (canRun(instId, x86::rbx, x86::rdi)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq));
    if (canRun(instId, x86::rbx, imm(100000))) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpImm32));
    else if (canRun(instId, x86::rbx, imm(10))) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpImm8));
    if (canRun(instId, x86::rbx, imm(10000000000ull))) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpImm64));

    if (canRun(instId, x86::bl, x86::dl, x86::al)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpb, InstSpec::kOpGpb, InstSpec::kOpGpb));
    if (canRun(instId, x86::bx, x86::di, x86::si)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpw, InstSpec::kOpGpw, InstSpec::kOpGpw));
    if (canRun(instId, x86::ebx, x86::edi, x86::esi)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpGpd));
    if (canRun(instId, x86::rbx, x86::rdi, x86::rsi)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpGpq));
  }

  if (canRun(instId, x86::mm1, imm(1))) dst.append(allocator, InstSpec::pack(InstSpec::kOpMm, InstSpec::kOpImm8));
  if (canRun(instId, x86::mm1, x86::mm2)) dst.append(allocator, InstSpec::pack(InstSpec::kOpMm, InstSpec::kOpMm));
  if (canRun(instId, x86::mm1, x86::mm2, imm(1))) dst.append(allocator, InstSpec::pack(InstSpec::kOpMm, InstSpec::kOpMm, InstSpec::kOpImm8));

  if (canRun(instId, x86::xmm3, imm(1))) dst.append(allocator, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpImm8));
  if (canRun(instId, x86::xmm3, x86::xmm5)) dst.append(allocator, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpXmm));
  if (canRun(instId, x86::xmm3, x86::ymm5)) dst.append(allocator, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpYmm));
  if (canRun(instId, x86::ymm3, x86::xmm5)) dst.append(allocator, InstSpec::pack(InstSpec::kOpYmm, InstSpec::kOpXmm));
  if (canRun(instId, x86::ymm3, x86::ymm5)) dst.append(allocator, InstSpec::pack(InstSpec::kOpYmm, InstSpec::kOpYmm));

  if (canRun(instId, x86::xmm3, x86::xmm5, imm(1))) dst.append(allocator, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpXmm, InstSpec::kOpImm8));
  if (canRun(instId, x86::xmm3, x86::ymm5, imm(1))) dst.append(allocator, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpYmm, InstSpec::kOpImm8));
  if (canRun(instId, x86::ymm3, x86::xmm5, imm(1))) dst.append(allocator, InstSpec::pack(InstSpec::kOpYmm, InstSpec::kOpXmm, InstSpec::kOpImm8));
  if (canRun(instId, x86::ymm3, x86::ymm5, imm(1))) dst.append(allocator, InstSpec::pack(InstSpec::kOpYmm, InstSpec::kOpYmm, InstSpec::kOpImm8));
  if (canRun(instId, x86::xmm3, x86::xmm5, x86::xmm2)) dst.append(allocator, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpXmm, InstSpec::kOpXmm));
  if (canRun(instId, x86::ymm3, x86::ymm5, x86::xmm2)) dst.append(allocator, InstSpec::pack(InstSpec::kOpYmm, InstSpec::kOpYmm, InstSpec::kOpXmm));
  if (canRun(instId, x86::ymm3, x86::ymm5, x86::ymm2)) dst.append(allocator, InstSpec::pack(InstSpec::kOpYmm, InstSpec::kOpYmm, InstSpec::kOpYmm));

  if (canRun(instId, x86::xmm3, x86::xmm5, x86::xmm2, imm(1))) dst.append(allocator, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpXmm, InstSpec::kOpXmm, InstSpec::kOpImm8));
  if (canRun(instId, x86::ymm3, x86::ymm5, x86::xmm2, imm(1))) dst.append(allocator, InstSpec::pack(InstSpec::kOpYmm, InstSpec::kOpYmm, InstSpec::kOpXmm, InstSpec::kOpImm8));
  if (canRun(instId, x86::ymm3, x86::ymm5, x86::ymm2, imm(1))) dst.append(allocator, InstSpec::pack(InstSpec::kOpYmm, InstSpec::kOpYmm, InstSpec::kOpYmm, InstSpec::kOpImm8));

  if (canRun(instId, x86::xmm3, x86::xmm5, x86::xmm2, x86::xmm6)) dst.append(allocator, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpXmm, InstSpec::kOpXmm, InstSpec::kOpXmm));
  if (canRun(instId, x86::ymm3, x86::ymm5, x86::ymm2, x86::ymm6)) dst.append(allocator, InstSpec::pack(InstSpec::kOpYmm, InstSpec::kOpYmm, InstSpec::kOpYmm, InstSpec::kOpYmm));
}

bool InstBench::isImplicit(uint32_t instId) noexcept {
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

bool InstBench::_canRun(const BaseInst& inst, const Operand_* operands, uint32_t count) const noexcept {
  using namespace asmjit;

  if (inst.id() == x86::Inst::kIdNone)
    return false;

  if (BaseInst::validate(ArchInfo::kIdHost, inst, operands, count) != kErrorOk)
    return false;

  BaseFeatures features;
  if (BaseInst::queryFeatures(ArchInfo::kIdHost, inst, operands, count, features) != kErrorOk)
    return false;

  if (!_cpuInfo.features().hasAll(features))
    return false;

  return true;
}

uint32_t InstBench::numIterByInstId(uint32_t instId) noexcept {
  switch (instId) {
    // Return low number for instructions that are really slow.
    case x86::Inst::kIdCpuid:
      return 10;

    default:
      return 200;
  }
}

double InstBench::testInstruction(uint32_t instId, InstSpec instSpec, uint32_t parallel) {
  _instId = instId;
  _instSpec = instSpec;
  _nParallel = parallel ? 6 : 1;

  Func func = compileFunc();
  if (!func) {
    printf("FAILED to compile function for '%s' instruction\n", x86::InstDB::nameById(instId));
    return -1.0;
  }

  uint32_t nIter = numIterByInstId(_instId);
  uint64_t cycles;

  func(nIter, &cycles);

  uint64_t localWorse = cycles;
  uint32_t nSimilar = 0;
  uint64_t nAcceptThreshold = cycles / 1024;
  uint64_t nSimilarThreshold = cycles / 2048;

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

void InstBench::beforeBody(x86::Assembler& a) {
  if (isVec(_instId, _instSpec)) {
    // TODO: Need to know if the instruction works with ints/floats/doubles.
  }
}

void InstBench::compileBody(x86::Assembler& a, x86::Gp rCnt) {
  using namespace asmjit;

  uint32_t instId = _instId;
  uint32_t rMask[32] = { 0 };

  rMask[x86::Reg::kGroupGp  ] = Support::fillTrailingBits(a.gpCount()) &
                               ~Support::bitMask(x86::Gp::kIdSp, rCnt.id());
  rMask[x86::Reg::kGroupVec ] = Support::fillTrailingBits(a.gpCount());
  rMask[x86::Reg::kGroupMm  ] = 0xFF;
  rMask[x86::Reg::kGroupKReg] = 0xFE;

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
      case InstSpec::kOpGpb  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[x86::Reg::kGroupGp], x86::RegTraits<x86::Reg::kTypeGpbLo>::kSignature); break;
      case InstSpec::kOpGpw  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[x86::Reg::kGroupGp], x86::RegTraits<x86::Reg::kTypeGpw>::kSignature); break;
      case InstSpec::kOpGpd  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[x86::Reg::kGroupGp], x86::RegTraits<x86::Reg::kTypeGpd>::kSignature); break;
      case InstSpec::kOpGpq  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[x86::Reg::kGroupGp], x86::RegTraits<x86::Reg::kTypeGpq>::kSignature); break;

      case InstSpec::kOpAl   : fillRegArray(dst, _nUnroll, x86::Gp::kIdAx, 0, 0xFFFF, x86::RegTraits<x86::Reg::kTypeGpbLo>::kSignature); break;
      case InstSpec::kOpBl   : fillRegArray(dst, _nUnroll, x86::Gp::kIdBx, 0, 0xFFFF, x86::RegTraits<x86::Reg::kTypeGpbLo>::kSignature); break;
      case InstSpec::kOpCl   : fillRegArray(dst, _nUnroll, x86::Gp::kIdCx, 0, 0xFFFF, x86::RegTraits<x86::Reg::kTypeGpbLo>::kSignature); break;
      case InstSpec::kOpDl   : fillRegArray(dst, _nUnroll, x86::Gp::kIdDx, 0, 0xFFFF, x86::RegTraits<x86::Reg::kTypeGpbLo>::kSignature); break;

      case InstSpec::kOpAx   : fillRegArray(dst, _nUnroll, x86::Gp::kIdAx, 0, 0xFFFF, x86::RegTraits<x86::Reg::kTypeGpw>::kSignature); break;
      case InstSpec::kOpBx   : fillRegArray(dst, _nUnroll, x86::Gp::kIdBx, 0, 0xFFFF, x86::RegTraits<x86::Reg::kTypeGpw>::kSignature); break;
      case InstSpec::kOpCx   : fillRegArray(dst, _nUnroll, x86::Gp::kIdCx, 0, 0xFFFF, x86::RegTraits<x86::Reg::kTypeGpw>::kSignature); break;
      case InstSpec::kOpDx   : fillRegArray(dst, _nUnroll, x86::Gp::kIdDx, 0, 0xFFFF, x86::RegTraits<x86::Reg::kTypeGpw>::kSignature); break;

      case InstSpec::kOpEax  : fillRegArray(dst, _nUnroll, x86::Gp::kIdAx, 0, 0xFFFF, x86::RegTraits<x86::Reg::kTypeGpd>::kSignature); break;
      case InstSpec::kOpEbx  : fillRegArray(dst, _nUnroll, x86::Gp::kIdBx, 0, 0xFFFF, x86::RegTraits<x86::Reg::kTypeGpd>::kSignature); break;
      case InstSpec::kOpEcx  : fillRegArray(dst, _nUnroll, x86::Gp::kIdCx, 0, 0xFFFF, x86::RegTraits<x86::Reg::kTypeGpd>::kSignature); break;
      case InstSpec::kOpEdx  : fillRegArray(dst, _nUnroll, x86::Gp::kIdDx, 0, 0xFFFF, x86::RegTraits<x86::Reg::kTypeGpd>::kSignature); break;

      case InstSpec::kOpRax  : fillRegArray(dst, _nUnroll, x86::Gp::kIdAx, 0, 0xFFFF, x86::RegTraits<x86::Reg::kTypeGpq>::kSignature); break;
      case InstSpec::kOpRbx  : fillRegArray(dst, _nUnroll, x86::Gp::kIdBx, 0, 0xFFFF, x86::RegTraits<x86::Reg::kTypeGpq>::kSignature); break;
      case InstSpec::kOpRcx  : fillRegArray(dst, _nUnroll, x86::Gp::kIdCx, 0, 0xFFFF, x86::RegTraits<x86::Reg::kTypeGpq>::kSignature); break;
      case InstSpec::kOpRdx  : fillRegArray(dst, _nUnroll, x86::Gp::kIdDx, 0, 0xFFFF, x86::RegTraits<x86::Reg::kTypeGpq>::kSignature); break;

      case InstSpec::kOpMm   : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[x86::Reg::kGroupMm ], x86::RegTraits<x86::Reg::kTypeMm >::kSignature); break;
      case InstSpec::kOpXmm  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[x86::Reg::kGroupVec], x86::RegTraits<x86::Reg::kTypeXmm>::kSignature); break;
      case InstSpec::kOpXmm0 : fillRegArray(dst, _nUnroll, 0     , 0   , rMask[x86::Reg::kGroupVec], x86::RegTraits<x86::Reg::kTypeXmm>::kSignature); break;
      case InstSpec::kOpYmm  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[x86::Reg::kGroupVec], x86::RegTraits<x86::Reg::kTypeYmm>::kSignature); break;
      case InstSpec::kOpZmm  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[x86::Reg::kGroupVec], x86::RegTraits<x86::Reg::kTypeZmm>::kSignature); break;

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
        if (instId == x86::Inst::kIdDiv || instId == x86::Inst::kIdIdiv) {
          a.emit(x86::Inst::kIdXor, x86::ah, x86::ah);
          if (n == 0)
            a.emit(x86::Inst::kIdMov, x86::ecx, 3);

          if (n == 0)
            a.emit(x86::Inst::kIdMov, x86::al, 133);
          else if (n % 3 == 0)
            a.emit(x86::Inst::kIdAdd, x86::al, 133);
          else if (isParallel)
            a.emit(x86::Inst::kIdMov, x86::eax, 133);
        }

        if (instId == x86::Inst::kIdLea)
          a.emit(instId, o0[n], x86::ptr(o1[n].as<x86::Gp>()));
        else
          a.emit(instId, o0[n], o1[n]);
      }
      break;
    }

    case 3: {
      for (uint32_t n = 0; n < _nUnroll; n++) {
        if (instId == x86::Inst::kIdDiv || instId == x86::Inst::kIdIdiv) {
          a.emit(x86::Inst::kIdXor, x86::edx, x86::edx);
          if (n == 0)
            a.emit(x86::Inst::kIdMov, x86::ecx, 3);

          if (n == 0)
            a.emit(x86::Inst::kIdMov, x86::eax, 133);
          else if (n % 3 == 0)
            a.emit(x86::Inst::kIdAdd, x86::eax, 133);
          else if (isParallel)
            a.emit(x86::Inst::kIdMov, x86::eax, int64_t(123456789));
        }

        if (instId == x86::Inst::kIdLea && o2[n].isReg())
          a.emit(instId, o0[n], x86::ptr(o1[n].as<x86::Gp>(), o2[n].as<x86::Gp>()));
        else if (instId == x86::Inst::kIdLea && o2[n].isImm())
          a.emit(instId, o0[n], x86::ptr(o1[n].as<x86::Gp>(), o2[n].as<Imm>().i32()));
        else
          a.emit(instId, o0[n], o1[n], o2[n]);
      }
      break;
    }

    case 4: {
      for (uint32_t n = 0; n < _nUnroll; n++) {
        if (instId == x86::Inst::kIdLea)
          a.emit(instId, o0[n], x86::ptr(o1[n].as<x86::Gp>(), o2[n].as<x86::Gp>(), 0, o3[n].as<Imm>().i32()));
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

void InstBench::afterBody(x86::Assembler& a) {
  if (isMMX(_instId, _instSpec))
    a.emms();
}

} // cult namespace
