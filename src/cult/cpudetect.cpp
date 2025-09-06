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
  _model_id = 0;
  _family_id = 0;
  _stepping_id = 0;

  ::memset(_vendor_string, 0, sizeof(_vendor_string));
  ::memset(_vendor_name  , 0, sizeof(_vendor_name  ));
  ::memset(_brand_string , 0, sizeof(_brand_string ));
  ::memset(_uarch_name   , 0, sizeof(_uarch_name   ));
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

  CpuUtils::CpuidIn in = { 0 };
  CpuUtils::CpuidOut out = { 0 };

  uint32_t maxEax = 0;
  cpuid_query(&out, in.eax);

  for (;;) {
    uint32_t maxEcx = 0;

    switch (in.eax) {
      case 0x00u: {
        maxEax = out.eax;
        ::memcpy(_vendor_string + 0, &out.ebx, 4);
        ::memcpy(_vendor_string + 4, &out.edx, 4);
        ::memcpy(_vendor_string + 8, &out.ecx, 4);

        const char* vendorName = "Unknown";
        if (::memcmp(_vendor_string, "GenuineIntel"   , 12) == 0) vendorName = "Intel";
        if (::memcmp(_vendor_string, "AuthenticAMD"   , 12) == 0) vendorName = "AMD";
        if (::memcmp(_vendor_string, "VIA\0VIA\0VIA\0", 12) == 0) vendorName = "VIA";
        strcpy(_vendor_name, vendorName);
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

        _model_id  = modelId;
        _family_id = familyId;
        _stepping_id = out.eax & 0x0F;
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
          if (out.is_valid())
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
  uint32_t* brandString = reinterpret_cast<uint32_t*>(_brand_string);

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

  fix_brand_string(_brand_string);
}

void CpuDetect::_queryCpuInfo() {
  // CPU microarchitecture name.
  const char* uarch = "Unknown";

  if (strcmp(_vendor_string, "GenuineIntel") == 0) {
    uarch = "Unknown";

    if (_family_id <= 0x04) {
      uarch = "I486";
    }

    if (_family_id == 0x05) {
      uarch = "Pentium";
      switch (_model_id) {
        case 0x04:
        case 0x08: uarch = "Pentium MMX"; break;
        case 0x09: uarch = "Quark"; break;
      }
    }

    if (_family_id == 0x06) {
      switch (_model_id) {
        case 0x01: uarch = "Pentium Pro"; break;
        case 0x03: uarch = "Pentium 2"; break;
        case 0x05: uarch = "Pentium 2"; break;
        case 0x06: uarch = "Pentium 2"; break;
        case 0x07: uarch = "Pentium 3"; break;
        case 0x08: uarch = "Pentium 3"; break;
        case 0x09: uarch = "Pentium M"; break;
        case 0x0A: uarch = "Pentium 3"; break;
        case 0x0B: uarch = "Pentium 3"; break;
        case 0x0D: uarch = "Pentium M"; break;
        case 0x0E: uarch = "Pentium M"; break;
        case 0x0F: uarch = "Core"; break;
        case 0x15: uarch = "Pentium M"; break;
        case 0x16: uarch = "Core"; break;
        case 0x17: uarch = "Penryn"; break;
        case 0x1A: uarch = "Nehalem"; break;
        case 0x1C: uarch = "Bonnell"; break;
        case 0x1D: uarch = "Penryn"; break;
        case 0x1E: uarch = "Nehalem"; break;
        case 0x25: uarch = "Westmere"; break;
        case 0x26: uarch = "Bonnell"; break;
        case 0x27: uarch = "Saltwell"; break;
        case 0x2A: uarch = "Sandy Bridge"; break;
        case 0x2C: uarch = "Westmere"; break;
        case 0x2D: uarch = "Sandy Bridge"; break;
        case 0x2E: uarch = "Nehalem"; break;
        case 0x2F: uarch = "Westmere"; break;
        case 0x35: uarch = "Saltwell"; break;
        case 0x36: uarch = "Saltwell"; break;
        case 0x37: uarch = "Silvermont"; break;
        case 0x3A: uarch = "Ivy Bridge"; break;
        case 0x3C: uarch = "Haswell"; break;
        case 0x3D: uarch = "Broadwell"; break;
        case 0x3E: uarch = "Ivy Bridge"; break;
        case 0x3F: uarch = "Haswell"; break;
        case 0x45: uarch = "Haswell"; break;
        case 0x46: uarch = "Haswell"; break;
        case 0x4A: uarch = "Silvermont"; break;
        case 0x4C: uarch = "Airmont"; break;
        case 0x4D: uarch = "Silvermont"; break;
        case 0x4E: uarch = "Skylake"; break;
        case 0x4F: uarch = "Broadwell"; break;
        case 0x55: uarch = "Cascade Lake"; break;
        case 0x56: uarch = "Broadwell"; break;
        case 0x5A: uarch = "Silvermont"; break;
        case 0x5C: uarch = "Goldmont"; break;
        case 0x5D: uarch = "Silvermont"; break;
        case 0x5E: uarch = "Skylake"; break;
        case 0x5F: uarch = "Goldmont"; break;
        case 0x66: uarch = "Cannon Lake"; break;
        case 0x6A: uarch = "Ice Lake"; break;
        case 0x7A: uarch = "Goldmont+"; break;
        case 0x7D: uarch = "Ice Lake"; break;
        case 0x7E: uarch = "Ice Lake"; break;
        case 0x8A: uarch = "Tremont"; break;
        case 0x8C: uarch = "Tiger Lake"; break;
        case 0x8D: uarch = "Tiger Lake"; break;
        case 0x8E: uarch = "Kaby Lake"; break;
        case 0x8F: uarch = "Sapphire Rapids"; break;
        case 0x97: uarch = "Alder Lake"; break;
        case 0x9A: uarch = "Alder Lake"; break;
        case 0x9E: uarch = "Kaby Lake"; break;
        case 0xA5: uarch = "Comet Lake"; break;
        case 0xA7: uarch = "Rocket Lake"; break;
        case 0xB7: uarch = "Raptor Lake"; break;
        case 0x96: uarch = "Tremont"; break;
        case 0x9C: uarch = "Tremont"; break;
      }
    }

    if (_family_id == 0x0B) {
      switch (_model_id) {
        case 0x01: uarch = "Knights Corner"; break;
        case 0x57: uarch = "Knights Landing"; break;
      }
    }

    if (_family_id == 0x0F) {
      switch (_model_id) {
        case 0x00: uarch = "Pentium 4"; break;
        case 0x01: uarch = "Pentium 4"; break;
        case 0x02: uarch = "Pentium 4"; break;
        case 0x03: uarch = "Prescott"; break;
        case 0x04: uarch = "Prescott"; break;
        case 0x06: uarch = "Prescott"; break;
        case 0x0D: uarch = "Pentium M"; break;
      }
    }
  }

  if (strcmp(_vendor_string, "AuthenticAMD") == 0) {
    uarch = "Unknown";

    if (_family_id <= 0x04) {
      uarch = "AM486";
      if (_family_id == 0x04 && _model_id >= 0x0E) {
        uarch = "AM586";
      }
    }

    if (_family_id == 0x05) {
      uarch = "K5";
      if (_model_id >= 0x06) uarch = "K6";
      if (_model_id >= 0x08) uarch = "K6-2";
      if (_model_id >= 0x09) uarch = "K6-3";
    }

    if (_family_id == 0x06) uarch = "K7";
    if (_family_id == 0x08) uarch = "K8";
    if (_family_id == 0x0F) uarch = "K8";
    if (_family_id == 0x10) uarch = "K10";
    if (_family_id == 0x11) uarch = "K8";
    if (_family_id == 0x12) uarch = "K10";
    if (_family_id == 0x14) uarch = "Bobcat";

    if (_family_id == 0x15) {
      uarch = "Bulldozer";
      if (_model_id >= 0x02) uarch = "Piledriver";
      if (_model_id >= 0x30) uarch = "Steamroller";
      if (_model_id >= 0x60) uarch = "Excavator";
    }

    if (_family_id == 0x16) {
      uarch = "Jaguar";
      if (_model_id == 0x30) uarch = "Puma";
    }

    if (_family_id == 0x17) {
      uarch = "Zen";

      if (_model_id == 0x90) uarch = "Zen 2";
      if (_model_id == 0x71) uarch = "Zen 2";
      if (_model_id == 0x68) uarch = "Zen 2";
      if (_model_id == 0x60) uarch = "Zen 2";
      if (_model_id == 0x47) uarch = "Zen 2";
      if (_model_id == 0x31) uarch = "Zen 2";
    }

    if (_family_id == 0x18) {
      uarch = "Zen / Dhyana";
    }

    if (_family_id == 0x19) {
      uarch = "Zen 3";

      if (_model_id == 0x61) uarch = "Zen 4";
    }

    if (_family_id == 0x1A) {
      uarch = "Zen 5";
    }
  }
  strncpy(_uarch_name, uarch, sizeof(_uarch_name) - 1);

  if (_app->verbose()) {
    printf("CpuDetect:\n");
    printf("  VendorName: %s\n", _vendor_name);
    printf("  VendorString: %s\n", _vendor_string);
    printf("  BrandString: %s\n", _brand_string);
    printf("  uArch: %s\n", _uarch_name);
    printf("  ModelId: 0x%02X\n", _model_id);
    printf("  FamilyId: 0x%04X\n", _family_id);
    printf("  SteppingId: 0x%02X\n", _stepping_id);
    printf("\n");
  }

  JSONBuilder& json = _app->json();
  json.before_record()
      .add_key("cpuInfo")
      .open_object()
        .before_record().add_key("vendorName").add_string(_vendor_name)
        .before_record().add_key("vendorString").add_string(_vendor_string)
        .before_record().add_key("brandString").add_string(_brand_string)
        .before_record().add_key("uarch").add_string(_uarch_name)
        .before_record().add_key("modelId").add_stringf("0x%02X", _model_id)
        .before_record().add_key("familyId").add_stringf("0x%0002X", _family_id)
        .before_record().add_key("steppingId").add_stringf("0x%02X", _stepping_id)
      .close_object(true);
}

void CpuDetect::addEntry(const CpuUtils::CpuidIn& in, const CpuUtils::CpuidOut& out) {
  if (!out.is_valid())
    return;

  if (_app->verbose())
    printf("  In:%08X Sub:%08X -> EAX:%08X EBX:%08X ECX:%08X EDX:%08X\n", in.eax, in.ecx, out.eax, out.ebx, out.ecx, out.edx);

  _entries.push_back(CpuUtils::CpuidEntry{in, out});
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
