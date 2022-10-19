#include <stdlib.h>

#include "app.h"
#include "cpudetect.h"
#include "instbench.h"
#include "schedutils.h"

namespace cult {

App::App(int argc, char* argv[])
  : _cmd(argc, argv),
    _json(&_output) {}

App::~App() {}

void App::parseArguments() {
  if (_cmd.hasKey("--help")) _help = true;
  if (_cmd.hasKey("--dump")) _dump = true;
  if (_cmd.hasKey("--quiet")) _verbose = false;
  if (_cmd.hasKey("--estimate")) _estimate = true;
  if (_cmd.hasKey("--no-rounding")) _round = false;

  if (help() || verbose()) {
    printf("CULT v%u.%u.%u [Using AsmJit v%u.%u.%u]\n",
      CULT_VERSION_MAJOR,
      CULT_VERSION_MINOR,
      CULT_VERSION_MICRO,
      (ASMJIT_LIBRARY_VERSION >> 16),
      (ASMJIT_LIBRARY_VERSION >> 8) & 0xFFu,
      (ASMJIT_LIBRARY_VERSION >> 0) & 0xFFu);
    printf("  --help for command line options\n\n");
  }

  if (help()) {
    printf("Usage:\n");
    printf("  --help             - Show help information\n");
    printf("  --dump             - Dump generated asm to stdout\n");
    printf("  --quiet            - Quiet mode, no output except final JSON\n");
    printf("  --estimate         - Estimate only (faster, but less precise)\n");
    printf("  --no-rounding      - Don't round cycles and latencies\n");
    printf("  --instruction=name - Only benchmark a particular instruction\n");
    printf("  --output=file      - end output to file instead of stdout\n");
    printf("\n");
    exit(0);
  }

  const char* instruction = _cmd.valueOf("--instruction");
  if (instruction) {
    _singleInstId = asmjit::InstAPI::stringToInstId(Arch::kHost, instruction, strlen(instruction));
    if (_singleInstId == 0) {
      printf("The required instruction '%s' was not found in the database\n", instruction);
      exit(1);
    }
  }
}

int App::run() {
  SchedUtils::setAffinity(0);

  _json.openObject();
  _json.beforeRecord()
       .addKey("cult")
       .openObject()
         .beforeRecord()
         .addKey("version").addStringf("%d.%d.%d", CULT_VERSION_MAJOR, CULT_VERSION_MINOR, CULT_VERSION_MICRO)
       .closeObject(true);

  {
    CpuDetect cpuDetect(this);
    cpuDetect.run();
  }

  {
    InstBench instBench(this);
    instBench.run();
  }

  _json.nl()
       .closeObject()
       .nl();

  const char* outputFileName = _cmd.valueOf("--output");
  if (outputFileName) {
    FILE* file = fopen(outputFileName, "wb");
    if (!file) {
      printf("Couldn't open output file: %s\n", outputFileName);
    }
    else {
      fwrite(_output.data(), _output.size(), 1, file);
      fclose(file);
    }
  }
  else {
    puts(_output.data());
  }
  return 0;
}

} // {cult} namespace

int main(int argc, char* argv[]) {
  cult::App app(argc, argv);
  app.parseArguments();
  return app.run();
}
