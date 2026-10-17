# Building and Testing

## Building

libas3 is a C++17 static library, `as3_parser`, built with CMake 3.15 or later. There are no dependencies beyond the standard library.

```bash
cmake -S . -B build
cmake --build build --config Release
```

This builds the library and, when libas3 is the top-level project, the test executable `as3_tests` and a package folder (`build/package`: the library and every header under `include/libas3/`).

`src/js/as3.js` and `src/js/avm1.js` are embedded into the library at build time (`src/js/embed.cmake` writes them to `generated/*.inc` in the build folder), so `js::runtime()` and `js::as2::runtime()` need no files at run time. Editing either `.js` file rebuilds the embedded copy.

### Inside another project

Added with `add_subdirectory`, libas3 builds only the library: no tests, no package.

```cmake
add_subdirectory(libas3)
target_link_libraries(my_player PRIVATE as3_parser)
```

The library's include directories are public: `src` and each of its subfolders. Headers are included by their folder:

```cpp
#include "bytecode/vm.h"      // as3::VM, NativeClassBuilder
#include "parser/parser.h"    // as3::Parser
#include "bytecode/compiler.h"
#include "abc/abc.h"          // as3::abc::compile
#include "abc/abc_domain.h"   // as3::abc::Domain
#include "avm1/avm1.h"        // as3::avm1::Compiler, disassemble
#include "avm1/avm1_interpreter.h"
#include "js/js.h"            // as3::js::translate
#include "js/as2.h"           // as3::js::as2::translate
```

## Running the tests

Every suite is in one executable. With no argument it runs them all; with a suite's name (in any case) it runs that one.

```bash
./build/Release/as3_tests              # every suite
./build/Release/as3_tests language     # one suite
./build/Release/as3_tests benchmark
```

On a single-configuration generator (Ninja, Makefiles) the executable is `build/as3_tests`. The exit code is 0 when every test passed and 1 otherwise. (`as3_tests --help` prints an older list of suite names; the names below are the current ones.)

Each test prints `[PASS] name` or `[FAIL] name: why`, each suite a count, and the run a summary.

### The suites

| Suite | File | What it covers |
|-------|------|----------------|
| `Parser` | `parser_tests.cpp` | The parser: declarations, statements, expressions, words that are keywords only in places, metadata, numbers and strings |
| `Bytecode` | `bytecode_tests.cpp` | AST to bytecode |
| `VM` | `vm_tests.cpp` | Running bytecode; the older host API (`registerBuiltin`, `defineExternClass`, `registerObject`) and child VMs |
| `AS2` | `as2_tests.cpp` | ActionScript 1 and 2 on the VM: line-break statement ends, `on`/`onClipEvent`/`tellTarget`/`ifFrameLoaded`, host objects, timeline scopes |
| `Language` | `language_tests.cpp` | Classes, packages, interfaces, statements, closures, operators, types and numbers, metadata, conditional compilation, `with`, `include` |
| `Library` | `library_tests.cpp` | The built-in classes and functions |
| `XML` | `xml_tests.cpp` | E4X, `describeType`, and as3corelib's JSON encoder (which uses both) |
| `NativeClasses` | `native_class_tests.cpp` | Host classes made with `defineNativeClass`, script classes extending them, a JPEG encoder run on a native BitmapData |
| `BunnyPaint` | `bunnypaint_tests.cpp` | A 6,000-line paint program and as3corelib's JPEG encoder, compiled and loaded through the class resolver |
| `Errors` | `error_tests.cpp` | Compile errors, runtime errors and their messages, stack traces, recursion limits, broken input |
| `Abc` | `abc_tests.cpp` | Reading, writing and verifying ABC, Flash CS5's among it |
| `AbcCompiler` | `abc_compiler_tests.cpp` | ActionScript 3 compiled to ABC and verified, timelines too |
| `AVM1` | `avm1_tests.cpp` | ActionScript 1 and 2 compiled to SWF actions, checked by their listing and by a small AVM1 machine in the tests (`avm1_machine.h`) |
| `AVM1Run` | `avm1_run_tests.cpp` | The AVM1 interpreter: compiled actions run on the VM against a test host, compared with the same scripts compiled for the VM |
| `AbcDomain` | `abc_domain_tests.cpp` | Programs compiled to ABC and run by an `abc::Domain`, compared with the same source run directly; ABC classes over native ones and under source ones; a published movie's ABC |
| `JavaScript` | `js_tests.cpp` | ActionScript 3 translated to JavaScript, and, where Node is installed, programs run both in the VM and in Node, compared |
| `JavaScriptAS2` | `js_as2_tests.cpp` | ActionScript 1 and 2 translated to JavaScript, run in Node on `avm1.js` and compared with the AVM1 test machine and with what Flash Player traces |
| `Benchmark` | `benchmark_tests.cpp` | Timings (see below) |

