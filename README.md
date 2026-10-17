# libas3 - ActionScript 3 Compiler & Bytecode VM

An ActionScript 3 parser, compiler and bytecode virtual machine written in C++17, for running real ActionScript programs (classes in packages, a host's native classes, the built-in library) inside a host application. It also compiles ActionScript 3 to ABC and ActionScript 1 and 2 to SWF actions for Flash Player, runs both on the VM, and translates all three to JavaScript.

## Experimental Status

This project is being developed for my own learning and experimentation. There are no guarantees of accuracy, feature completion, or support.

## Features

### The language
- Classes in packages: constructors with `super(...)` (an implicit `super()` when left out), `super.method()` and `super.prop`, fields initialized for each instance, static members, getters and setters (overridden and inherited), `override`, `final`, `dynamic`, and the access attributes (accepted, not enforced)
- Interfaces: declared, extending others, implemented; `is` and `as` with them
- Methods read off an object are bound to it: `addEventListener(MouseEvent.CLICK, onClick)` calls `onClick` with the right `this`, and `obj.f == obj.f`
- Packages: fully qualified names, single and wildcard imports, classes of the same package without an import, and a class resolver that loads classes when they are first needed
- Statements: `switch`, `for-in` and `for each-in`, labeled `break` and `continue`, `try`/`catch` (several, by type)/`finally`/`throw`, `with`, `include "file.as"`
- Functions: closures, nested functions, default parameter values, `...rest`, `arguments`, `call` and `apply`
- Expressions: `typeof`, `delete`, `instanceof`, `in`, `is`, `as`, `??`, regular expression literals, `new <int>[1, 2]` vector literals
- Types: `int` and `uint` wrap to 32 bits where declared, typed variables start at their type's default, `Vector.<T>`
- E4X: XML literals, `x.child`, `x.@attribute`, `x..descendant`, filters, assignment and `delete`
- Metadata (`[Embed(...)]`, `[Event(...)]`) kept for the host; conditional compilation (`CONFIG::debug`)
- ActionScript 1 and 2: statements ending at line breaks, `on (release) { }` and `onClipEvent (...) { }`, `tellTarget`, `ifFrameLoaded`, scripts that run on a timeline, ActionScript 2 classes

Not supported: namespaces beyond accepting their syntax, and XML namespaces. See [docs/LANGUAGE.md](docs/LANGUAGE.md).

### The library
`Object`, `Array`, `Vector.<T>`, `String` (counting in UTF-16 units), `Number`, `int`, `uint`, `Boolean`, `Math`, `Date`, `RegExp`, `JSON`, `XML`, `XMLList`, `Error` and its subclasses, `Function`, `flash.utils` (`Dictionary`, `getDefinitionByName`, `getQualifiedClassName`, `describeType`, `getTimer`, the timers) and the top-level functions. The Flash Player API (display list, events...) is the host's to provide as native classes. See [docs/LIBRARY.md](docs/LIBRARY.md).

### Beyond the VM
- **ABC**: ActionScript 3 compiled to the abcFile a SWF's DoABC tag carries; a reader, writer, verifier and disassembler for ABC; and `abc::Domain`, which runs a movie's ABC on the VM. See [docs/ABC.md](docs/ABC.md).
- **AVM1**: ActionScript 1 and 2 compiled to SWF action bytecode, a disassembler, and an interpreter that runs SWF actions on the VM. See [docs/AVM1.md](docs/AVM1.md).
- **JavaScript**: ActionScript 3 translated to readable ES2020 classes on `as3.js`, and ActionScript 1 and 2 to a module on `avm1.js`, for movies published to HTML5. See [docs/JAVASCRIPT.md](docs/JAVASCRIPT.md).

## Documentation

The [docs](docs/README.md) folder has the details:

- [The language](docs/LANGUAGE.md): ActionScript 3, and ActionScript 1 and 2, as libas3 supports them, and how they differ
- [The built-in library](docs/LIBRARY.md)
- [Embedding](docs/EMBEDDING.md): the host API: running scripts, values, native classes, resolvers, timers, errors
- [ABC](docs/ABC.md): compiling to, reading, verifying and running AVM2 bytecode
- [AVM1](docs/AVM1.md): compiling to, disassembling and running SWF action bytecode
- [JavaScript](docs/JAVASCRIPT.md): the AS3 and AS2 translators and their runtimes
- [Building and testing](docs/TESTING.md)
- [Bytecode VM design](docs/BYTECODE_DESIGN.md)

## Project Structure

```
src/
├── ast/              # AST node definitions
├── lexer/            # Tokenization
├── parser/           # The parser (AST generation), ActionScript 3, 2 and 1
├── runtime/          # Values, classes, the built-in library
│   ├── value.h/cpp         # Val, Value, ClassInfo, HostObject
│   └── builtins_*.cpp      # Object, Array, String, Math, Date, RegExp, JSON, XML...
├── bytecode/         # Compiler and VM
│   ├── opcode.h/cpp        # Instruction set
│   ├── chunk.h/cpp         # Bytecode, functions and classes as compiled
│   ├── compiler.h/cpp      # AST → Bytecode
│   ├── vm.h/cpp            # The interpreter loop, calls, exceptions
│   └── vm_runtime.cpp      # Properties, names, classes, native classes
├── abc/              # ActionScript 3 → ABC (AVM2 bytecode), and running ABC
│   ├── abc.h               # compile(): sources and timelines to an abcFile
│   ├── abc_domain.h/cpp    # Domain: an abcFile's classes and code run on the VM
│   ├── abc_model.h/cpp     # The front end the compilers share: definitions, classes, timelines
│   ├── abc_file.h/cpp      # The abcFile as data; read() and write()
│   ├── abc_opcodes.h/cpp   # AVM2's instructions, decoding, stack effects
│   ├── abc_verifier.h/cpp  # verify(), disassemble()
│   ├── abc_builder.h/cpp   # Constant pools and method code as they are made
│   ├── abc_player.h/cpp    # The player's classes, by package
│   └── abc_compiler.cpp    # AST → ABC
├── avm1/             # ActionScript 1 and 2 → SWF actions, and running them
│   ├── avm1.h              # Compiler, disassemble
│   ├── avm1_actions.h      # The action codes
│   ├── avm1_compiler.cpp   # AST → actions
│   ├── avm1_disassembler.cpp
│   └── avm1_interpreter.h/cpp  # Interpreter: SWF actions run on the VM
└── js/               # ActionScript 3, 1 and 2 → JavaScript, for HTML5 pages
    ├── js.h                # translate(): sources and timelines to JavaScript
    ├── js_translator.cpp   # AST → JavaScript
    ├── js_writer.h/cpp     # Lines, source maps, JavaScript literals
    ├── as3.js              # The runtime translated code needs (js::runtime())
    ├── js_runtime.cpp      # as3.js, embedded
    ├── as2.h               # as2::translate(): ActionScript 1 and 2 to JavaScript
    ├── as2_translator.cpp  # AST → JavaScript on avm1.js
    └── avm1.js             # AVM1's semantics for translated code (as2::runtime())

docs/                 # Documentation
examples/             # A sample script
test/                 # One executable, a suite per file
```

## Building

```bash
cmake -S . -B build
cmake --build build --config Release
./build/Release/as3_tests.exe            # every suite
./build/Release/as3_tests.exe language   # one suite
```

See [docs/TESTING.md](docs/TESTING.md) for using libas3 inside another CMake project, the test suites and the benchmarks.

## Usage

### Running a script
```cpp
#include "bytecode/vm.h"

as3::VM vm;
// Parses, compiles and runs. False (with the error kept) when it fails.
if (!vm.load("var x = 10; trace('Sum:', x + 20);", "frame1.as")) {
    std::cerr << vm.getError() << " at line " << vm.getErrorLine() << "\n" << vm.getErrorStack();
}
```

Values are `as3::Val`s: a number or boolean in place, a string or object by reference. The host calls into scripts with `vm.callFunction`, `vm.callMethod`, `vm.construct`, `vm.getProperty` and `vm.setProperty`, and gives the VM the source of classes it needs with `vm.setClassResolver`. See [docs/EMBEDDING.md](docs/EMBEDDING.md).

### Native classes
The host implements classes such as `flash.display.Sprite` or `flash.geom.Rectangle` in C++, and scripts use and extend them:

```cpp
#include "bytecode/vm.h"
#include "runtime/builtins.h"    // hostOf

struct RectState : as3::HostObject {
    double x = 0, y = 0, width = 0, height = 0;
};

using namespace as3;

vm.defineNativeClass("flash.geom.Rectangle")
    .constructor([](VM& vm, const Val& self, Args args) {   // new Rectangle(...), and a subclass's super(...)
        auto state = std::make_shared<RectState>();
        state->width = args.number(2);
        state->height = args.number(3);
        self->host = state;
        return Val();
    })
    .getter("width", [](VM& vm, const Val& self, Args) {
        return Val::number(hostOf<RectState>(vm, self, "Rectangle").width);
    })
    .method("isEmpty", [](VM& vm, const Val& self, Args) {
        RectState& r = hostOf<RectState>(vm, self, "Rectangle");
        return Val::boolean(r.width <= 0 || r.height <= 0);
    })
    .define();
```

A script class extending it keeps the native state, finds the native members unqualified, and overrides them; native code calling `vm.callMethod(self, "toString")` reaches the script's override. [docs/EMBEDDING.md](docs/EMBEDDING.md#native-classes) has the whole builder: setters, statics, fields, prototype methods, and instances the host makes itself.

## Compiling to ABC

```cpp
#include "abc/abc.h"

as3::abc::Timeline main;
main.className = as3::abc::timelineClassName("game");   // "game_fla.MainTimeline"
main.frames = {{0, "import com.example.Ball;\nball.speed = 2;\nstop();"}};
main.instances = {{"ball", "com.example.Ball"}};

as3::abc::Output out = as3::abc::compile({{readFile("com/example/Ball.as"), "Ball.as"}}, {main});
if (!out.ok) std::cerr << out.errorText();
// out.abc for a DoABC tag, out.classes for SymbolClass
```

`abc_file.h`, `abc_opcodes.h` and `abc_verifier.h` read, write, verify and disassemble ABC, and `abc::Domain` runs it on the VM. See [docs/ABC.md](docs/ABC.md).

## SWF action bytecode (AVM1)

`avm1/avm1.h` compiles ActionScript 1 and 2 to the actions SWF movies carry for Flash Player's first virtual machine, for a host writing SWF files:

```cpp
#include "avm1/avm1.h"

as3::avm1::Compiler compiler;
as3::avm1::FrameScript frame = compiler.compileFrameScript("stop();\nhat._alpha = 50;");
// frame.actions is a DoAction tag's body; frame.initActions, what was
// between #initclip and #endinitclip, a DoInitAction's.
std::vector<as3::avm1::Handler> handlers = compiler.compileHandlers("on (release) { gotoAndPlay(2); }");
as3::avm1::ClassCode hat;
compiler.compileClass(classSource, hat);   // an ActionScript 2 class, for a DoInitAction
std::string listing = as3::avm1::disassemble(frame.actions);
```

`avm1::Interpreter` (`avm1/avm1_interpreter.h`) runs SWF actions on the VM against a host that answers for the movie's clips. See [docs/AVM1.md](docs/AVM1.md).

## Translating to JavaScript

`as3::js::translate` makes readable JavaScript (ES2020 classes) from the same sources and timelines as the ABC compiler. The JavaScript runs on `as3.js` (`as3::js::runtime()`, the source in `src/js/as3.js`) and a JavaScript implementation of the flash.* classes:

```cpp
#include "js/js.h"

// main: a Timeline, as for ABC above.
as3::js::Output out = as3::js::translate({{readFile("com/example/Ball.as"), "Ball.as"}}, {main});
if (!out.ok) std::cerr << out.errorText();
// out.files: "com/example/Ball.js", ...; out.bundle: all of them; out.sourceMaps
```

[docs/JAVASCRIPT.md](docs/JAVASCRIPT.md) describes the JavaScript made, the language's semantics it keeps, what is not translated, and the interface the flash.* runtime meets (`as3.define`, `as3.register`, the `$construct` protocol, `as3.output`, the timers).

## ActionScript 1 and 2 to JavaScript

`as3::js::as2::translate` (`js/as2.h`) makes one JavaScript module of a movie's ActionScript 1 and 2: its frame scripts, the `on()` and `onClipEvent()` handlers of its instances, and its ActionScript 2 classes. It runs on `avm1.js` (`as3::js::as2::runtime()`, the source in `src/js/avm1.js`), which gives the language as AVM1 has it, and on a host that gives movie clips their ActionScript 2 face. [docs/JAVASCRIPT.md](docs/JAVASCRIPT.md#actionscript-1-and-2-to-javascript) describes the module and what the host gives (`avm1.host`, `avm1.runMovies`).

## Running Tests

```bash
./build/Release/as3_tests.exe                # every suite
./build/Release/as3_tests.exe nativeclasses  # one
```

Suites: Parser, Bytecode, VM, AS2, Language, Library, XML, NativeClasses, BunnyPaint, Errors, Abc, AbcCompiler, AVM1, AVM1Run, AbcDomain, JavaScript, JavaScriptAS2, and Benchmark. [docs/TESTING.md](docs/TESTING.md) says what each covers and what some need (Node, sources outside the repository).

## Performance

A stack-based interpreter with values that hold numbers in place, inline caches on property reads, writes and method calls (a class's field is a slot, found without a lookup by name once seen), fused instructions for `this.x` and `local.x`, call frames used again rather than made, and loops tested at the bottom. On a recent desktop (Release build):

- 1,000,000 iterations of arithmetic, a method call and a property read and write: about 100 ms
- 320 by 320 pixels with ten native calls each (1,000,000 calls to a host's `getPixel32` and `setPixel32`): about 65 ms
- Making a VM with its built-in library: about 0.25 ms

## Bytecode Design

See [docs/BYTECODE_DESIGN.md](docs/BYTECODE_DESIGN.md).

## Acknowledgments

Inspired by:
- ActionScript Language Specification
- [Crafting Interpreters](https://craftinginterpreters.com) by Robert Nystrom
- Lua VM design
- V8 JavaScript engine
