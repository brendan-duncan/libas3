# ABC (AVM2 Bytecode)

ABC is the abcFile a SWF's DoABC tag carries: the ActionScript 3 bytecode Flash Player's second virtual machine (AVM2) runs. libas3 can compile ActionScript 3 to it, read, write, verify and disassemble it, and run it on the VM.

| Header | |
|--------|-|
| `abc/abc.h` | `abc::compile`: sources and timelines to an abcFile |
| `abc/abc_file.h` | `abc::AbcFile`, the file as data; `read` and `write` |
| `abc/abc_opcodes.h` | The instructions: decoding, names, stack effects |
| `abc/abc_verifier.h` | `verify`, `disassemble` |
| `abc/abc_domain.h` | `abc::Domain`: an abcFile's classes and code run on the VM |

The tests are `test/abc_tests.cpp`, `test/abc_compiler_tests.cpp` and `test/abc_domain_tests.cpp`.

## Compiling ActionScript 3 to ABC

For movies that run in a player rather than in the VM, `as3::abc::compile` makes ABC from the same sources the VM runs: classes in packages, and the frame scripts of timelines.

```cpp
#include "abc/abc.h"

std::vector<as3::abc::Source> sources = {{readFile("com/example/Ball.as"), "Ball.as"}};

as3::abc::Timeline main;                                  // the main timeline's scripts
main.className = as3::abc::timelineClassName("game");     // "game_fla.MainTimeline"
main.instances = {{"ball", "com.example.Ball"}, {"score", "flash.text.TextField"}};
main.frames = {{0, "import com.example.Ball;\nball.speed = 2;\nstop();"}};

as3::abc::Output out = as3::abc::compile(sources, {main});
if (!out.ok) {
    std::cerr << out.errorText();              // "Ball.as:12: error: ..."
} else {
    // out.abc: the DoABC tag's ABC (after its flags, 1 for lazy, and its name).
    // out.classes: "com.example.Ball", ..., "game_fla.MainTimeline", for SymbolClass.
}
```

`out.diagnostics` has each error and warning with its file and line.

### What is compiled

- Each source file is a script: its classes (with their superclasses pushed as scopes as they are made, as compilers do), package functions and variables, and code outside the package. A script runs when one of its definitions is first needed, so files may be given in any order; the timelines' scripts come last.
- A `Timeline` becomes a dynamic subclass of its `baseClass` (`flash.display.MovieClip` when not given) whose constructor calls `addFrameScript(0, frame1, ...)`. The variables and functions of its frames' scripts are its public members, each frame's code is a method (`frame1` for frame 0), its stage instances are public variables of their types, and the `flash.*` packages are imported without import statements, as the authoring tool does. A library symbol exported for ActionScript with no class file is a `Timeline` without frames.
- `timelineClassName(document, symbol, index)` gives the names published movies use: `"<document>_fla.MainTimeline"` for the main timeline, `"<document>_fla.<symbol>_<index>"` for a symbol's.
- Classes the player provides need no definitions: `import flash.display.Sprite` names `flash.display::Sprite`, as `import fl.controls.Button` names that class for a movie that has the components. A type named through a wildcard import is found in a table of the player's classes (`abc_player.cpp`); one not found there is taken as `*`, with a warning.

### Options

- `debugInfo`: `debugfile` and `debugline` instructions, for a debugger player's stack traces and line numbers.
- `constants`: conditional compilation constants (`{"CONFIG::debug", "true"}`).
- `includeHandler`: what `include "file.as"` reads.
- `verify` (on by default): the output is checked with the verifier below, and a problem is an error.

### Not compiled

- Namespaces beyond the four access ones (the parser keeps no `ns::name` or `use namespace`: such members are internal).
- XML namespaces.
- ActionScript 1 and 2: see [AVM1.md](AVM1.md).

A local variable inside `with` is found before the `with` object's property of the same name.

## Reading, writing and verifying

The rest of the format is a library too, for reading ABC from published movies as well as for checking what is made:

