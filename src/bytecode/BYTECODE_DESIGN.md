# Bytecode VM Design

## Overview

This is a **stack-based virtual machine** for executing ActionScript 3 bytecode. The design prioritizes simplicity, debuggability, and performance over the tree-walking interpreter.

## Architecture

```
┌────────────┐     ┌──────────────┐     ┌────────────┐
│   Parser   │────>│   Compiler   │────>│     VM     │
│    (AST)   │     │ (AST→Bytecode│     │  (Execute) │
└────────────┘     └──────────────┘     └────────────┘
```

### Components

1. **OpCodes** (`opcode.h/cpp`)
   - 90+ instruction opcodes
   - Stack-based operations
   - Optimized for common patterns

2. **Chunk** (`chunk.h/cpp`)
   - Bytecode container
   - Constant pool
   - Debug information (line numbers)
   - Disassembler

3. **Compiler** (`compiler.h/cpp`)
   - AST → Bytecode translation
   - Local variable resolution
   - Upvalue (closure) handling
   - Jump patching for control flow

4. **VM** (`vm.h/cpp` - ✅ DONE)
   - Instruction dispatch loop
   - Value stack
   - Call frames
   - Global variables
   - Native function callbacks
   - Class and OOP support
   - Native interop (registerBuiltin, defineExternClass, registerObject)

## Instruction Set

### Stack Operations
```
OP_POP          - Pop top value
OP_DUP          - Duplicate top value
OP_SWAP         - Swap top two values
```

### Constants
```
OP_CONSTANT     - Push constant [u16 index]
OP_TRUE         - Push true
OP_FALSE        - Push false
OP_NULL         - Push null
OP_UNDEFINED    - Push undefined
OP_NUMBER_0/1   - Push 0 or 1 (optimized)
```

### Variables
```
OP_GET_GLOBAL   - Get global var [u16 name]
OP_SET_GLOBAL   - Set global var [u16 name]
OP_GET_LOCAL    - Get local var [u8 slot]
OP_SET_LOCAL    - Set local var [u8 slot]
OP_GET_UPVALUE  - Get closure var [u8 index]
OP_SET_UPVALUE  - Set closure var [u8 index]
```

### Arithmetic
```
OP_ADD, OP_SUBTRACT, OP_MULTIPLY, OP_DIVIDE, OP_MODULO
OP_POWER, OP_NEGATE
```

### Comparison
```
OP_EQUAL, OP_NOT_EQUAL            (== !=)
OP_STRICT_EQUAL, OP_STRICT_NOT_EQUAL  (=== !==)
OP_LESS, OP_LESS_EQUAL, OP_GREATER, OP_GREATER_EQUAL
```

### Control Flow
```
OP_JUMP          - Unconditional [s16 offset]
OP_JUMP_IF_FALSE - Jump if falsy [s16 offset]
OP_JUMP_IF_TRUE  - Jump if truthy [s16 offset]
OP_LOOP          - Jump backward [u16 offset]
```

### Functions
```
OP_CALL          - Call function [u8 arg_count]
OP_CLOSURE       - Create closure [u16 func_index]
OP_RETURN        - Return from function
```

### Objects & Arrays
```
OP_NEW_ARRAY     - Create array [u16 count]
OP_NEW_OBJECT    - Create object [u16 count]
OP_GET_PROPERTY  - Get property [u16 name]
OP_SET_PROPERTY  - Set property [u16 name]
OP_GET_INDEX     - Array/object access
OP_SET_INDEX     - Array/object assignment
```

## Example Compilation

### Source Code
```actionscript
function add(a, b) {
    return a + b;
}
var result = add(5, 3);
trace(result);
```

### Bytecode
```
== <script> ==
0000    1 OP_CLOSURE       0 'add'
0003    | OP_DEFINE_GLOBAL 0 'add'
0006    2 OP_GET_GLOBAL    0 'add'
0009    | OP_NUMBER_0
0010    | OP_NUMBER        5.0
0019    | OP_NUMBER        3.0
0028    | OP_CALL          2
0030    | OP_DEFINE_GLOBAL 1 'result'
0033    3 OP_GET_GLOBAL    2 'trace'
0036    | OP_GET_GLOBAL    1 'result'
0039    | OP_CALL          1
0041    | OP_POP
0042    | OP_UNDEFINED
0043    | OP_RETURN

== add ==
0000    2 OP_GET_LOCAL     0
0002    | OP_GET_LOCAL     1
0004    | OP_ADD
0005    | OP_RETURN
```

## Performance Characteristics

### vs Tree-Walking Interpreter

