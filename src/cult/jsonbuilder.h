#ifndef _CULT_JSONBUILDER_H
#define _CULT_JSONBUILDER_H

#include "globals.h"

namespace cult {

class JSONBuilder {
public:
  enum Token : uint32_t {
    kTokenNone  = 0,
    kTokenValue = 1
  };

  JSONBuilder(String* dst);

  JSONBuilder& openArray();
  JSONBuilder& closeArray(bool nl = false);

  JSONBuilder& openObject();
  JSONBuilder& closeObject(bool nl = false);

  JSONBuilder& addKey(const char* str);

  JSONBuilder& addBool(bool b);
  JSONBuilder& addInt(int64_t n);
  JSONBuilder& addUInt(uint64_t n);
  JSONBuilder& addDouble(double d);
  JSONBuilder& addDoublef(const char* fmt, double d);
  JSONBuilder& addString(const char* str);
  JSONBuilder& addStringf(const char* fmt, ...);

  JSONBuilder& alignTo(size_t n);
  JSONBuilder& beforeRecord();

  JSONBuilder& nl() { _dst->append('\n'); return *this; }
  JSONBuilder& indent() { _dst->appendChars(' ', _level); return *this; }

  String* _dst;
  uint32_t _last;
  uint32_t _level;
};

} // cult namespace

#endif // _CULT_JSONBUILDER_H
