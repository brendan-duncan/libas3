# libas3 Test Framework

Unified test framework for ActionScript-like parser, interpreter, and bytecode VM.

## Overview

All tests run from a single executable (`as3_tests`) using a shared test runner API.

### Test Suites

1. **Parser** (52 tests) - `parser_tests.cpp`
   - All AS3 language constructs
   - Statements, expressions, declarations
   - Error handling and edge cases

2. **Bytecode** (18 tests) - `bytecode_tests.cpp`
   - AST → Bytecode compilation
   - All operations and control flow
   - Optimization verification

3. **VM** (18 tests) - `vm_tests.cpp`
   - Bytecode execution
   - All VM operations
   - Native function integration
   - User-defined function calls (with/without args, nested calls)

4. **Benchmark** (8 tests) - `benchmark_tests.cpp`
   - VM performance benchmarks
   - Various workload types
   - Execution time measurements

**Total: 92 tests**

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
===== Test Suite =====
Running 4 test suites

========================================
Test Suite: Parser
========================================
[PASS] Empty program
[PASS] Variable with type annotation
...
Suite: Parser - 52/52 passed

========================================
Test Suite: Bytecode
========================================
[PASS] Simple Arithmetic
[PASS] Constant Optimization
...
Suite: Bytecode - 18/18 passed

========================================
Test Suite: VM
========================================
Result: 30
[PASS] Simple Arithmetic
...
Result: 30
[PASS] Function Call - Simple
Answer: 42
[PASS] Function Call - No Args
...
Suite: VM - 18/18 passed

========================================
Test Suite: Benchmark
========================================
    Total: 18973 μs, Per iteration: 1.8973 μs
[PASS] Simple Arithmetic (10k)
...
Suite: Benchmark - 8/8 passed

===== Summary =====
Total Tests: 96
Passed: 96
Failed: 0

All tests passed!
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
- Results show total time and per-iteration time
- **Always use Release builds for accurate benchmarks**
- Debug builds have ~5-10x slower performance

## Test Coverage

| Component | Tests | Coverage |
|-----------|-------|----------|
| Parser | 52 | Full language support |
| Bytecode Compiler | 18 | All operations |
| VM | 18 | Full instruction set + function calls |
| Benchmarks | 8 | VM performance tests |
| **Total** | **96** | **Comprehensive** |

## Future Enhancements

- [ ] Suite filtering by name
- [ ] Test timeout support
- [ ] Test fixtures for setup/teardown
- [ ] Parameterized tests
- [ ] Output capture for verification
- [ ] XML/JSON test reports
- [ ] Code coverage integration
