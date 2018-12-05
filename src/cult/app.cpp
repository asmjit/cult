#include "./app.h"
#include "./cpudetect.h"
#include "./instbench.h"
#include "./schedutils.h"

namespace cult {

App::App(int argc, char* argv[]) noexcept
  : _cmd(argc, argv),
    _zone(1024 * 32),
    _allocator(&_zone),
    _help(false),
    _dump(false),
    _round(true),
    _verbose(true),
    _estimate(false),
    _json(&_output) {

  SchedUtils::setAffinity(0);

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

    printf("\n");
    printf("Parameters:\n");
    printf("  --help        - Show help\n");
    printf("  --dump        - Dump generated asm to stdout\n");
    printf("  --quiet       - Quiet mode, no output except final JSON\n");
    printf("  --estimate    - Estimate only (faster, but less precise)\n");
    printf("  --no-rounding - Don't round cycles and latencies\n");
    printf("  --output=file - Output to file instead of stdout\n");
    printf("\n");
  }
}

App::~App() noexcept {
}

int App::run() noexcept {
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

} // cult namespace

int main(int argc, char* argv[]) {
  cult::App app(argc, argv);

  if (app.help())
    return 0;

  return app.run();
}
