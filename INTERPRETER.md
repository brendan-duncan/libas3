# ActionScript 3 Tree-Walking Interpreter

A quick AST visitor-based interpreter for executing ActionScript 3 code.

## Architecture

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│    Source    │ ──> │   Parser     │ ──> │  Executor    │
│     Code     │     │   (AST)      │     │  (Runtime)   │
└──────────────┘     └──────────────┘     └──────────────┘
```

### Components

1. **Runtime** (`src/runtime/`)
   - `value.h/cpp` - Runtime value representation (numbers, strings, objects, arrays, functions)
   - `environment.h/cpp` - Variable scope and environment chain

2. **Executor** (`src/executor/`)
   - `executor.h/cpp` - AST visitor that executes statements and evaluates expressions

## Features

### ✅ Working Features

- **Variables**: `var`, `const`, `let`
- **Data Types**: Numbers, strings, booleans, null, undefined
- **Operators**: Arithmetic (+, -, *, /, %), comparison (<, >, <=, >=, ==, !=), logical (&&, ||, !)
- **Control Flow**: if/else, while, do-while, for, for-in, switch/case, break, continue
- **Functions**: Function declarations, return statements, recursion, closures
- **Arrays**: Literals, indexing, length property
- **Objects**: Literals, property access (dot notation)
- **Built-ins**: `trace()`, `print()`, `parseInt()`, `parseFloat()`, `isNaN()`

### ❌ Not Yet Implemented

- Classes and inheritance
- `this` binding in methods
- `new` constructor calls
- Getters/setters
- Exception handling (try/catch/finally)
- Type checking (all dynamic for now)
- Advanced array methods (push, pop, etc.)
- Regular expressions
- Packages and imports

## Usage

### Build

```bash
cd build
cmake ..
cmake --build .
```

### Run Examples

```bash
./Debug/as3_interpreter.exe   # Runs demo with examples
```

### Execute Your Own Code

```cpp
#include "parser/parser.h"
#include "executor/executor.h"

as3::Parser parser("var x = 10; trace(x);");
auto program = parser.parse();

as3::Executor executor;
executor.execute(program.get());
```

## Examples

### Simple Arithmetic
```actionscript
var x = 10;
var y = 20;
trace("Sum:", x + y);  // Output: Sum: 30
```

### Functions
```actionscript
function factorial(n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}
trace("5! =", factorial(5));  // Output: 5! = 120
```

### Arrays
```actionscript
var nums = [1, 2, 3, 4, 5];
for (var i = 0; i < nums.length; i++) {
    trace("nums[" + i + "] =", nums[i]);
}
```

### Objects
```actionscript
var person = {name: "Alice", age: 30};
trace("Name:", person.name);  // Output: Name: Alice
```

## Performance

This is a **tree-walking interpreter** - it directly executes the AST without compilation.

**Pros:**
- Simple implementation (~500 lines of core logic)
- Easy to debug
- Fast to prototype new features

**Cons:**
- ~10-100x slower than bytecode VM
- Not suitable for production games/apps
- High memory usage for complex programs

## Next Steps: Migration to Bytecode

When ready for better performance:

1. Design bytecode instruction set
2. Implement bytecode compiler (AST → bytecode)
3. Build stack-based VM
4. Migrate runtime to work with VM
5. Optional: Add JIT compilation

The current AST visitor helps us understand execution semantics before committing to bytecode design.

## Testing

All parser tests still pass:
```bash
./Debug/as3_parser_test.exe  # 52/52 tests passing
```

## License

MIT
