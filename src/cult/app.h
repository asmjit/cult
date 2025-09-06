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

  bool has_key(const char* key) const {
    for (int i = 0; i < argc; i++)
      if (strcmp(argv[i], key) == 0)
        return true;
    return false;
  }

  const char* value_of(const char* key) const {
    size_t key_size = strlen(key);
    size_t arg_size = 0;

    const char* arg = NULL;
    for (int i = 0; i <= argc; i++) {
      if (i == argc)
        return NULL;

      arg = argv[i];
      arg_size = strlen(arg);
      if (arg_size >= key_size && ::memcmp(arg, key, key_size) == 0)
        break;
    }

    if (arg_size > key_size && arg[key_size] == '=')
      return arg + key_size + 1;
    else
      return arg + key_size;
  }

  int argc;
  const char* const* argv;
};

class App {
public:
  App(int argc, char* argv[]);
  ~App();

  inline const CmdLine& cmd_line() const { return _cmd; }
  inline bool help() const { return _help; }
  inline bool verbose() const { return _verbose; }
  inline bool dump() const { return _dump; }
  inline JSONBuilder& json() { return _json; }

  void parse_arguments();
  int run();

  CmdLine _cmd;
  bool _help = false;
  bool _dump = false;
  bool _round = true;
  bool _verbose = true;
  bool _estimate = false;
  uint32_t _single_inst_id = 0;

  String _output;
  JSONBuilder _json;
};

} // {cult} namespace

#endif // _CULT_APP_H
