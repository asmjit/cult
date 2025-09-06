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

  JSONBuilder& open_array();
  JSONBuilder& close_array(bool nl = false);

  JSONBuilder& open_object();
  JSONBuilder& close_object(bool nl = false);

  JSONBuilder& add_key(const char* str);

  JSONBuilder& add_bool(bool b);
  JSONBuilder& add_int(int64_t n);
  JSONBuilder& add_uint(uint64_t n);
  JSONBuilder& add_double(double d);
  JSONBuilder& add_doublef(const char* fmt, double d);
  JSONBuilder& add_string(const char* str);
  JSONBuilder& add_stringf(const char* fmt, ...);

  JSONBuilder& align_to(size_t n);
  JSONBuilder& before_record();

  JSONBuilder& nl() { _dst->append('\n'); return *this; }
  JSONBuilder& indent() { _dst->append_chars(' ', _level); return *this; }

  String* _dst;
  uint32_t _last;
  uint32_t _level;
};

} // {cult} namespace

#endif // _CULT_JSONBUILDER_H