```cpp
#include "abc/abc_file.h"
#include "abc/abc_opcodes.h"
#include "abc/abc_verifier.h"

as3::abc::AbcFile file;                        // pools, methods, metadata, instances, classes, scripts, bodies
std::string error;
if (!as3::abc::read(bytes, file, error)) { /* not an abcFile: error says why */ }
std::vector<uint8_t> again = as3::abc::write(file);           // the same bytes

std::vector<as3::abc::Instruction> code;       // a body's instructions, operands decoded
as3::abc::decodeAll(file.bodies[0].code, code, error);
as3::abc::StackEffect effect = as3::abc::stackEffect(file, code[0]);

std::vector<std::string> problems = as3::abc::verify(file);   // empty when it verifies
std::string text = as3::abc::disassemble(file);              // classes, traits, each method's code
std::string body = as3::abc::disassembleBody(file, file.bodies[0]);
```

- `read` takes every abcFile (version 46.16) a compiler makes: all multiname kinds, metadata, optional parameters, every opcode including the debug ones and `lookupswitch`. It reads, and writes back byte for byte, the 249 KB ABC of a movie Flash CS5 published with the fl components in it. A truncated file is an error.
- `verify` checks what a player's verifier checks of the structure (not the types): pool indexes, one body for each method that runs and none for an interface's, instructions that decode, branches onto instructions (a backward one onto a `label`), and along every path an operand stack never below empty or above `max_stack` and a scope stack within `max_scope_depth - init_scope_depth`, the same wherever paths meet, with exception handlers starting from one value and no scopes. Each problem is a line naming the method: `"method 12 (Main/draw): ..."`.
- `methodNames(file)` gives what each method is called where it is used (`"pkg.Class/name"`, `"pkg.Class iinit"`, `"script 0 init"`), for messages.

## Running ABC

`as3::abc::Domain` runs ABC (a published movie's DoABC tags, or what `abc::compile` made) on the VM, so that a movie's compiled classes work as classes compiled from source do:

```cpp
#include "abc/abc_domain.h"

as3::VM vm;                                    // the host's classes defined on it as usual
as3::abc::Domain domain(vm);                   // lives as long as code of it may run
std::string error;
if (!domain.load(abcBytes, error)) { /* not an abcFile */ }
as3::Val main = vm.findDefinition("game_fla.MainTimeline");   // its script runs now
as3::Val clip = vm.construct(main);
```

- A class defined in ABC is a `ClassInfo` like any other. The VM finds it by qualified name (`findDefinition`, `findClass`, `getDefinitionByName`, and the names scripts compiled from source use) before asking the class resolver. It extends native classes (its instance initializer runs as a native constructor does, and `super()` runs the native one), classes from source extend it, and methods are called and overridden either way: native code calling `vm.callMethod(self, "draw")` reaches an ABC subclass's `draw`.
- A script's initializer runs the first time one of its definitions is needed (as with a DoABC tag's lazy flag). `runEntry()` runs the last script of the file loaded last, for code outside classes (a DoABC tag without the lazy flag, or a program with code outside its packages); `runAll()` runs every script not run yet. Both throw `ScriptException` for an ActionScript error.
- `find(name)` gives one of the domain's definitions, `definitionNames()` all of them.
- Methods are interpreted as they are: instructions decoded once per body, branches resolved to instructions, the operand stack, registers and scope stack of each call in a pool of values. Each method is a native function of the VM (so the VM's calls, bound methods, `call` and `apply` reach it), and names are found as AVM2 finds them: on the scope stack (with `with` objects), the scopes the method was made in, then the domain's scripts and the VM's definitions.
- Private members are kept by their class (`pkg.Class::name`), so a subclass's private member does not hide its superclass's; members in other namespaces by their names.
- `domain.takeErrorStack()` gives the methods an error came out of, innermost first, with the instruction's offset (or line, with debug info): `"\tat fl.core.UIComponent/draw()[@12]"`.
- `instructionCount()` counts the instructions run, for tests and profiling.

```cpp
try {
    domain.runEntry();
} catch (as3::ScriptException& e) {
    std::cerr << vm.toString(e.value) << "\n" << domain.takeErrorStack();
}
```

### Not run

The domain memory instructions (`li8`, `si32`...), `callmethod`, and XML namespaces. Argument counts are not checked: a missing argument is `undefined`.

The `flash.*` classes a movie's ABC uses are the host's, defined on the VM as native classes (or, for tests, as stand-ins through the class resolver: see `standIns` in `test/abc_domain_tests.cpp`).
