#ifndef _CULT_APP_H
#define _CULT_APP_H

#include "./globals.h"
#include "./jsonbuilder.h"

#include <stdlib.h>
#include <string.h>

namespace cult {

class CmdLine {
public:
  CmdLine(int argc, const char* const* argv)
    : argc(argc),
      argv(argv) {}

  bool hasKey(const char* key) const {
    for (int i = 0; i < argc; i++)
      if (::strcmp(argv[i], key) == 0)
        return true;
    return false;
  }

  const char* getKey(const char* key) const {
    size_t keyLen = ::strlen(key);
    size_t argLen = 0;

    const char* arg = NULL;
    for (int i = 0; i <= argc; i++) {
      if (i == argc)
        return NULL;

      arg = argv[i];
      argLen = ::strlen(arg);
      if (argLen >= keyLen && ::memcmp(arg, key, keyLen) == 0)
        break;
    }

    if (argLen > keyLen && arg[keyLen] == '=')
      return arg + keyLen + 1;
    else
      return arg + keyLen;
  }

  int argc;
  const char* const* argv;
};

class App {
public:
  App(int argc, char* argv[]) noexcept;
  ~App() noexcept;

  inline bool verbose() const noexcept { return _verbose; }
  inline bool dump() const noexcept { return _dump; }
  inline JSONBuilder& json() noexcept { return _json; }

  int run() noexcept;

  CmdLine _cmd;
  Zone _zone;
  ZoneHeap _heap;

  bool _verbose;
  bool _round;
  bool _dump;

  StringBuilder _output;
  JSONBuilder _json;
};

} // cult namespace

#endif // _CULT_APP_H
