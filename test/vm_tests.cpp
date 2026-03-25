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

    suite.addTest("Function Call - Simple", []() {
        executeAndVerify("function add(a, b) { return a + b; } var result = add(10, 20); trace('Result:', result);");
    });

    suite.addTest("Function Call - No Args", []() {
        executeAndVerify("function getFortyTwo() { return 42; } trace('Answer:', getFortyTwo());");
    });

    suite.addTest("Function Call - Multiple", []() {
        executeAndVerify("function square(n) { return n * n; } trace('3^2:', square(3)); trace('4^2:', square(4));");
    });

    suite.addTest("Function Call - Nested", []() {
        executeAndVerify("function double(n) { return n * 2; } function quad(n) { return double(double(n)); } trace('Quad 5:', quad(5));");
    });

    suite.addTest("Method Call - Basic", []() {
        executeAndVerify("var obj = { value: 42, getValue: function() { return this.value; } }; trace('Value:', obj.getValue());");
    });

    suite.addTest("Method Call - String Property", []() {
        executeAndVerify("var obj = { name: 'Test', getName: function() { return this.name; } }; trace('Name:', obj.getName());");
    });

    suite.addTest("Method Call - With Parameters", []() {
        executeAndVerify("var obj = { x: 10, add: function(a, b) { return a + b + this.x; } }; trace('Sum:', obj.add(5, 3));");
    });

    suite.addTest("Method Call - Multiple Methods", []() {
        executeAndVerify("var obj = { val: 5, double: function() { return this.val * 2; }, triple: function() { return this.val * 3; } }; trace('Double:', obj.double()); trace('Triple:', obj.triple());");
    });

    suite.addTest("Method Call - Nested Object", []() {
        executeAndVerify("var outer = { inner: { data: 42, getData: function() { return this.data; } } }; trace('Data:', outer.inner.getData());");
    });

    suite.addTest("Method Call - this.property Access", []() {
        executeAndVerify("var person = { firstName: 'John', lastName: 'Doe', fullName: function() { return this.firstName; } }; trace('Name:', person.fullName());");
    });

    suite.addTest("Class - Basic Definition", []() {
        executeAndVerify("class MyClass { function test() { return 42; } } trace('Class defined');");
    });

    suite.addTest("Class - With Multiple Methods", []() {
        executeAndVerify("class Calculator { function add(a, b) { return a + b; } function multiply(a, b) { return a * b; } } trace('Calculator class defined');");
    });

    suite.addTest("Class - Instance Creation", []() {
        executeAndVerify("class Point { function Point(x, y) { } } var p = new Point(10, 20); trace('Instance created');");
    });

    suite.addTest("Class - Constructor with this", []() {
        executeAndVerify("class Counter { function Counter(start) { this.value = start; } } var c = new Counter(5); trace('Value:', c.value);");
    });

    suite.addTest("Class - Instance Method Call", []() {
        executeAndVerify("class Calculator { function Calculator() { this.result = 0; } function add(x) { this.result = this.result + x; } } var calc = new Calculator(); calc.add(10); trace('Result:', calc.result);");
    });

    suite.addTest("Class - Method with Return", []() {
        executeAndVerify("class Math { function Math() { } function double(x) { return x * 2; } } var m = new Math(); trace('Double 21:', m.double(21));");
    });

    suite.addTest("Class - Inheritance Basic", []() {
        executeAndVerify("class Animal { function speak() { return 'sound'; } } class Dog extends Animal { function bark() { return 'woof'; } } trace('Inheritance works');");
    });

    suite.addTest("Class - Inheritance Instance", []() {
        executeAndVerify("class Animal { function Animal() { } function speak() { return 'sound'; } } class Dog extends Animal { function Dog() { } } var d = new Dog(); trace('Speak:', d.speak());");
    });
}

// Auto-initialize tests
static struct VMTestInit {
    VMTestInit() { init_vm_tests(); }
} vm_test_init;
