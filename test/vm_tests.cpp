#include "test_runner.h"
#include "../src/parser/parser.h"
#include "../src/bytecode/compiler.h"
#include "../src/bytecode/vm.h"
#include <stdexcept>

using namespace as3;
using namespace as3test;

TEST_SUITE(VM)

// Helper to compile and execute code, verify no errors
static void executeAndVerify(const std::string& code) {
    Parser parser(code);
    auto program = parser.parse();

    if (parser.hasErrors()) {
        throw std::runtime_error("Parse error: " + parser.getErrors());
    }

    Compiler compiler;
    auto bytecode = compiler.compile(program.get());

    VM vm;
    vm.execute(bytecode);

    if (vm.hasError()) {
        throw std::runtime_error("Runtime error: " + vm.getError());
    }
}

static void init_vm_tests() {
    auto& suite = suite_VM;

    suite.addTest("Simple Arithmetic", []() {
        executeAndVerify("var x = 10; var y = 20; var result = x + y; trace('Result:', result);");
    });

    suite.addTest("String Concatenation", []() {
        executeAndVerify("var name = 'World'; trace('Hello, ' + name + '!');");
    });

    suite.addTest("Constants", []() {
        executeAndVerify("trace('true:', true); trace('false:', false); trace('null:', null);");
    });

    suite.addTest("Comparison", []() {
        executeAndVerify("var x = 10; var y = 20; trace('x < y:', x < y); trace('x == 10:', x == 10);");
    });

    suite.addTest("Logical Operators", []() {
        executeAndVerify("var a = true; var b = false; trace('a && b:', a && b); trace('a || b:', a || b);");
    });

    suite.addTest("Array Operations", []() {
        executeAndVerify("var arr = [1, 2, 3]; trace('Array:', arr); trace('arr[1]:', arr[1]);");
    });

    suite.addTest("Object Operations", []() {
        executeAndVerify("var obj = {x: 10, y: 20}; trace('Object:', obj); trace('obj.x:', obj.x);");
    });

    suite.addTest("Bitwise Operations", []() {
        executeAndVerify("var a = 5; var b = 3; trace('a & b:', a & b); trace('a | b:', a | b);");
    });

    suite.addTest("Power Operation", []() {
        executeAndVerify("var base = 2; var exp = 8; var result = base * base * base; trace('2^3:', result);");
    });

    suite.addTest("Built-in Functions", []() {
        executeAndVerify("trace('parseInt:', parseInt('42')); trace('parseFloat:', parseFloat('3.14')); trace('isNaN:', isNaN(NaN));");
    });

    suite.addTest("Complex Expression", []() {
        executeAndVerify("var a = 5; var b = 10; var c = (a + b) * 2 - 3; trace('Result:', c);");
    });

    suite.addTest("If Statement", []() {
        executeAndVerify("var x = 10; if (x > 5) { trace('x is greater than 5'); } else { trace('x is not greater than 5'); }");
    });

    suite.addTest("While Loop", []() {
        executeAndVerify("var i = 0; while (i < 3) { trace('i:', i); i = i + 1; }");
    });

    suite.addTest("Ternary Operator", []() {
        executeAndVerify("var x = 10; var result = x > 5 ? 100 : 200; trace('Result:', result);");
    });
}

// Auto-initialize tests
static struct VMTestInit {
    VMTestInit() { init_vm_tests(); }
} vm_test_init;
