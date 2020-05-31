#include "./jsonbuilder.h"

namespace cult {

JSONBuilder::JSONBuilder(String* dst) noexcept
  : _dst(dst),
    _last(kTokenNone),
    _level(0) {}

JSONBuilder& JSONBuilder::openArray() noexcept {
  if (_last == kTokenValue)
    _dst->append(',');

  _dst->append('[');
  _last = kTokenNone;
  _level++;

  return *this;
}

JSONBuilder& JSONBuilder::closeArray(bool nl) noexcept {
  _level--;
  if (nl) {
    _dst->append('\n');
    _dst->appendChars(' ', _level * 2);
  }

  _dst->append(']');
  _last = kTokenValue;

  return *this;
}

JSONBuilder& JSONBuilder::openObject() noexcept {
  if (_last == kTokenValue)
    _dst->append(',');

  _dst->append('{');
  _last = kTokenNone;
  _level++;

  return *this;
}

JSONBuilder& JSONBuilder::closeObject(bool nl) noexcept {
  _level--;
  if (nl) {
    _dst->append('\n');
    _dst->appendChars(' ', _level * 2);
  }

  _dst->append('}');
  _last = kTokenValue;
  return *this;
}

JSONBuilder& JSONBuilder::addKey(const char* str) noexcept {
  addString(str);

  _dst->append(':');
  _last = kTokenNone;

  return *this;
}

JSONBuilder& JSONBuilder::addBool(bool b) noexcept {
  if (_last == kTokenValue)
    _dst->append(',');

  _dst->append(b ? "true" : "false");
  _last = kTokenValue;

  return *this;
}

JSONBuilder& JSONBuilder::addInt(int64_t n) noexcept {
  if (_last == kTokenValue)
    _dst->append(',');

  _dst->appendInt(n);
  _last = kTokenValue;

  return *this;
}

JSONBuilder& JSONBuilder::addUInt(uint64_t n) noexcept {
  if (_last == kTokenValue)
    _dst->append(',');

  _dst->appendUInt(n);
  _last = kTokenValue;

  return *this;
}

JSONBuilder& JSONBuilder::addDouble(double d) noexcept {
  if (_last == kTokenValue)
    _dst->append(',');

  _dst->appendFormat("%g", d);
  _last = kTokenValue;

  return *this;
}

JSONBuilder& JSONBuilder::addDoublef(const char* fmt, double d) noexcept {
  if (_last == kTokenValue)
    _dst->append(',');

  _dst->appendFormat(fmt, d);
  _last = kTokenValue;

  return *this;
}

JSONBuilder& JSONBuilder::addString(const char* str) noexcept {
  if (_last == kTokenValue)
    _dst->append(',');

  _dst->appendFormat("\"%s\"", str);
  _last = kTokenValue;

  return *this;
}

JSONBuilder& JSONBuilder::addStringf(const char* fmt, ...) noexcept {
  va_list ap;
  va_start(ap, fmt);

  _dst->append('\"');
  _dst->appendVFormat(fmt, ap);
  _dst->append('\"');
  _last = kTokenValue;

  va_end(ap);

  return *this;
}

JSONBuilder& JSONBuilder::alignTo(size_t n) noexcept {
  size_t i = _dst->size();
  const char* p = _dst->data();

  while (i)
    if (p[--i] == '\n')
      break;

  size_t cur = _dst->size() - i;
  if (cur < n)
    _dst->appendChars(' ', n - cur);

  return *this;
}

JSONBuilder& JSONBuilder::beforeRecord() noexcept {
  if (_last == kTokenValue)
    _dst->append(',');

  _dst->append('\n');
  _dst->appendChars(' ', _level * 2);
  _last = kTokenNone;

  return *this;
}

} // cult namespace
