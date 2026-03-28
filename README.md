# libas3 - ActionScript 3 Parser & Bytecode VM

An ActionScript 3 parser and bytecode virtual machine written in C++17.

## Features

### ✅ AS3 Parser
- Statements (if, while, for, for-in, switch, try/catch, etc.)
- Expressions (binary, unary, conditional, assignments, etc.)
- Functions with closures
- Classes, interfaces, packages
- Arrays and objects
- Full operator precedence
- Error recovery and reporting
- **53/53 test cases passing**


### ✅ Bytecode VM
- Stack-based virtual machine
- Constant pool management
- Local variables with stack slots
- Call frame management
- Bytecode disassembler
- Complete AST → Bytecode compiler
- Built-in functions (trace, print, parseInt, parseFloat, isNaN)
- Control flow (if/while/for/ternary)
- Arrays, objects, and all operators (arithmetic, logical, bitwise, comparison)

## Project Structure

```
src/
├── ast/              # AST node definitions
│   ├── ast_nodes.h
│   └── ast_nodes.cpp
│
├── lexer/            # Tokenization
│   ├── token.h
│   ├── lexer.h
│   └── lexer.cpp
│
├── parser/           # AS3 parser (AST generation)
│   ├── parser.h
│   └── parser.cpp
│
├── runtime/          # Runtime value system
│   └── value.h/cpp
│
└── bytecode/         # Bytecode VM
    ├── opcode.h/cpp        # Instruction set
    ├── chunk.h/cpp         # Bytecode container
    ├── compiler.h/cpp      # AST → Bytecode
    └── vm.h/cpp            # Virtual machine

test/
├── test_runner.h          # Shared test framework
├── main.cpp               # Unified test entry point
├── parser_tests.cpp       # Parser tests (52 tests)
├── bytecode_tests.cpp     # Bytecode compiler tests
├── vm_tests.cpp           # VM execution tests
└── benchmark_tests.cpp    # VM performance benchmarks
```

## Building

```bash
# Initial setup
mkdir build && cd build
cmake ..

# Debug build
cmake --build .

# Release build (recommended for benchmarks)
cmake --build . --config Release
```

## Usage

### 1. Parser Only
```cpp
#include "parser/parser.h"

as3::Parser parser("var x = 42;");
auto program = parser.parse();

if (parser.hasErrors()) {
    std::cout << parser.getErrors();
}
```

### 2. Bytecode Compilation & Execution
```cpp
#include "parser/parser.h"
#include "bytecode/compiler.h"
#include "bytecode/vm.h"

// Parse AS3 code
as3::Parser parser("var x = 10; var y = 20; trace('Sum:', x + y);");
auto program = parser.parse();

// Compile to bytecode
as3::Compiler compiler;
auto bytecode = compiler.compile(program.get());

// Execute on VM
as3::VM vm;
vm.execute(bytecode);
// Output: Sum: 30

// Optional: Disassemble for inspection
bytecode->chunk.disassemble("main");
```

### 3. Custom Built-in Functions
```cpp
#include "parser/parser.h"
#include "bytecode/compiler.h"
#include "bytecode/vm.h"

as3::VM vm;

// Register a custom function callable from AS3
vm.registerBuiltin("double", [](const std::vector<as3::ValuePtr>& args) {
    if (args.empty()) return as3::Value::makeNumber(0);
    return as3::Value::makeNumber(args[0]->toNumber() * 2);
});

// Now usable in AS3 code
as3::Parser parser("trace('Result:', double(21));");  // Outputs: Result: 42
auto bytecode = as3::Compiler().compile(parser.parse().get());
vm.execute(bytecode);
```

### 4. Extern Classes with Native Methods
```cpp
as3::VM vm;

// Define a class with native callback methods
vm.defineExternClass("Math", {
    {"abs", [](const std::vector<as3::ValuePtr>& args) {
        return as3::Value::makeNumber(std::abs(args[0]->toNumber()));
    }},
    {"max", [](const std::vector<as3::ValuePtr>& args) {
        double m = args[0]->toNumber();
        for (size_t i = 1; i < args.size(); i++) {
            m = std::max(m, args[i]->toNumber());
        }
        return as3::Value::makeNumber(m);
    }}
});

// Usage in AS3:
as3::Parser parser("var m = new Math(); trace('Abs:', m.abs(-5)); trace('Max:', m.max(1, 3, 2));");
```