| Metric | Tree-Walking | Bytecode VM | Improvement |
|--------|-------------|-------------|-------------|
| Loop Speed | 1x | 10-50x | ✅ Much faster |
| Function Calls | 1x | 5-20x | ✅ Faster |
| Memory Usage | High | Medium | ✅ More compact |
| Startup Time | Instant | +compile | ⚠️ Slower start |

### Optimizations Implemented

1. **Instruction Compaction**
   - Common constants: OP_NUMBER_0, OP_NUMBER_1, OP_TRUE, OP_FALSE
   - 1-byte opcodes for frequent operations

2. **Local Variable Optimization**
   - Stack slots for locals (no hash lookup)
   - 1-byte addressing (up to 256 locals)

3. **Constant Pool**
   - Deduplication of constants
   - Efficient string/number storage

4. **Jump Patching**
   - Pre-computed jump offsets
   - No runtime label resolution

### Future Optimizations (Not Implemented)

1. **Method Caching**
   - OP_INVOKE for direct method calls
   - Inline caching for property access

2. **Type Specialization**
   - Separate int/float operations
   - Specialized array operations

3. **JIT Compilation**
   - Hot loop detection
   - Native code generation

4. **Garbage Collection**
   - Currently uses shared_ptr (reference counting)
   - Could use mark-sweep or generational GC

## Migration Path from Tree-Walking

### Phase 1: Bytecode Infrastructure (✅ DONE)
- [x] OpCode definitions (90+ opcodes)
- [x] Chunk container
- [x] Compiler (AST → Bytecode)
- [x] Disassembler

### Phase 2: Basic VM (✅ DONE)
- [x] Value stack
- [x] Instruction dispatch
- [x] Arithmetic & comparison
- [x] Variables (global & local)
- [x] Basic control flow

### Phase 3: Functions & Closures (✅ DONE)
- [x] Call frames
- [x] Function calls
- [x] Closures (basic support)
- [x] Return values
- [x] Native function calls

### Phase 4: Objects & Arrays (✅ DONE)
- [x] Object creation
- [x] Property access
- [x] Array operations
- [x] Method calls

### Phase 5: Classes (✅ DONE)
- [x] Class definitions
- [x] Inheritance
- [x] Constructors
- [x] Super calls

### Phase 6: Native Interop (✅ DONE)
- [x] Custom built-in functions (registerBuiltin)
- [x] Extern classes with native methods (defineExternClass)
- [x] Object instance registration (registerObject)

## Usage

### Basic Compilation & Execution
```cpp
// Compile
as3::Compiler compiler;
auto bytecode = compiler.compile(program);

// Disassemble (debug)
bytecode->chunk.disassemble("main");

// Execute
as3::VM vm;
vm.execute(bytecode);
```

### Custom Built-in Functions
```cpp
as3::VM vm;
vm.registerBuiltin("double", [](const std::vector<as3::ValuePtr>& args) {
    return as3::Value::makeNumber(args[0]->toNumber() * 2);
});
// Now usable in AS3: double(21) returns 42
```

### Extern Classes
```cpp
vm.defineExternClass("Math", {
    {"abs", [](const std::vector<as3::ValuePtr>& args) {
        return as3::Value::makeNumber(std::abs(args[0]->toNumber()));
    }}
});
// Now usable in AS3: new Math().abs(-5)
```

### Pre-registered Objects
```cpp
vm.defineExternClass("Shape", {...});
vm.registerObject("hair01", "Shape");
// Now usable in AS3: hair01.getType()
```

## Testing Strategy

1. **Unit Tests**
   - Each opcode individually
   - Edge cases (stack overflow, etc.)

2. **Integration Tests**
   - Compile + execute known programs
   - Compare output with tree-walking interpreter

3. **Performance Tests**
   - Fibonacci (recursion)
   - Array operations (loops)
   - Object manipulation
   - Built-in function performance

4. **Compatibility Tests**
   - All 53 parser tests
   - All 18 bytecode compiler tests
   - All 37 VM tests (including native interop)
   - 8 benchmark tests

## Next Steps

What's implemented:

1. ✅ Design instruction set (90+ opcodes)
2. ✅ Implement Chunk with constant pool
3. ✅ Implement Compiler (AST → Bytecode)
4. ✅ Implement VM with full instruction dispatch
5. ✅ Add disassembler
6. ✅ Test & benchmark (116 tests passing)
7. ✅ Native interop (registerBuiltin, defineExternClass, registerObject)

## Advanced Optimizations (Future Work)

- [ ] Inline caching
- [ ] Method dispatch optimization
- [ ] Computed goto for dispatch
- [ ] JIT compilation
- [ ] Improved garbage collection
