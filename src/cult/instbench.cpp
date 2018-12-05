#include "./instbench.h"

namespace cult {

static bool isSafeGp(uint32_t instId) {
  return instId == x86::Inst::kIdAdd      ||
         instId == x86::Inst::kIdAdc      ||
         instId == x86::Inst::kIdAdcx     ||
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
         instId == x86::Inst::kIdMovsx    ||
         instId == x86::Inst::kIdMovsxd   ||
         instId == x86::Inst::kIdMovzx    ||
         instId == x86::Inst::kIdMovbe    ||
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
         instId == x86::Inst::kIdXor     ;
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
    dst[i] = BaseReg(rSign, rIdArray[rId]);
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
      uint32_t opCount = instSpec.count();

      StringTmp<256> sb;
      if (instId == x86::Inst::kIdCall)
        sb.appendString("call+ret");
      else
        sb.appendString(x86::InstDB::nameById(instId));

      for (uint32_t i = 0; i < opCount; i++) {
        if (i == 0)
          sb.appendString(" ");
        else if (instId == x86::Inst::kIdLea)
          sb.appendString(i == 1 ? ", [" : " + ");
        else
          sb.appendString(", ");

        sb.appendString(instSpecOpAsString(instSpec.get(i)));
        if (instId == x86::Inst::kIdLea && i == opCount - 1)
          sb.appendString("]");
      }

      double lat = testInstruction(instId, instSpec, 0);
      double rcp = testInstruction(instId, instSpec, 1);

      if (_app->_round) {
        lat = roundResult(lat);
        rcp = roundResult(rcp);
      }

      // Some tests are probably skewed. If this happens the latency is the throughput.
      if (rcp > lat)
        lat = rcp;

      if (_app->verbose())
        printf("  %-38s: Lat:%6.2f Rcp:%6.2f\n", sb.data(), lat, rcp);

      json.beforeRecord()
          .openObject()
          .addKey("inst").addString(sb.data()).alignTo(52)
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
      instId == x86::Inst::kIdXgetbv   ||
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
    return;
  }

  if (instId == x86::Inst::kIdCall) {
    dst.append(allocator, InstSpec::pack(InstSpec::kOpRel));
    if (is64Bit())
      dst.append(allocator, InstSpec::pack(InstSpec::kOpGpq));
    else
      dst.append(allocator, InstSpec::pack(InstSpec::kOpGpd));
    return;
  }

  if (instId == x86::Inst::kIdJmp) {
    dst.append(allocator, InstSpec::pack(InstSpec::kOpRel));
    return;
  }

