#include "test_runner.h"
#include "../src/parser/parser.h"
#include "../src/executor/executor.h"
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace as3;
using namespace as3test;

TEST_SUITE(Interpreter)

// Helper to execute code and verify no errors
static void executeAndVerify(const std::string& code) {
    Parser parser(code);
    auto program = parser.parse();

    if (parser.hasErrors()) {
        throw std::runtime_error("Parse error: " + parser.getErrors());
    }

    Executor executor;
    executor.execute(program.get());
}

static void init_interpreter_tests() {
    auto& suite = suite_Interpreter;

    suite.addTest("Arithmetic", []() {
        executeAndVerify("var x = 10; var y = 20; trace(\"x + y =\", x + y); trace(\"x * y =\", x * y);");
    });

    suite.addTest("Variables", []() {
        executeAndVerify("var name = \"Alice\"; var age = 30; trace(\"Name:\", name); trace(\"Age:\", age);");
    });

    suite.addTest("Simple Function", []() {
        executeAndVerify("function add(a, b) { return a + b; } trace(\"5 + 3 =\", add(5, 3));");
    });

    suite.addTest("For Loop", []() {
        executeAndVerify("for (var i = 1; i <= 5; i++) { trace(\"Count:\", i); }");
    });

    suite.addTest("Arrays", []() {
        executeAndVerify("var numbers = [1, 2, 3, 4, 5]; trace(\"Array:\", numbers); trace(\"Length:\", numbers.length); trace(\"First:\", numbers[0]);");
    });

    suite.addTest("Objects", []() {
        executeAndVerify("var person = {name: \"Bob\", age: 25}; trace(\"Name:\", person.name); trace(\"Age:\", person.age);");
    });

    suite.addTest("If/Else", []() {
        executeAndVerify("var score = 85; if (score >= 90) { trace(\"Grade: A\"); } else if (score >= 80) { trace(\"Grade: B\"); } else { trace(\"Grade: C\"); }");
    });

    suite.addTest("While Loop", []() {
        executeAndVerify("var i = 1; while (i <= 3) { trace(\"i =\", i); i = i + 1; }");
    });

    suite.addTest("Fibonacci", []() {
        executeAndVerify("function fib(n) { if (n <= 1) return n; return fib(n - 1) + fib(n - 2); } trace(\"fib(10) =\", fib(10));");
    });

    suite.addTest("Strings", []() {
        executeAndVerify("var first = \"Hello\"; var second = \"World\"; var result = first + \" \" + second; trace(result);");
    });

    suite.addTest("Booleans", []() {
        executeAndVerify("var a = true; var b = false; trace(\"a && b =\", a && b); trace(\"a || b =\", a || b); trace(\"!a =\", !a);");
    });

    suite.addTest("Comparison", []() {
        executeAndVerify("var x = 10; var y = 20; trace(\"x < y =\", x < y); trace(\"x == y =\", x == y); trace(\"x != y =\", x != y);");
    });
}

// Auto-initialize tests
static struct InterpreterTestInit {
    InterpreterTestInit() { init_interpreter_tests(); }
} interpreter_test_init;
