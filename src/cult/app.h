#ifndef _CULT_APP_H
#define _CULT_APP_H

#include "globals.h"
#include "jsonbuilder.h"

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
      if (strcmp(argv[i], key) == 0)
        return true;
    return false;
  }

  const char* valueOf(const char* key) const {
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
  App(int argc, char* argv[]);
  ~App();

  inline const CmdLine& cmdLine() const { return _cmd; }
  inline bool help() const { return _help; }
  inline bool verbose() const { return _verbose; }
  inline bool dump() const { return _dump; }
  inline JSONBuilder& json() { return _json; }

  void parseArguments();
  int run();

  CmdLine _cmd;
  bool _help = false;
  bool _dump = false;
  bool _round = true;
  bool _verbose = true;
  bool _estimate = false;
  uint32_t _singleInstId = 0;

  String _output;
  JSONBuilder _json;
};

} // cult namespace

#endif // _CULT_APP_H
