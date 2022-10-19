#include "cpudetect.h"

namespace cult {

inline void fix_brand_string(char* str) {
  size_t len = strlen(str);
  while (len && str[len - 1] == ' ') {
    str[len - 1] = '\0';
    len--;
  }
}

CpuDetect::CpuDetect(App* app) : _app(app) {
  _modelId = 0;
  _familyId = 0;
  _steppingId = 0;

  ::memset(_vendorString, 0, sizeof(_vendorString));
  ::memset(_vendorName  , 0, sizeof(_vendorName  ));
  ::memset(_brandString , 0, sizeof(_brandString ));
  ::memset(_uarchName   , 0, sizeof(_uarchName   ));
}
CpuDetect::~CpuDetect() {}

void CpuDetect::run() {
  _queryCpuData();
  _queryCpuInfo();
}

void CpuDetect::_queryCpuData() {
  JSONBuilder& json = _app->json();

  if (_app->verbose())
    printf("CpuData (CPUID):\n");

  json.beforeRecord()
      .addKey("cpuData")
      .openArray();

  CpuUtils::CpuidIn in = { 0 };
  CpuUtils::CpuidOut out = { 0 };

  uint32_t maxEax = 0;
  cpuid_query(&out, in.eax);

  for (;;) {
    uint32_t maxEcx = 0;

    switch (in.eax) {
      case 0x00u: {
        maxEax = out.eax;
        ::memcpy(_vendorString + 0, &out.ebx, 4);
        ::memcpy(_vendorString + 4, &out.edx, 4);
        ::memcpy(_vendorString + 8, &out.ecx, 4);

        const char* vendorName = "Unknown";
        if (::memcmp(_vendorString, "GenuineIntel"   , 12) == 0) vendorName = "Intel";
        if (::memcmp(_vendorString, "AuthenticAMD"   , 12) == 0) vendorName = "AMD";
        if (::memcmp(_vendorString, "VIA\0VIA\0VIA\0", 12) == 0) vendorName = "VIA";
        strcpy(_vendorName, vendorName);
        break;
      }

      case 0x01u: {
        // Fill family and model fields.
        uint32_t modelId  = (out.eax >> 4) & 0x0F;
        uint32_t familyId = (out.eax >> 8) & 0x0F;

        // Use extended family and model fields.
        if (familyId == 0x06u || familyId == 0x0Fu)
          modelId += (((out.eax >> 16) & 0x0Fu) << 4);

        if (familyId == 0x0Fu)
          familyId += (out.eax >> 20) & 0xFFu;

        _modelId  = modelId;
        _familyId = familyId;
        _steppingId = out.eax & 0x0F;
        break;
      }

      case 0x04u:
        // Deterministic Cache Parameters.
        maxEcx = 63;
        break;

      case 0x07u:
        // Structured Extended Feature Flags Enumeration.
        maxEcx = out.eax;
        break;

      case 0x0Bu:
        // Extended Topology Enumeration. Scan all possibilities.
        maxEcx = 255;
        for (;;) {
          // Bits 7-0 in `out.ecx` are always the same as bits 7-0 in `in.ecx`.
          // If there are no other bits set we consider that output invalid.
          if (out.eax || out.ebx || (out.ecx & 0xFFFFFF00u) || in.eax == 0)
            addEntry(in, out);

          if (in.ecx == maxEcx)
            break;
          cpuid_query(&out, in.eax, ++in.ecx);
        }

        maxEcx = 0xFFFFFFFFu;
        break;

      case 0x0Du:
        // Processor Extended State Enumeration. We set ECX to a possible
        // maximum value, and discard results from invalid calls.
        maxEcx = 63;
        break;

      case 0x0Fu:
        // Currently it seems only defined one is for ECX 0 or 1, but the
        // manual says to check bits in EDX, so we set `maxEcx` to maximum
        // possible value in the future.
        maxEcx = 31;
        break;

      case 0x10u:
        // L3 Cache QoS Enforcement Enumeration.
        maxEcx = 63;
        break;

      case 0x14u:
        // Intel Processor Trace Enumeration
        maxEcx = out.eax;
        break;
    }

    if (maxEcx != 0xFFFFFFFFu) {
      if (maxEcx > 0) {
        for (;;) {
          // Sometimes we just use max possible value and iterate all, if CPU
          // reports invalid call it's fine, we just continue until we reach
          // the limit.
          if (out.isValid())
            addEntry(in, out);

          if (in.ecx == maxEcx)
            break;
          cpuid_query(&out, in.eax, ++in.ecx);
        }
      }
      else {
        addEntry(in, out);
      }
    }

    in.ecx = 0;
    if (in.eax == maxEax)
      break;

    // Don't ask for CPU serial number.
    if (++in.eax == 3)
      ::memset(&out, 0, sizeof(out));
    else
      cpuid_query(&out, in.eax, 0);
  }

  in.eax = 0x80000000u;
  in.ecx = 0;

  maxEax = in.eax;
  cpuid_query(&out, in.eax);
  uint32_t* brandString = reinterpret_cast<uint32_t*>(_brandString);

  for (;;) {
    switch (in.eax) {
      case 0x80000000u: {
        if (out.eax > 0x80000000u)
          maxEax = out.eax;
        break;
      }

      case 0x80000002u:
      case 0x80000003u:
      case 0x80000004u:
        *brandString++ = out.eax;
        *brandString++ = out.ebx;
        *brandString++ = out.ecx;
        *brandString++ = out.edx;
        break;
    }

    addEntry(in, out);
    if (in.eax == maxEax)
      break;
    cpuid_query(&out, ++in.eax, 0);
  }

  // Mystery level 0x8FFFFFFF.
  in.eax = 0x8FFFFFFFu;
  cpuid_query(&out, in.eax, 0);
  addEntry(in, out);

  if (_app->verbose())
    printf("\n");
  json.closeArray(true);

  fix_brand_string(_brandString);
}

void CpuDetect::_queryCpuInfo() {
  // CPU microarchitecture name.
  const char* uarch = "Unknown";

  if (strcmp(_vendorString, "GenuineIntel") == 0) {
    uarch = "Unknown";

    if (_familyId <= 0x04) {
      uarch = "I486";
    }

    if (_familyId == 0x05) {
      uarch = "Pentium";
      switch (_modelId) {
        case 0x04:
        case 0x08: uarch = "Pentium MMX"   ; break;
        case 0x09: uarch = "Quark"         ; break;
      }
    }

    if (_familyId == 0x06) {
      switch (_modelId) {
        case 0x01: uarch = "Pentium Pro"   ; break;
        case 0x03: uarch = "Pentium 2"     ; break;
        case 0x05: uarch = "Pentium 2"     ; break;
        case 0x06: uarch = "Pentium 2"     ; break;
        case 0x07: uarch = "Pentium 3"     ; break;
        case 0x08: uarch = "Pentium 3"     ; break;
        case 0x09: uarch = "Pentium M"     ; break;
        case 0x0A: uarch = "Pentium 3"     ; break;
        case 0x0B: uarch = "Pentium 3"     ; break;
        case 0x0D: uarch = "Pentium M"     ; break;
        case 0x0E: uarch = "Yonah"         ; break;
        case 0x0F: uarch = "Merom"         ; break;
        case 0x15: uarch = "Pentium M"     ; break;
        case 0x16: uarch = "Merom"         ; break;
        case 0x17: uarch = "Penryn"        ; break;
        case 0x1A: uarch = "Nehalem"       ; break;
        case 0x1C: uarch = "Bonnell"       ; break;
        case 0x1D: uarch = "Penryn"        ; break;
        case 0x1E: uarch = "Nehalem"       ; break;
        case 0x25: uarch = "Westmere"      ; break;
        case 0x26: uarch = "Bonnell"       ; break;
        case 0x27: uarch = "Bonnell"       ; break;
        case 0x2A: uarch = "Sandy Bridge"  ; break;
        case 0x2C: uarch = "Westmere"      ; break;
        case 0x2D: uarch = "Sandy Bridge"  ; break;
        case 0x2E: uarch = "Nehalem"       ; break;
        case 0x2F: uarch = "Westmere"      ; break;
        case 0x35: uarch = "Bonnell"       ; break;
        case 0x36: uarch = "Bonnell"       ; break;
        case 0x37: uarch = "Solvermont"    ; break;
        case 0x3A: uarch = "Ivy Bridge"    ; break;
        case 0x3C: uarch = "Haswell"       ; break;
        case 0x3D: uarch = "Broadwell"     ; break;
        case 0x3E: uarch = "Ivy Bridge"    ; break;
        case 0x3F: uarch = "Haswell"       ; break;
        case 0x45: uarch = "Haswell"       ; break;
        case 0x46: uarch = "Haswell"       ; break;
        case 0x4A: uarch = "Solvermont"    ; break;
        case 0x4D: uarch = "Solvermont"    ; break;
        case 0x4E: uarch = "Skylake"       ; break;
        case 0x5E: uarch = "Skylake"       ; break;
        case 0x8E: uarch = "Kaby Lake"     ; break;
        case 0x9E: uarch = "Kaby Lake"     ; break;
      }
    }

    if (_familyId == 0x0B) {
      switch (_modelId) {
        case 0x01: uarch = "Knights Corner"; break;
        case 0x57: uarch = "Knights Landing"; break;
      }
    }

    if (_familyId == 0x0F) {
      switch (_modelId) {
        case 0x00: uarch = "Pentium 4"     ; break;
        case 0x01: uarch = "Pentium 4"     ; break;
        case 0x02: uarch = "Pentium 4"     ; break;
        case 0x03: uarch = "Prescott"      ; break;
        case 0x04: uarch = "Prescott"      ; break;
        case 0x06: uarch = "Prescott"      ; break;
        case 0x0D: uarch = "Dothan"        ; break;
      }
    }
  }

  if (strcmp(_vendorString, "AuthenticAMD") == 0) {
    uarch = "Unknown";

    if (_familyId <= 0x04) {
      uarch = "AM486";
      if (_familyId == 0x04 && _modelId >= 0x0E) {
        uarch = "AM586";
      }
    }

    if (_familyId == 0x05) {
      uarch = "K5";
      if (_modelId >= 0x06) uarch = "K6";
      if (_modelId >= 0x08) uarch = "K6-2";
      if (_modelId >= 0x09) uarch = "K6-3";
    }

    if (_familyId == 0x06) uarch = "K7";
    if (_familyId == 0x08) uarch = "K8";
    if (_familyId == 0x0F) uarch = "K8";
    if (_familyId == 0x10) uarch = "K10";
    if (_familyId == 0x11) uarch = "K8";
    if (_familyId == 0x12) uarch = "K10";
    if (_familyId == 0x14) uarch = "Bobcat";

    if (_familyId == 0x15) {
      uarch = "Bulldozer";
      if (_modelId >= 0x02) uarch = "Piledriver";
      if (_modelId >= 0x30) uarch = "Steamroller";
      if (_modelId >= 0x60) uarch = "Excavator";
    }

    if (_familyId == 0x16) {
      uarch = "Jaguar";
      if (_modelId == 0x30) uarch = "Puma";
    }

    if (_familyId == 0x17) {
      uarch = "Zen";

      if (_modelId == 0x90) uarch = "Zen 2";
      if (_modelId == 0x71) uarch = "Zen 2";
      if (_modelId == 0x68) uarch = "Zen 2";
      if (_modelId == 0x60) uarch = "Zen 2";
      if (_modelId == 0x47) uarch = "Zen 2";
      if (_modelId == 0x31) uarch = "Zen 2";
    }

    if (_familyId == 0x18) {
      uarch = "Zen / Dhyana";
    }

    if (_familyId == 0x19) {
      uarch = "Zen 3";

      if (_modelId == 0x61) uarch = "Zen 4";
    }

    if (_familyId == 0x1A) {
      uarch = "Zen 5";
    }
  }
  strncpy(_uarchName, uarch, sizeof(_uarchName) - 1);

  if (_app->verbose()) {
    printf("CpuDetect:\n");
    printf("  VendorName: %s\n", _vendorName);
    printf("  VendorString: %s\n", _vendorString);
    printf("  BrandString: %s\n", _brandString);
    printf("  uArch: %s\n", _uarchName);
    printf("  ModelId: 0x%02X\n", _modelId);
    printf("  FamilyId: 0x%04X\n", _familyId);
    printf("  SteppingId: 0x%02X\n", _steppingId);
    printf("\n");
  }

  JSONBuilder& json = _app->json();
  json.beforeRecord()
      .addKey("cpuInfo")
      .openObject()
        .beforeRecord().addKey("vendorName").addString(_vendorName)
        .beforeRecord().addKey("vendorString").addString(_vendorString)
        .beforeRecord().addKey("brandString").addString(_brandString)
        .beforeRecord().addKey("uarch").addString(_uarchName)
        .beforeRecord().addKey("modelId").addStringf("0x%02X", _modelId)
        .beforeRecord().addKey("familyId").addStringf("0x%0002X", _familyId)
        .beforeRecord().addKey("steppingId").addStringf("0x%02X", _steppingId)
      .closeObject(true);
}

void CpuDetect::addEntry(const CpuUtils::CpuidIn& in, const CpuUtils::CpuidOut& out) {
  if (!out.isValid())
    return;

  if (_app->verbose())
    printf("  In:%08X Sub:%08X -> EAX:%08X EBX:%08X ECX:%08X EDX:%08X\n", in.eax, in.ecx, out.eax, out.ebx, out.ecx, out.edx);

  _entries.push_back(CpuUtils::CpuidEntry{in, out});

  JSONBuilder& json = _app->json();
  json.beforeRecord()
      .openObject()
      .addKey("level").addStringf("0x%08X", in.eax)
      .addKey("subleaf").addStringf("0x%08X", in.ecx)
      .addKey("eax").addStringf("0x%08X", out.eax)
      .addKey("ebx").addStringf("0x%08X", out.ebx)
      .addKey("ecx").addStringf("0x%08X", out.ecx)
      .addKey("edx").addStringf("0x%08X", out.edx)
      .closeObject();
}

CpuUtils::CpuidOut CpuDetect::entryOf(uint32_t eax, uint32_t ecx) {
  CpuUtils::CpuidOut out {};
  for (const CpuUtils::CpuidEntry& entry : _entries) {
    if (entry.in.eax == eax && entry.in.ecx == ecx) {
      out = entry.out;
      break;
    }
  }
  return out;
}

} // {cult} namespace
