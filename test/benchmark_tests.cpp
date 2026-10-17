#include "test_runner.h"
#include "../src/parser/parser.h"
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

    // Benchmark bytecode VM: one VM, the script run again and again.
    VM vm;
    auto start = high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        vm.execute(bytecode);
    }
    auto end = high_resolution_clock::now();
    auto vm_duration = duration_cast<microseconds>(end - start).count();
    if (vm.hasError()) {
        throw std::runtime_error("Runtime error: " + vm.getError());
    }

    // Results
    double per_iteration = static_cast<double>(vm_duration) / iterations;
    std::cout << "    Total: " << vm_duration << " us, "
              << "Per iteration: " << per_iteration << " us" << std::endl;
}

// Runs a script once on a VM (with what the host defines first) and says
// how long it took.
static double timeScript(VM& vm, const std::string& code, const char* what) {
    Parser parser(code);
    auto program = parser.parse();
    if (parser.hasErrors()) {
        throw std::runtime_error("Parse error: " + parser.getErrors());
    }
    Compiler compiler;
    auto bytecode = compiler.compile(program.get());
    auto start = high_resolution_clock::now();
    vm.execute(bytecode);
    auto end = high_resolution_clock::now();
    if (vm.hasError()) {
        throw std::runtime_error("Runtime error: " + vm.getError());
    }
    const double ms = static_cast<double>(duration_cast<microseconds>(end - start).count()) / 1000.0;
    std::cout << "    " << what << ": " << ms << " ms" << std::endl;
    return ms;
}

namespace {

struct Pixels : HostObject {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> data;
};

} // namespace

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

    suite.addTest("VM creation (100)", []() {
        auto start = high_resolution_clock::now();
        for (int i = 0; i < 100; i++) {
            VM vm;
        }
        auto end = high_resolution_clock::now();
        std::cout << "    Per VM: " << duration_cast<microseconds>(end - start).count() / 100.0 << " us" << std::endl;
    });

    // A million times: arithmetic, a method call, a property read and
    // write, as a class's code does it.
    suite.addTest("1M iterations: arithmetic, method call, property", []() {
        VM vm;
        timeScript(vm,
                   "class Particle {\n"
                   "  public var x:Number = 0;\n"
                   "  public var hits:int = 0;\n"
                   "  public function step(dx:Number):Number { x += dx; return x; }\n"
                   "}\n"
                   "function loop():Number {\n"
                   "  var p:Particle = new Particle();\n"
                   "  var sum:Number = 0;\n"
                   "  for (var i:int = 0; i < 1000000; i++) {\n"
                   "    sum += i * 0.5 + (i & 7);\n"
                   "    p.step(0.25);\n"
                   "    p.hits = p.hits + 1;\n"
                   "  }\n"
                   "  return sum + p.x + p.hits;\n"
                   "}\n"
                   "var total = loop();\n",
                   "1M iterations");
        if (Val(vm.getGlobal("total")).toNumber() != 250014000000.0 + 250000 + 1000000 - 3500000 + 3500000) {
            // sum = 0.5 * (n-1)n/2 + 3.5n; checked loosely below.
        }
        const double total = Val(vm.getGlobal("total")).toNumber();
        const double expected = 0.5 * 999999.0 * 1000000.0 / 2.0 + 3.5 * 1000000.0 + 250000.0 + 1000000.0;
        if (total != expected) {
            throw std::runtime_error("wrong result " + numberToString(total));
        }
    });

    // As a paint program's brush does: per pixel of a 320 by 320 bitmap,
    // ten calls to the host's getPixel32 and setPixel32, and ARGB arithmetic.
    suite.addTest("320x320 pixels, 10 native calls each", []() {
        VM vm;
        vm.defineNativeClass("flash.display.BitmapData")
            .constructor([](VM&, const Val& self, Args args) {
                auto pixels = std::make_shared<Pixels>();
                pixels->width = args.integer(0);
                pixels->height = args.integer(1);
                pixels->data.assign(static_cast<size_t>(pixels->width * pixels->height), 0xFF808080u);
                self->host = pixels;
                return Val();
            })
            .method("getPixel32",
                    [](VM&, const Val& self, Args args) {
                        Pixels* pixels = self.host<Pixels>();
                        const int x = args.integer(0);
                        const int y = args.integer(1);
                        if (x < 0 || y < 0 || x >= pixels->width || y >= pixels->height) return Val::number(0);
                        return Val::number(pixels->data[static_cast<size_t>(y * pixels->width + x)]);
                    },
                    2)
            .method("setPixel32",
                    [](VM&, const Val& self, Args args) {
                        Pixels* pixels = self.host<Pixels>();
                        const int x = args.integer(0);
                        const int y = args.integer(1);
                        if (x >= 0 && y >= 0 && x < pixels->width && y < pixels->height) {
                            pixels->data[static_cast<size_t>(y * pixels->width + x)] = args[2].toUint32();
                        }
                        return Val();
                    },
                    3)
            .define();
        timeScript(vm,
                   "import flash.display.BitmapData;\n"
                   "class Brush {\n"
                   "  private var mCanvas:BitmapData;\n"
                   "  public function Brush(canvas:BitmapData) { mCanvas = canvas; }\n"
                   "  public function dab(w:int, h:int):void {\n"
                   "    for (var y:int = 0; y < h; y++) {\n"
                   "      for (var x:int = 0; x < w; x++) {\n"
                   "        var a:uint = mCanvas.getPixel32(x, y);\n"
                   "        var b:uint = mCanvas.getPixel32(x + 1, y);\n"
                   "        var c:uint = mCanvas.getPixel32(x, y + 1);\n"
                   "        var d:uint = mCanvas.getPixel32(x - 1, y);\n"
                   "        var e:uint = mCanvas.getPixel32(x, y - 1);\n"
                   "        var f:uint = mCanvas.getPixel32(x + 1, y + 1);\n"
                   "        var g:uint = mCanvas.getPixel32(x - 1, y - 1);\n"
                   "        var h2:uint = mCanvas.getPixel32(x + 1, y - 1);\n"
                   "        var i:uint = mCanvas.getPixel32(x - 1, y + 1);\n"
                   "        var r:uint = (((a >> 16) & 0xFF) + ((b >> 16) & 0xFF) + ((c >> 16) & 0xFF) + ((d >> 16) & 0xFF)) >> 2;\n"
                   "        var gr:uint = (((e >> 8) & 0xFF) + ((f >> 8) & 0xFF) + ((g >> 8) & 0xFF) + ((h2 >> 8) & 0xFF)) >> 2;\n"
                   "        var bl:uint = (i & 0xFF);\n"
                   "        mCanvas.setPixel32(x, y, 0xFF000000 | (r << 16) | (gr << 8) | bl);\n"
                   "      }\n"
                   "    }\n"
                   "  }\n"
                   "}\n"
                   "var canvas:BitmapData = new BitmapData(320, 320, true, 0xFF808080);\n"
                   "new Brush(canvas).dab(320, 320);\n",
                   "320x320 dab (1M native calls)");
    });
}

// Auto-initialize tests
static struct BenchmarkTestInit {
    BenchmarkTestInit() { init_benchmark_tests(); }
} benchmark_test_init;
