# libas3 Test Framework

Unified test framework for ActionScript 3 parser, interpreter, and bytecode VM.

## Overview

All tests run from a single executable (`as3_tests`) using a shared test runner API.

### Test Suites

1. **Parser** (52 tests) - `parser_tests.cpp`
   - All AS3 language constructs
   - Statements, expressions, declarations
   - Error handling and edge cases

2. **Interpreter** (12 tests) - `interpreter_tests.cpp`
   - Tree-walking interpreter execution
   - Functions, loops, conditionals
   - Arrays, objects, closures

3. **Bytecode** (18 tests) - `bytecode_tests.cpp`
   - AST → Bytecode compilation
   - All operations and control flow
   - Optimization verification

4. **VM** (14 tests) - `vm_tests.cpp`
   - Bytecode execution
   - All VM operations
   - Native function integration

5. **Benchmark** (8 tests) - `benchmark_tests.cpp`
   - Performance comparison (Interpreter vs VM)
   - Various workload types
   - Speedup measurements

**Total: 104 tests**

## Test Runner API

Defined in `test_runner.h`:

```cpp
// Define a test suite
TEST_SUITE(SuiteName)

// Initialize tests
static void init_suite_tests() {
    auto& suite = suite_SuiteName;

    suite.addTest("Test Name", []() {
        // Test code
        if (condition) {
            throw std::runtime_error("Test failed");
        }
    });
}

// Auto-register
static struct SuiteInit {
    SuiteInit() { init_suite_tests(); }
} suite_init;
```

### Features

- ✅ Automatic test registration
- ✅ Exception-based test failures
- ✅ Comprehensive test reporting
- ✅ Pass/fail tracking
- ✅ Summary statistics

## Running Tests

```bash
# Debug build
cd build
cmake --build .
./Debug/as3_tests.exe

# Release build (recommended for benchmarks)
cmake --build . --config Release
./Release/as3_tests.exe
```

## Test Output

```
===== ActionScript 3 Test Suite =====
Running 5 test suites

========================================
Test Suite: Parser
========================================
[PASS] Empty program
[PASS] Variable with type annotation
...
Suite: Parser - 52/52 passed

========================================
Test Suite: Interpreter
========================================
x + y = 30
x * y = 200
[PASS] Arithmetic
...
Suite: Interpreter - 12/12 passed

...

===== Summary =====
Total Tests: 104
Passed: 104
Failed: 0

✓ All tests passed!
```

## Adding New Tests

1. Create or open a test file (e.g., `my_tests.cpp`)

2. Use the test suite macro:
```cpp
#include "test_runner.h"

TEST_SUITE(MyTests)

static void init_my_tests() {
    auto& suite = suite_MyTests;

    suite.addTest("My First Test", []() {
        // Test implementation
        if (!check_something()) {
            throw std::runtime_error("Test failed");
        }
    });
}

// Auto-register
static struct MyTestsInit {
    MyTestsInit() { init_my_tests(); }
} my_tests_init;
```

3. Add the file to CMakeLists.txt:
```cmake
add_executable(as3_tests
    test/main.cpp
    test/my_tests.cpp  # Add your file
    ...
)
```

4. Rebuild and run!

## Test Philosophy

- **Fast**: Tests should run quickly (< 1 minute total)
- **Comprehensive**: Cover all major code paths
- **Independent**: Tests don't depend on each other
- **Clear**: Test names describe what they test
- **Isolated**: Each test has its own setup/teardown

## Benchmark Notes

- Benchmarks run many iterations (1k-10k)
- Results are printed with speedup comparisons
- **Always use Release builds for accurate benchmarks**
- Debug builds have ~10x slower performance

## Test Coverage

| Component | Tests | Coverage |
|-----------|-------|----------|
| Parser | 52 | Full language support |
| Interpreter | 12 | All core features |
| Bytecode Compiler | 18 | All operations |
| VM | 14 | Full instruction set |
| Benchmarks | 8 | Representative workloads |

## Future Enhancements

- [ ] Suite filtering by name
- [ ] Test timeout support
- [ ] Test fixtures for setup/teardown
- [ ] Parameterized tests
- [ ] Output capture for verification
- [ ] XML/JSON test reports
- [ ] Code coverage integration
