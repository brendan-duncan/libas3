#include "test_runner.h"
#include "../src/parser/parser.h"
#include "../src/executor/executor.h"
#include "../src/bytecode/compiler.h"
#include "../src/bytecode/vm.h"
#include <chrono>
#include <iostream>

using namespace as3;
using namespace as3test;
using namespace std::chrono;

TEST_SUITE(Benchmark)

// Helper to run benchmark
static void benchmark(const std::string& code, int iterations) {
    // Parse once
    Parser parser(code);
    auto program = parser.parse();

    if (parser.hasErrors()) {
        throw std::runtime_error("Parse error: " + parser.getErrors());
    }

    // Compile to bytecode once
    Compiler compiler;
    auto bytecode = compiler.compile(program.get());

    // Benchmark tree-walking interpreter
    auto start = high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        Executor executor;
        executor.execute(program.get());
    }
    auto end = high_resolution_clock::now();
    auto interpreter_duration = duration_cast<microseconds>(end - start).count();

    // Benchmark bytecode VM
    start = high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        VM vm;
        vm.execute(bytecode);
    }
    end = high_resolution_clock::now();
    auto vm_duration = duration_cast<microseconds>(end - start).count();

    // Results
    double speedup = static_cast<double>(interpreter_duration) / vm_duration;
    std::cout << "    Interpreter: " << interpreter_duration << " μs, "
              << "VM: " << vm_duration << " μs, "
              << "Speedup: " << speedup << "x" << std::endl;
}

static void init_benchmark_tests() {
    auto& suite = suite_Benchmark;

    suite.addTest("Simple Arithmetic (10k)", []() {
        benchmark("var x = 10; var y = 20; var z = x + y;", 10000);
    });

    suite.addTest("Complex Expression (10k)", []() {
        benchmark("var a = 5; var b = 10; var c = 15; var result = (a + b) * c - (a / b);", 10000);
    });

    suite.addTest("Conditional Logic (10k)", []() {
        benchmark("var x = 10; if (x > 5) { var y = x * 2; } else { var y = x / 2; }", 10000);
    });

    suite.addTest("Loop (1k)", []() {
        benchmark("var sum = 0; var i = 0; while (i < 10) { sum = sum + i; i = i + 1; }", 1000);
    });

    suite.addTest("Fibonacci Iterative (1k)", []() {
        benchmark("var a = 0; var b = 1; var i = 0; while (i < 15) { var temp = a + b; a = b; b = temp; i = i + 1; }", 1000);
    });

    suite.addTest("Array Operations (5k)", []() {
        benchmark("var arr = [1, 2, 3, 4, 5]; var sum = arr[0] + arr[1] + arr[2] + arr[3] + arr[4];", 5000);
    });

    suite.addTest("Object Operations (5k)", []() {
        benchmark("var obj = {x: 10, y: 20}; var sum = obj.x + obj.y;", 5000);
    });

    suite.addTest("Built-in Functions (5k)", []() {
        benchmark("var x = parseInt('42'); var y = parseFloat('3.14');", 5000);
    });
}

// Auto-initialize tests
static struct BenchmarkTestInit {
    BenchmarkTestInit() { init_benchmark_tests(); }
} benchmark_test_init;
