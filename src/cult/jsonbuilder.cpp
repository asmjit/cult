#include "./jsonbuilder.h"

namespace cult {

JSONBuilder::JSONBuilder(StringBuilder* dst) noexcept
  : _dst(dst),
    _last(kTokenNone),
    _level(0) {}

JSONBuilder& JSONBuilder::openArray() noexcept {
  if (_last == kTokenValue)
    _dst->appendChar(',');

  _dst->appendChar('[');
  _last = kTokenNone;
  _level++;

  return *this;
}

JSONBuilder& JSONBuilder::closeArray() noexcept {
  _dst->appendChar(']');
  _last = kTokenValue;
  _level--;

  return *this;
}

JSONBuilder& JSONBuilder::openObject() noexcept {
  if (_last == kTokenValue)
    _dst->appendChar(',');

  _dst->appendChar('{');
  _last = kTokenNone;
  _level++;

  return *this;
}

JSONBuilder& JSONBuilder::closeObject() noexcept {
  _dst->appendChar('}');
  _last = kTokenValue;
  _level--;

  return *this;
}

JSONBuilder& JSONBuilder::addKey(const char* str) noexcept {
  addString(str);

  _dst->appendChar(':');
  _last = kTokenNone;

  return *this;
}

JSONBuilder& JSONBuilder::addBool(bool b) noexcept {
  if (_last == kTokenValue)
    _dst->appendChar(',');

  _dst->appendString(b ? "true" : "false");
  _last = kTokenValue;

  return *this;
}

JSONBuilder& JSONBuilder::addInt(int64_t n) noexcept {
  if (_last == kTokenValue)
    _dst->appendChar(',');

  _dst->appendInt(n);
  _last = kTokenValue;

  return *this;
}

JSONBuilder& JSONBuilder::addUInt(uint64_t n) noexcept {
  if (_last == kTokenValue)
    _dst->appendChar(',');

  _dst->appendUInt(n);
  _last = kTokenValue;

  return *this;
}

JSONBuilder& JSONBuilder::addDouble(double d) noexcept {
  if (_last == kTokenValue)
    _dst->appendChar(',');

  _dst->appendFormat("%g", d);
  _last = kTokenValue;

  return *this;
}

JSONBuilder& JSONBuilder::addDoublef(const char* fmt, double d) noexcept {
  if (_last == kTokenValue)
    _dst->appendChar(',');

  _dst->appendFormat(fmt, d);
  _last = kTokenValue;

  return *this;
}

JSONBuilder& JSONBuilder::addString(const char* str) noexcept {
  if (_last == kTokenValue)
    _dst->appendChar(',');

  _dst->appendFormat("\"%s\"", str);
  _last = kTokenValue;

  return *this;
}

JSONBuilder& JSONBuilder::addStringf(const char* fmt, ...) noexcept {
  va_list ap;
  va_start(ap, fmt);

  _dst->appendChar('\"');
  _dst->appendFormatVA(fmt, ap);
  _dst->appendChar('\"');
  _last = kTokenValue;

  va_end(ap);

  return *this;
}

JSONBuilder& JSONBuilder::alignTo(size_t n) noexcept {
  size_t i = _dst->getLength();
  const char* p = _dst->getData();

  while (i)
    if (p[--i] == '\n')
      break;

  size_t cur = _dst->getLength() - i;
  if (cur < n)
    _dst->appendChars(' ', n - cur);

  return *this;
}

JSONBuilder& JSONBuilder::beforeRecord() noexcept {
  if (_last == kTokenValue)
    _dst->appendChar(',');

  _dst->appendChar('\n');
  _dst->appendChars(' ', _level * 2);
  _last = kTokenNone;

  return *this;
}

} // cult namespace