  // Handle instructions that uses implicit register(s) here.
  if (isImplicit(instId)) {
    if (instId == x86::Inst::kIdCbw)
      dst.append(allocator, InstSpec::pack(InstSpec::kOpAx));

    if (instId == x86::Inst::kIdCdq)
      dst.append(allocator, InstSpec::pack(InstSpec::kOpEdx, InstSpec::kOpEax));

    if (instId == x86::Inst::kIdCwd)
      dst.append(allocator, InstSpec::pack(InstSpec::kOpDx, InstSpec::kOpAx));

    if (instId == x86::Inst::kIdCwde)
      dst.append(allocator, InstSpec::pack(InstSpec::kOpEax));

    if (instId == x86::Inst::kIdCdqe && is64Bit())
      dst.append(allocator, InstSpec::pack(InstSpec::kOpRax));

    if (instId == x86::Inst::kIdCqo && is64Bit())
      dst.append(allocator, InstSpec::pack(InstSpec::kOpRdx, InstSpec::kOpRax));

    if (instId == x86::Inst::kIdDiv || instId == x86::Inst::kIdIdiv ||
        instId == x86::Inst::kIdMul || instId == x86::Inst::kIdImul) {
      dst.append(allocator, InstSpec::pack(InstSpec::kOpAx, InstSpec::kOpGpb));
      dst.append(allocator, InstSpec::pack(InstSpec::kOpDx, InstSpec::kOpAx, InstSpec::kOpGpw));
      dst.append(allocator, InstSpec::pack(InstSpec::kOpEdx, InstSpec::kOpEax, InstSpec::kOpGpd));

      if (is64Bit())
        dst.append(allocator, InstSpec::pack(InstSpec::kOpRdx, InstSpec::kOpRax, InstSpec::kOpGpq));
    }

    if (instId == x86::Inst::kIdMulx) {
    if (canRun(instId, x86::eax, x86::eax, x86::eax, x86::edx))
      dst.append(allocator, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpEdx));
      if (is64Bit())
        dst.append(allocator, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpRdx));
    }

    if (instId == x86::Inst::kIdBlendvpd ||
        instId == x86::Inst::kIdBlendvps ||
        instId == x86::Inst::kIdSha256rnds2 ||
        instId == x86::Inst::kIdPblendvb) {
      if (canRun(instId, x86::xmm2, x86::xmm1, x86::xmm0))
        dst.append(allocator, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpXmm, InstSpec::kOpXmm0));
    }

    if (instId == x86::Inst::kIdPcmpistri ||
        instId == x86::Inst::kIdVpcmpistri) {
      if (canRun(instId, x86::xmm0, x86::xmm0, imm(0), x86::ecx))
        dst.append(allocator, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpXmm, InstSpec::kOpImm8, InstSpec::kOpEcx));
    }

    if (instId == x86::Inst::kIdPcmpistrm ||
        instId == x86::Inst::kIdVpcmpistrm) {
      if (canRun(instId, x86::xmm0, x86::xmm0, imm(0), x86::xmm0))
        dst.append(allocator, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpXmm, InstSpec::kOpImm8, InstSpec::kOpXmm0));
    }

    if (instId == x86::Inst::kIdPcmpestri ||
        instId == x86::Inst::kIdVpcmpestri) {
      if (canRun(instId, x86::xmm0, x86::xmm0, imm(0), x86::ecx, x86::eax, x86::edx))
        dst.append(allocator, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpXmm, InstSpec::kOpImm8, InstSpec::kOpEcx, InstSpec::kOpEax, InstSpec::kOpEdx));
    }

    if (instId == x86::Inst::kIdPcmpestrm ||
        instId == x86::Inst::kIdVpcmpestrm) {
      if (canRun(instId, x86::xmm0, x86::xmm0, imm(0), x86::xmm0, x86::eax, x86::edx))
        dst.append(allocator, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpXmm, InstSpec::kOpImm8, InstSpec::kOpXmm0, InstSpec::kOpEax, InstSpec::kOpEdx));
    }

    // Imul has also a variant that doesn't use implicit registers.
    if (instId != x86::Inst::kIdImul)
      return;
  }

  if (isSafeGp(instId)) {
    if (instId != x86::Inst::kIdImul) {
      if (canRun(instId, x86::bl)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpb));
      if (canRun(instId, x86::bx)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpw));
      if (canRun(instId, x86::ebx)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpd));
      if (canRun(instId, x86::rbx)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpq));
    }

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

    if (canRun(instId, x86::ebx, x86::edi, imm(1))) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpImm8));
    if (canRun(instId, x86::rbx, x86::rdi, imm(1))) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpImm8));

    if (canRun(instId, x86::bl, x86::dl, x86::al)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpb, InstSpec::kOpGpb, InstSpec::kOpGpb));
    if (canRun(instId, x86::bx, x86::di, x86::si)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpw, InstSpec::kOpGpw, InstSpec::kOpGpw));
    if (canRun(instId, x86::ebx, x86::edi, x86::esi)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpGpd, InstSpec::kOpGpd));
    if (canRun(instId, x86::rbx, x86::rdi, x86::rsi)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpGpq, InstSpec::kOpGpq));
  }

  if (canRun(instId, x86::mm1, imm(1))) dst.append(allocator, InstSpec::pack(InstSpec::kOpMm, InstSpec::kOpImm8));
  if (canRun(instId, x86::mm1, x86::mm2)) dst.append(allocator, InstSpec::pack(InstSpec::kOpMm, InstSpec::kOpMm));
  if (canRun(instId, x86::mm1, x86::mm2, imm(1))) dst.append(allocator, InstSpec::pack(InstSpec::kOpMm, InstSpec::kOpMm, InstSpec::kOpImm8));
  if (canRun(instId, x86::mm5, x86::edi)) dst.append(allocator, InstSpec::pack(InstSpec::kOpMm, InstSpec::kOpGpd));
  if (canRun(instId, x86::edi, x86::mm5)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpMm));
  if (canRun(instId, x86::mm5, x86::xmm5)) dst.append(allocator, InstSpec::pack(InstSpec::kOpMm, InstSpec::kOpXmm));
  if (canRun(instId, x86::xmm5, x86::mm5)) dst.append(allocator, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpMm));

  if (canRun(instId, x86::xmm3, imm(1))) dst.append(allocator, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpImm8));
  if (canRun(instId, x86::xmm3, x86::xmm5)) dst.append(allocator, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpXmm));
  if (canRun(instId, x86::xmm3, x86::ymm5)) dst.append(allocator, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpYmm));
  if (canRun(instId, x86::ymm3, x86::xmm5)) dst.append(allocator, InstSpec::pack(InstSpec::kOpYmm, InstSpec::kOpXmm));
  if (canRun(instId, x86::ymm3, x86::ymm5)) dst.append(allocator, InstSpec::pack(InstSpec::kOpYmm, InstSpec::kOpYmm));

  if (canRun(instId, x86::edi, x86::xmm5)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpXmm));
  if (canRun(instId, x86::rdi, x86::xmm5)) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpXmm));
  if (canRun(instId, x86::edi, x86::xmm5, imm(1))) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpd, InstSpec::kOpXmm, InstSpec::kOpImm8));
  if (canRun(instId, x86::rdi, x86::xmm5, imm(1))) dst.append(allocator, InstSpec::pack(InstSpec::kOpGpq, InstSpec::kOpXmm, InstSpec::kOpImm8));
  if (canRun(instId, x86::xmm3, x86::xmm5, x86::edi)) dst.append(allocator, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpXmm, InstSpec::kOpGpd));
  if (canRun(instId, x86::xmm3, x86::xmm5, x86::rdi)) dst.append(allocator, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpXmm, InstSpec::kOpGpq));

  if (canRun(instId, x86::xmm5, x86::edi)) dst.append(allocator, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpGpd));
  if (canRun(instId, x86::xmm5, x86::rdi)) dst.append(allocator, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpGpq));
  if (canRun(instId, x86::xmm5, x86::edi, imm(1))) dst.append(allocator, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpGpd, InstSpec::kOpImm8));
  if (canRun(instId, x86::xmm5, x86::rdi, imm(1))) dst.append(allocator, InstSpec::pack(InstSpec::kOpXmm, InstSpec::kOpGpq, InstSpec::kOpImm8));

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
    case x86::Inst::kIdRdrand:
    case x86::Inst::kIdRdseed:
      return 40;

    default:
      return 1000;
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

  uint32_t kOverheadPerIteration = 1; // Assume that each iteration cost an additional 1 cycle.
  uint32_t kOverheadBeforeLoop = 2;   // Assume that we waste 2 cycles before we enter the inner loop.
  uint32_t kOverheadPerSingleTest = nIter * kOverheadPerIteration - kOverheadBeforeLoop;

  // Consider a significant improvement 0.08 cycles per instruction (0.2 cycles in fast mode).
  uint32_t kSignificantImprovement = uint32_t(double(nIter) * (_app->_estimate ? 0.2 : 0.08));

  // If we called the function N times without a significant improvement we terminate the test.
  uint32_t kMaximumImprovementTries = _app->_estimate ? 1000 : 10000;

  uint32_t kMaxIterationCount = 1000000;

  uint64_t best;
  func(nIter, &best);

  uint64_t previousBest = best;
  uint32_t improvementTries = 0;

  for (uint32_t i = 0; i < kMaxIterationCount; i++) {
    uint64_t n;
    func(nIter, &n);

    // Decrement the estimated overhead caused by the inner loop.
    if (n > kOverheadPerSingleTest)
      n -= kOverheadPerSingleTest;
    else
      n = 0; // Should never happen.

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

      case InstSpec::kOpGpb  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[x86::Reg::kGroupGp ], x86::RegTraits<x86::Reg::kTypeGpbLo>::kSignature); break;
      case InstSpec::kOpGpw  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[x86::Reg::kGroupGp ], x86::RegTraits<x86::Reg::kTypeGpw>::kSignature); break;
      case InstSpec::kOpGpd  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[x86::Reg::kGroupGp ], x86::RegTraits<x86::Reg::kTypeGpd>::kSignature); break;
      case InstSpec::kOpGpq  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[x86::Reg::kGroupGp ], x86::RegTraits<x86::Reg::kTypeGpq>::kSignature); break;
      case InstSpec::kOpMm   : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[x86::Reg::kGroupMm ], x86::RegTraits<x86::Reg::kTypeMm >::kSignature); break;
      case InstSpec::kOpXmm  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[x86::Reg::kGroupVec], x86::RegTraits<x86::Reg::kTypeXmm>::kSignature); break;
      case InstSpec::kOpYmm  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[x86::Reg::kGroupVec], x86::RegTraits<x86::Reg::kTypeYmm>::kSignature); break;
      case InstSpec::kOpZmm  : fillRegArray(dst, _nUnroll, rStart, rInc, rMask[x86::Reg::kGroupVec], x86::RegTraits<x86::Reg::kTypeZmm>::kSignature); break;

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
      stackOperationSize = (_instSpec.get(0) == InstSpec::kOpGpw ? 2 : int(a.gpSize())) * int(_nUnroll);
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

  a.align(kAlignCode, 16);
  a.bind(L_Body);

  if (instId == x86::Inst::kIdPop)
    a.sub(a.zsp(), stackOperationSize);

  switch (instId) {
    case x86::Inst::kIdCall: {
      assert(opCount == 1);
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
            a.emit(instId, o0[n], x86::ptr(o1[n].as<x86::Gp>(), o2[n].as<Imm>().i32()));
        }
      }

      if (opCount == 4) {
        for (uint32_t n = 0; n < _nUnroll; n++) {
          a.emit(instId, o0[n], x86::ptr(o1[n].as<x86::Gp>(), o2[n].as<x86::Gp>(), 0, o3[n].as<Imm>().i32()));
        }
      }

      break;
    }

    // Instructions that don't require special care.
    default: {
      assert(opCount <= 6);
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

  if (instId == x86::Inst::kIdPush)
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
