# libas3 Documentation

libas3 parses ActionScript 3, 2 and 1, and runs it on its own bytecode VM, compiles it to the bytecode Flash Player runs (ABC for ActionScript 3, SWF actions for ActionScript 1 and 2), or translates it to JavaScript. The [top-level README](../README.md) is the overview.

## Writing ActionScript for libas3

- [The language](LANGUAGE.md): ActionScript 3 as libas3 supports it (classes, packages, interfaces, closures, statements, types and `int`/`uint` arithmetic, E4X, metadata, conditional compilation, `include`, `with`), and ActionScript 1 and 2, with where each runs and how they differ.
- [The built-in library](LIBRARY.md): the top-level classes and functions, `flash.utils` and `flash.errors`, and what is not there.

## Using libas3 from C++

- [Embedding](EMBEDDING.md): running scripts on the VM, values, native classes, calling scripts from the host, the class and include resolvers, timers, errors, child VMs.
- [ABC](ABC.md): compiling ActionScript 3 to an abcFile; reading, writing, verifying and disassembling ABC; running it on the VM with `abc::Domain`.
- [AVM1](AVM1.md): compiling ActionScript 1 and 2 to SWF actions, the disassembler, and the interpreter that runs SWF actions on the VM.
- [JavaScript](JAVASCRIPT.md): translating ActionScript 3, and ActionScript 1 and 2, to JavaScript; the output, the runtimes (`as3.js`, `avm1.js`) and the interface a flash.* runtime meets.

## Working on libas3

- [Building and testing](TESTING.md): CMake, the test runner and its suites, the benchmarks, writing tests.
- [Bytecode VM design](BYTECODE_DESIGN.md): values, classes, calls, inline caches, exceptions and the instruction set.

## Which part to use

| You have | You want | Use |
|----------|----------|-----|
| ActionScript 3 (or simple 1 and 2) source | To run it in a C++ host | `VM::load` ([EMBEDDING.md](EMBEDDING.md)) |
| ActionScript 3 source | A SWF's DoABC tag | `abc::compile` ([ABC.md](ABC.md)) |
| A published movie's ABC | To run its classes in a C++ host | `abc::Domain` ([ABC.md](ABC.md)) |
| ActionScript 1 and 2 source | A SWF's DoAction, DoInitAction and event tags | `avm1::Compiler` ([AVM1.md](AVM1.md)) |
| A published movie's SWF actions | To run them in a C++ host | `avm1::Interpreter` ([AVM1.md](AVM1.md)) |
| ActionScript 3 source | JavaScript for an HTML5 page | `js::translate` ([JAVASCRIPT.md](JAVASCRIPT.md)) |
| ActionScript 1 and 2 source | JavaScript for an HTML5 page | `js::as2::translate` ([JAVASCRIPT.md](JAVASCRIPT.md)) |