### What some tests need

Some tests use files outside the repository, and pass without running those parts when they are not there (most print `skipped`):

- Node (`node` on the `PATH`): the JavaScript and JavaScriptAS2 suites translate without it, but only run the translated programs with it.
- The bunnyPaint sources and as3corelib, at the fixed path `D:/Backup/hulabunny/bunnyPaint/`: the BunnyPaint suite and tests in the Abc, AbcCompiler, AbcDomain, NativeClasses, XML and JavaScript suites.

Environment variables:

- `AS3_JS_DUMP`: the JavaScript suite prints the JavaScript it made.
- `AS3_AVM1_SWFS`: folders of SWF movies, separated by semicolons. The AVM1Run suite runs every action block of every movie in them through the interpreter and reports the errors. Without it, that test does nothing.

### Benchmarks

The Benchmark suite times small programs (arithmetic, loops, arrays, objects, built-in calls, making a VM) and two larger ones: 1,000,000 iterations of arithmetic, a method call and a property read and write; and 320 by 320 pixels with ten native calls each. It prints the time each took. Run it from a Release build: a Debug build is several times slower.

```bash
./build/Release/as3_tests benchmark
```

### Profiling with Tracy

With the CMake option `AS3_TRACY`, libas3 marks what a host asks of it as [Tracy](https://github.com/wolfpld/tracy) zones: parsing (`AS3 parse`), compiling (`AS3 compile`), running a script (`AS3 load`, `AS3 execute`), calling one from the host (`AS3 call`, with the function's name), running timers (`AS3 timers`), AVM1 actions (`AVM1 run`, `AVM1 call`, `AVM1 call method`, with where they came from) and ABC (`ABC load`, `ABC run entry`, `ABC run all`). Without the option the zones compile to nothing.

```bash
cmake -S . -B build -DAS3_TRACY=ON
```

Tracy v0.14.1 is fetched, and its `TRACY_ENABLE` turned on, unless the project already has a `Tracy::TracyClient` target or `AS3_TRACY_SOURCE` names a Tracy checkout to build. The profiler that reads the trace must be the same version as the client. Run the host and connect the Tracy profiler to it, or capture without the interface:

```bash
tracy-capture -o run.tracy -a 127.0.0.1    # waits for the program
TRACY_NO_EXIT=1 ./my_host                   # stays until the trace is sent
tracy-csvexport run.tracy                   # the zones, as CSV
```

The zones are on calls into libas3, not inside the interpreter loop: each costs some tens of nanoseconds, too much for every instruction or every call a script makes.

## Writing tests

The runner is `test/test_runner.h`. A test is a function that throws (any `std::exception`) to fail:

```cpp
#include "test_runner.h"
#include "../src/bytecode/vm.h"

using namespace as3;

TEST_SUITE(MySuite)

static void init_my_tests() {
    auto& suite = suite_MySuite;

    suite.addTest("Adds", []() {
        VM vm;
        std::string out;
        vm.defineFunction("trace", [&out](VM& vm, const Val&, Args args) {
            out += vm.toString(args[0]) + "\n";
            return Val();
        });
        if (!vm.load("trace(1 + 2);", "adds.as")) throw std::runtime_error(vm.getError());
        if (out != "3\n") throw std::runtime_error("got " + out);
    });
}

static struct MyTestsInit {
    MyTestsInit() { init_my_tests(); }
} my_tests_init;
```

`TEST_SUITE(Name)` registers the suite under that name (what the command line's filter matches). Add the file to the `as3_tests` sources in `CMakeLists.txt`.

Most suites follow the same pattern as above: run a program, gather what it traces through a `trace` the test defines, and compare the text with what ActionScript gives.
