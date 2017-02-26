#ifndef _CULT_JSONBUILDER_H
#define _CULT_JSONBUILDER_H

#include "./globals.h"

namespace cult {

class JSONBuilder {
public:
  enum Token {
    kTokenNone  = 0,
    kTokenValue = 1
  };

  JSONBuilder(StringBuilder* dst) noexcept;

  JSONBuilder& openArray() noexcept;
  JSONBuilder& closeArray() noexcept;

  JSONBuilder& openObject() noexcept;
  JSONBuilder& closeObject() noexcept;

  JSONBuilder& addKey(const char* str) noexcept;

  JSONBuilder& addBool(bool b) noexcept;
  JSONBuilder& addInt(int64_t n) noexcept;
  JSONBuilder& addUInt(uint64_t n) noexcept;
  JSONBuilder& addUIntHex(uint64_t n, uint32_t width = 0) noexcept;
  JSONBuilder& addDouble(double d) noexcept;
  JSONBuilder& addDoublef(const char* fmt, double d) noexcept;
  JSONBuilder& addString(const char* str) noexcept;
  JSONBuilder& addStringf(const char* fmt, ...) noexcept;

  JSONBuilder& alignTo(size_t n) noexcept;
  JSONBuilder& beforeRecord() noexcept;

  JSONBuilder& nl() { _dst->appendChar('\n'); return *this; }
  JSONBuilder& indent() { _dst->appendChars(' ', _level); return *this; }

  StringBuilder* _dst;
  uint32_t _last;
  uint32_t _level;
};

} // cult namespace

#endif // _CULT_JSONBUILDER_H
