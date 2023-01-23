CULT
----

CPU Ultimate Latency Test.

  * [Official Repository (asmjit/cult)](https://github.com/asmjit/cult)
  * [Official Blog (asmbits)](https://asmbits.blogspot.com/ncr)
  * [Official Chat (gitter)](https://gitter.im/asmjit/asmjit)
  * [Permissive Zlib license](./LICENSE.md)

Online Access
-------------

  * [AsmGrid](https://asmjit.com/asmgrid/) is a web application that allows to search data provided by asmdb and cult projects online.

Introduction
------------

CULT (**CPU Ultimate Latency Test**) is a tool that runs series of tests that help to estimate how many cycles an X86 processor (both 32-bit or 64-bit modes supported) takes to execute available instructions. The tool should help people that generate code for X86/X64 hardware by allowing them to run tests on their machines themselves instead of relying on information from CPU vendors or third parties that may be incomplete or that may not provide information for all targeted hardware.

The purpose of CULT is to benchmark as many CPUs as possible, to index the results, and to make them searchable, comparable, and accessible online. This information can be then used for various purposes, like statistics about average latencies of certain instructions (like addition, multiplication, and division) of modern CPUs compared to their predecessors, or as a comparison between various CPU generations for people that still write hand-written assembly to optimize certain operations. The output of CULT is JSON for making the results easier to process by third party tools.

Features
--------

  * **CpuDetect** - Extracts all possible CPUID queries for offline analysis, except for CPU serial code, which is always omitted for privacy reasons (and not available on modern CPUs anyway).
  * **Performance** - Extracts information of instruction cycles and latencies:
    * Every instruction is benchmarked in sequential mode, which means that all consecutive operations depend on each other. This test is used to calculate instruction latencies.
    * Every instruction is benchmarked in parallel mode, which is used to calculate theoretical throughput of the instruction, when used in parallel with instructions of the same kind. CULT displays this information as reciprocal throughput per clock cycle so for example 0.2 means 5 instructions per clock cycle.

TODOs
-----

  * [ ] Instructions that have all registers read only or instructions that only use a single write-only register do not properly show latencies as there is no dependency in the sequence of instructions generated.
  * [ ] Instructions that require consecutive registers (`vp4dpwssd[s]`, `v4f[n]madd{ps|ss}`, `vp2intersect{d|q}`) are not checked at the moment.

Building
--------

CULT requires only AsmJit as a dependency, which it expects by default at the same directory level as `cult` itself. A custom AsmJit directory can be specified with `-DASMJIT_DIR=...` when invoking cmake. The simplest way to compile cult is by using cmake:

```bash
# Clone CULT and AsmJit
$ git clone --depth=1 https://github.com/asmjit/asmjit
$ git clone --depth=1 https://github.com/asmjit/cult

# Create Build Directory
mkdir cult/build
cd cult/build

# Configure and Make
cmake .. -DCMAKE_BUILD_TYPE=Release
make

# Run CULT!
./cult
```

Command Line Arguments
----------------------

`$ cult [parameters]`

  * `--help` - Show possible command line parameters
  * `--dump` - Dump assembly generated and executed (useful for testing)
  * `--quiet` - Run in quiet mode and output only the resulting JSON
  * `--estimate` - Run faster (to verify it works) with less precision
  * `--no-rounding` - Don't round cycles and latencies
  * `--instruction=name` - Only benchmark a single instruction (useful for testing)
  * `--output=file` - Output to a file instead of STDOUT

CULT Output
-----------

CULT outputs information in two formats:

  * Verbose mode that prints what it does into STDOUT
  * JSON, which is send to STDOUT at the end or written to a file

The JSON document has the following structure:

```js
{
  "cult": {
    "version": "X.Y.Z"          // CULT 'major.minor.micro' version.
  },

  // CPU data retrieved by CPUID instruction.
  "cpuData": [
    {
      "level"     : "HEX",      // CPUID:EAX input (main leaf).
      "subleaf"   : "HEX",      // CPUID:ECX input (sub leaf).
      "eax"       : "HEX",      // CPUID:EAX output.
      "ebx"       : "HEX",      // CPUID:EBX output.
      "ecx"       : "HEX",      // CPUID:ECX output.
      "edx"       : "HEX"       // CPUID:EDX output.
    }
    ...
  ],

  // CPU information
  "cpuInfo": {
    "vendorName"  : "String",   // CPU vendor name.
    "vendorString": "String",   // CPU vendor string.
    "brandString" : "String",   // CPU brand string.
    "codename"    : "String",   // CPU code name.
    "modelId"     : "HEX",      // Model ID + Extended Model ID.
    "familyId"    : "HEX",      // Family ID + Extended Family ID.
    "steppingId"  : "HEX"       // Stepping.
  },

  // Array of instructions measured.
  "instructions": [
    {
      "inst"   : "inst x, y"    // Measured instruction and its operands (unique).
      "lat"    : X.YY           // Latency in CPU cycles, including fractions.
      "rcp"    : X.YY           // Reciprocal throughput, including fractions.
    }
    ...
  ]
}
```

Implementation Notes
--------------------

  * The application sets CPU affinity at the beginning to make sure that RDTSC results are read from the same core.
  * AsmJit instruction database & instospection features are used to query all supported instructions. Each instruction with all possible operand combinations is analyzed and benchmarked if the host CPU supports it. System instructions and some rarely used instructions are blacklisted though.
  * A single benchmark uses RDTSC and possibly RDTSCP (if available) to estimate the number of cycles consumed by the test. Tests repeat multiple times and only the best time is considered. A single instruction test is executed multiple times and it only finishes after the time of N best results was achieved.
  * Some instructions are tricky to test and require a bit more instructions for data preparation inside the test (for example division), more special cases are expected in the future.

Authors & Maintainers
---------------------

  * Petr Kobalicek <kobalicek.petr@gmail.com>