### 5. Pre-registered Object Instances
```cpp
as3::VM vm;

// Define class first
vm.defineExternClass("Shape", {
    {"getType", [](const std::vector<as3::ValuePtr>& args) {
        return as3::Value::makeString("circle");
    }}
});

// Register an object instance with that class type
vm.registerObject("hair01", "Shape");

// Usage in AS3:
as3::Parser parser("trace('Type:', hair01.getType());");  // Outputs: Type: circle
```

## Running Tests

All tests are now unified in a single executable:

```bash
# Run all tests (116 tests total)
./Release/as3_tests.exe

# In Debug mode
./Debug/as3_tests.exe
```

Test suites included:
- **Parser** (53 tests) - All AS3 language constructs
- **Bytecode** (18 tests) - Bytecode compiler verification
- **VM** (37 tests) - Bytecode VM execution including function calls, classes, and extern integration
- **Benchmark** (8 tests) - VM performance benchmarks

All test suites run automatically and provide a comprehensive test summary.

## Performance

The bytecode VM provides fast execution with a compact bytecode representation:

- ✅ **Optimized instruction dispatch** - Switch-based dispatch with 90+ opcodes
- ✅ **Compact bytecode** - Efficient constant pool and instruction encoding
- ✅ **Stack-based architecture** - Minimal memory overhead

## Example Programs

### Fibonacci (Recursion)
```actionscript
function fib(n:int):int {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}
trace("fib(10) =", fib(10));  // Output: fib(10) = 55
```

### Loops
```actionscript
for (var i:int = 1; i <= 5; i++) {
    trace("Count:", i);
}
// Output:
// Count: 1
// Count: 2
// Count: 3
// Count: 4
// Count: 5
```

### Objects & Arrays
```actionscript
var person = {name: "Alice", age: 30};
var numbers = [1, 2, 3, 4, 5];
trace("Name:", person.name);        // Name: Alice
trace("First:", numbers[0]);         // First: 1
trace("Length:", numbers.length);    // Length: 5
```

## Bytecode Design

See [BYTECODE_DESIGN.md](src/bytecode/BYTECODE_DESIGN.md) for complete documentation.

### Instruction Set Highlights

- **Constants**: OP_CONSTANT, OP_TRUE, OP_FALSE, OP_NULL, OP_UNDEFINED
- **Arithmetic**: OP_ADD, OP_SUBTRACT, OP_MULTIPLY, OP_DIVIDE, OP_POWER
- **Variables**: OP_GET_LOCAL, OP_SET_LOCAL, OP_GET_GLOBAL, OP_SET_GLOBAL
- **Control**: OP_JUMP, OP_JUMP_IF_FALSE, OP_LOOP
- **Functions**: OP_CALL, OP_CLOSURE, OP_RETURN
- **Objects**: OP_NEW_ARRAY, OP_NEW_OBJECT, OP_GET_PROPERTY, OP_SET_PROPERTY

### Example Bytecode

```actionscript
var x = 10;
var y = 20;
var result = x + y;
```

Compiles to:
```
0000    1 OP_NUMBER        10.0
0009    | OP_DEFINE_GLOBAL 0 'x'
0012    2 OP_NUMBER        20.0
0021    | OP_DEFINE_GLOBAL 1 'y'
0024    3 OP_GET_GLOBAL    0 'x'
0027    | OP_GET_GLOBAL    1 'y'
0030    | OP_ADD
0031    | OP_DEFINE_GLOBAL 2 'result'
```

## Roadmap

### Optimization 📋 PLANNED
- [ ] Inline caching
- [ ] Method dispatch optimization
- [ ] Hot path detection
- [ ] JIT compilation (optional)

## Documentation

- [BYTECODE_DESIGN.md](src/bytecode/BYTECODE_DESIGN.md) - Bytecode VM architecture
- [AST Nodes](src/ast/ast_nodes.h) - Complete AST structure
- [Test Framework](test/README.md) - Test suite documentation

## License

MIT

## Acknowledgments

Inspired by:
- ActionScript 3 Language Specification
- Crafting Interpreters by Robert Nystrom
- Lua VM design
- V8 JavaScript engine
