#ifndef _CULT_APP_H
#define _CULT_APP_H

#include "./globals.h"
#include "./jsonbuilder.h"

#include <stdlib.h>
#include <string.h>

namespace cult {

class CmdLine {
public:
  CmdLine(int argc, const char* const* argv) noexcept
    : argc(argc),
      argv(argv) {}

  bool hasKey(const char* key) const noexcept {
    for (int i = 0; i < argc; i++)
      if (strcmp(argv[i], key) == 0)
        return true;
    return false;
  }

  const char* valueOf(const char* key) const noexcept {
    size_t keySize = strlen(key);
    size_t argSize = 0;

    const char* arg = NULL;
    for (int i = 0; i <= argc; i++) {
      if (i == argc)
        return NULL;

      arg = argv[i];
      argSize = strlen(arg);
      if (argSize >= keySize && ::memcmp(arg, key, keySize) == 0)
        break;
    }

    if (argSize > keySize && arg[keySize] == '=')
      return arg + keySize + 1;
    else
      return arg + keySize;
  }

  int argc;
  const char* const* argv;
};

class App {
public:
  App(int argc, char* argv[]) noexcept;
  ~App() noexcept;

  inline const CmdLine& cmdLine() const noexcept { return _cmd; }
  inline ZoneAllocator* allocator() const noexcept { return const_cast<ZoneAllocator*>(&_allocator); }

  inline bool help() const noexcept { return _help; }
  inline bool verbose() const noexcept { return _verbose; }
  inline bool dump() const noexcept { return _dump; }
  inline JSONBuilder& json() noexcept { return _json; }

  int run() noexcept;

  CmdLine _cmd;
  Zone _zone;
  ZoneAllocator _allocator;

  bool _help;
  bool _dump;
  bool _round;
  bool _verbose;
  bool _estimate;

  String _output;
  JSONBuilder _json;
};

} // cult namespace

#endif // _CULT_APP_H
