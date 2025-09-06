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

void App::parse_arguments() {
  if (_cmd.has_key("--help")) _help = true;
  if (_cmd.has_key("--dump")) _dump = true;
  if (_cmd.has_key("--quiet")) _verbose = false;
  if (_cmd.has_key("--estimate")) _estimate = true;
  if (_cmd.has_key("--no-rounding")) _round = false;

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

  const char* instruction = _cmd.value_of("--instruction");
  if (instruction) {
    _single_inst_id = asmjit::InstAPI::string_to_inst_id(Arch::kHost, instruction, strlen(instruction));
    if (_single_inst_id == 0) {
      printf("The required instruction '%s' was not found in the database\n", instruction);
      exit(1);
    }
  }
}

int App::run() {
  SchedUtils::set_affinity(0);

  _json.open_object();
  _json.before_record()
       .add_key("cult")
       .open_object()
         .before_record()
         .add_key("version").add_stringf("%d.%d.%d", CULT_VERSION_MAJOR, CULT_VERSION_MINOR, CULT_VERSION_MICRO)
       .close_object(true);

  {
    CpuDetect cpu_detect(this);
    cpu_detect.run();
  }

  {
    InstBench inst_bench(this);
    inst_bench.run();
  }

  _json.nl().close_object().nl();

  const char* output_file_name = _cmd.value_of("--output");
  if (output_file_name) {
    FILE* file = fopen(output_file_name, "wb");
    if (!file) {
      printf("Couldn't open output file: %s\n", output_file_name);
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
  app.parse_arguments();
  return app.run();
}
