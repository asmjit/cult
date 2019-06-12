CULT
----

CPU Ultimate Latency Test.

  * [Official Repository (asmjit/cult)](https://github.com/asmjit/cult)
  * [Official Blog (asmbits)](https://asmbits.blogspot.com/ncr)
  * [Official Chat (gitter)](https://gitter.im/asmjit/asmjit)
  * [Permissive Zlib license](./LICENSE.md)

Online Access
-------------

[AsmGrid](https://kobalicek.com/asmgrid) is a web application that allows to search data provided by asmdb and cult projects online. It's a work-in-progress that currently runs on authors own domain until a proper one is registered.

Introduction
------------

CULT (**CPU Ultimate Latency Test**) is a tool that runs series of tests that help to estimate how many cycles an X86 processor (either in 32-bit or 64-bit mode) takes to execute each supported instruction. The tool should help people that target X86/X64 architecture for either JIT or AOT code generation and allows to people to run tests themselves on their machines instead of relying on third party data.

The purpose of CULT is to benchmark as many CPUs as possible, to index the results, and to make them searchable and accessible online. This information can be then used for various purposes, like statistics about average latencies of certain instructions (like addition, multiplication, and division) of modern CPUs compared to their predecessors, or as a comparison between various CPU generations for people that still write hand-written assembly to optimize certain functions. The output of CULT is JSON for making the results easier to be processed by third party tools.

Features
--------

  * **CPUID** - Extracts all possible CPUID queries for offline analysis, except for CPU serial code, which is always omitted for privacy reasons (and not available on modern CPUs anyway).
  * **Performance** - Extracts information of instruction cycles and latencies:
    * Every instruction is benchmarked in sequential mode, which means that all consecutive operations depend on each other. This test is used to calculate instruction latencies.
    * Every instruction is benchmarked in parallel mode, which is used to calculate theoretical reciprocal throughput of the instruction, when used in parallel with instructions of the same kind.

TODOs
-----

  * [ ] The tool at the moment doesn't check all possible instructions. Help welcome!
  * [ ] Finalize the set of GP instructions tested (requires a lot of special cases)
  * [ ] Implement tests that use memory operands
  * [ ] After all done aggregate results in another repository and make them public domain

Compiling
---------

CULT requires only AsmJit as a dependency, which it expects by default at the same directory level as `cult` itself. The simplest way to compile cult is by using cmake:

```bash
# Clone CULT and AsmJit (next-wip branch)
$ git clone --depth=1 https://github.com/asmjit/asmjit --branch next-wip
$ git clone --depth=1 https://github.com/asmjit/cult

# Create Build Directory
mkdir cult/build
cd cult/build

# Configure and Make
cmake .. [-DCMAKE_BUILD_TYPE=Release]
make

# Run CULT!
./cult
```

Command Line Arguments
----------------------

`$ cult [parameters]`

  * `--help` - Show possible command line parameters
  * `--dump` - Dump assembly generated (and executed)
  * `--quiet` - Run in quiet mode and output only the resulting JSON
  * `--estimate` - Run faster (to verify it works) with less precision
  * `--no-rounding` - Don't round cycles and latencies
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

  * The application sets CPU affinity at the beginning to make sure that RDTSC results are related to a single CPU (core).
  * It iterates over instruction database provided by AsmJit and uses AsmJit's validator to test whether an instruction and a particular operand combination is valid and can be executed by the host CPU. If it's valid and executable it becomes a test subject.
  * A single benchmark uses RDTSC and possibly RDTSCP (if available) to estimate the number of cycles consumed by the test. Tests repeat multiple times and only the best time is considered. A single instruction test is executed multiple times and it only finishes after the time of N best results was achieved.
  * Some instructions are tricky to test and require a bit more instructions for data preparation inside the test (for example division), more special cases are expected in the future.

Support
-------

Please consider a donation if you use the project and would like to keep it active in the future.

  * [Donate by PayPal](https://www.paypal.com/cgi-bin/webscr?cmd=_donations&business=QDRM6SRNG7378&lc=EN;&item_name=cult&currency_code=EUR)

Authors & Maintainers
---------------------

  * Petr Kobalicek <kobalicek.petr@gmail.com>
