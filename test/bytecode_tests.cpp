#include "test_runner.h"
#include "../src/parser/parser.h"
#include "../src/bytecode/compiler.h"
#include <stdexcept>

using namespace as3;
using namespace as3test;

TEST_SUITE(Bytecode)

// Helper to compile code and verify no errors
static void compileAndVerify(const std::string& code) {
    Parser parser(code);
    auto program = parser.parse();

    if (parser.hasErrors()) {
        throw std::runtime_error("Parse error: " + parser.getErrors());
    }

    Compiler compiler;
    auto bytecode = compiler.compile(program.get());

    if (bytecode == nullptr) {
        throw std::runtime_error("Compilation failed");
    }
}

static void init_bytecode_tests() {
    auto& suite = suite_Bytecode;

    suite.addTest("Simple Arithmetic", []() {
        compileAndVerify("var x = 10; var y = 20; var result = x + y;");
    });

    suite.addTest("Constant Optimization", []() {
        compileAndVerify("var a = 0; var b = 1; var c = -1; var d = 42;");
    });

    suite.addTest("Function Definition", []() {
        compileAndVerify("function add(a, b) { return a + b; }");
    });

    suite.addTest("Function Call", []() {
        compileAndVerify("function square(n) { return n * n; } var result = square(5);");
    });

    suite.addTest("If Statement", []() {
        compileAndVerify("var x = 10; if (x > 5) { var y = 20; } else { var z = 30; }");
    });

    suite.addTest("While Loop", []() {
        compileAndVerify("var i = 0; while (i < 5) { i = i + 1; }");
    });

    suite.addTest("For Loop", []() {
        compileAndVerify("for (var i = 0; i < 3; i = i + 1) { var x = i * 2; }");
    });

    suite.addTest("Array Literal", []() {
        compileAndVerify("var numbers = [1, 2, 3, 4, 5];");
    });

    suite.addTest("Object Literal", []() {
        compileAndVerify("var person = {name: \"Alice\", age: 30};");
    });

    suite.addTest("Binary Operations", []() {
        compileAndVerify("var a = 10 + 5; var b = 20 - 3; var c = 4 * 7; var d = 15 / 3;");
    });

    suite.addTest("Comparison", []() {
        compileAndVerify("var x = 10; var y = 20; var less = x < y; var equal = x == y;");
    });

    suite.addTest("Logical Operators", []() {
        compileAndVerify("var a = true; var b = false; var and = a && b; var or = a || b;");
    });

    suite.addTest("Unary Operators", []() {
        compileAndVerify("var x = 10; var neg = -x; var not = !true;");
    });

    suite.addTest("Property Access", []() {
        compileAndVerify("var obj = {x: 10}; var val = obj.x;");
    });

    suite.addTest("Ternary Operator", []() {
        compileAndVerify("var x = 10; var result = x > 5 ? 100 : 200;");
    });

    suite.addTest("Complex Expression", []() {
        compileAndVerify("var x = 10; var y = 20; var result = (x + y) * 2 - 5;");
    });

    suite.addTest("Multiple Statements", []() {
        compileAndVerify("var a = 1; var b = 2; var c = 3; var sum = a + b + c;");
    });

    suite.addTest("Nested Scopes", []() {
        compileAndVerify("var x = 10; { var y = 20; { var z = 30; } }");
    });
}

// Auto-initialize tests
static struct BytecodeTestInit {
    BytecodeTestInit() { init_bytecode_tests(); }
} bytecode_test_init;
