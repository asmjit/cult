#ifndef _CULT_JSONBUILDER_H
#define _CULT_JSONBUILDER_H

#include "./globals.h"

namespace cult {

class JSONBuilder {
public:
  enum Token : uint32_t {
    kTokenNone  = 0,
    kTokenValue = 1
  };

  JSONBuilder(String* dst) noexcept;

  JSONBuilder& openArray() noexcept;
  JSONBuilder& closeArray(bool nl = false) noexcept;

  JSONBuilder& openObject() noexcept;
  JSONBuilder& closeObject(bool nl = false) noexcept;

  JSONBuilder& addKey(const char* str) noexcept;

  JSONBuilder& addBool(bool b) noexcept;
  JSONBuilder& addInt(int64_t n) noexcept;
  JSONBuilder& addUInt(uint64_t n) noexcept;
  JSONBuilder& addDouble(double d) noexcept;
  JSONBuilder& addDoublef(const char* fmt, double d) noexcept;
  JSONBuilder& addString(const char* str) noexcept;
  JSONBuilder& addStringf(const char* fmt, ...) noexcept;

  JSONBuilder& alignTo(size_t n) noexcept;
  JSONBuilder& beforeRecord() noexcept;

  JSONBuilder& nl() { _dst->append('\n'); return *this; }
  JSONBuilder& indent() { _dst->appendChars(' ', _level); return *this; }

  String* _dst;
  uint32_t _last;
  uint32_t _level;
};

} // cult namespace

#endif // _CULT_JSONBUILDER_H
