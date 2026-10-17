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

    suite.addTest("Class - Inheritance Method Override", []() {
        executeAndVerify("class Animal { function speak() { return 'sound'; } } class Dog extends Animal { function speak() { return 'bark'; } } var d = new Dog(); trace('Speak:', d.speak());");
    });

    suite.addTest("Class - Inheritance Multiple Levels", []() {
        executeAndVerify("class Animal { function Animal() { } function breathe() { return 'breathing'; } } class Mammal extends Animal { function Mammal() { } function walk() { return 'walking'; } } class Dog extends Mammal { function Dog() { } function bark() { return 'barking'; } } var d = new Dog(); trace('Breathe:', d.breathe()); trace('Walk:', d.walk()); trace('Bark:', d.bark());");
    });

    suite.addTest("Class - Inheritance with Constructor", []() {
        executeAndVerify("class Animal { function Animal(name) { this.name = name; } } class Dog extends Animal { function Dog(name) { } } var d = new Dog('Rex'); trace('Name:', d.name);");
    });

    suite.addTest("Class - Extends Extern Class", []() {
        VM vm;
        vm.defineExternClass("Shape", {
            {"getType", [](const std::vector<ValuePtr>& args) {
                return Value::makeString("shape");
            }}
        });

        Parser parser("class Circle extends Shape { function Circle() { } function getRadius() { return 5; } } var c = new Circle(); trace('Type:', c.getType()); trace('Radius:', c.getRadius());");
        auto program = parser.parse();
        Compiler compiler;
        auto bytecode = compiler.compile(program.get());
        vm.execute(bytecode);

        if (vm.hasError()) {
            throw std::runtime_error("Runtime error: " + vm.getError());
        }
    });

    suite.addTest("registerBuiltin - Custom Function", []() {
        VM vm;
        vm.registerBuiltin("double", [](const std::vector<ValuePtr>& args) {
            if (args.empty()) return Value::makeNumber(0);
            return Value::makeNumber(args[0]->toNumber() * 2);
        });

        Parser parser("trace('Result:', double(21));");
        auto program = parser.parse();
        Compiler compiler;
        auto bytecode = compiler.compile(program.get());
        vm.execute(bytecode);

        if (vm.hasError()) {
            throw std::runtime_error("Runtime error: " + vm.getError());
        }
    });

    suite.addTest("registerBuiltin - Multiple Arguments", []() {
        VM vm;
        vm.registerBuiltin("add", [](const std::vector<ValuePtr>& args) {
            double sum = 0;
            for (const auto& arg : args) {
                sum += arg->toNumber();
            }
            return Value::makeNumber(sum);
        });

        Parser parser("trace('Sum:', add(1, 2, 3, 4, 5));");
        auto program = parser.parse();
        Compiler compiler;
        auto bytecode = compiler.compile(program.get());
        vm.execute(bytecode);

        if (vm.hasError()) {
            throw std::runtime_error("Runtime error: " + vm.getError());
        }
    });

    suite.addTest("defineExternClass - Basic", []() {
        VM vm;
        vm.defineExternClass("Math", {
            {"abs", [](const std::vector<ValuePtr>& args) {
                return Value::makeNumber(std::abs(args[0]->toNumber()));
            }}
        });

        Parser parser("var m = new Math(); trace('Abs:', m.abs(-5));");
        auto program = parser.parse();
        Compiler compiler;
        auto bytecode = compiler.compile(program.get());
        vm.execute(bytecode);

        if (vm.hasError()) {
            throw std::runtime_error("Runtime error: " + vm.getError());
        }
    });

    suite.addTest("defineExternClass - Multiple Methods", []() {
        VM vm;
        vm.defineExternClass("Calculator", {
            {"add", [](const std::vector<ValuePtr>& args) {
                return Value::makeNumber(args[0]->toNumber() + args[1]->toNumber());
            }},
            {"multiply", [](const std::vector<ValuePtr>& args) {
                return Value::makeNumber(args[0]->toNumber() * args[1]->toNumber());
            }}
        });

        Parser parser("var calc = new Calculator(); trace('Add:', calc.add(3, 4)); trace('Mul:', calc.multiply(3, 4));");
        auto program = parser.parse();
        Compiler compiler;
        auto bytecode = compiler.compile(program.get());
        vm.execute(bytecode);

        if (vm.hasError()) {
            throw std::runtime_error("Runtime error: " + vm.getError());
        }
    });

    suite.addTest("registerObject - Instance with Class Type", []() {
        VM vm;
        vm.defineExternClass("Shape", {
            {"getType", [](const std::vector<ValuePtr>& args) {
                return Value::makeString("circle");
            }}
        });

        vm.registerObject("hair01", "Shape");

        Parser parser("trace('Type:', hair01.getType());");
        auto program = parser.parse();
        Compiler compiler;
        auto bytecode = compiler.compile(program.get());
        vm.execute(bytecode);

        if (vm.hasError()) {
            throw std::runtime_error("Runtime error: " + vm.getError());
        }
    });

    suite.addTest("Hierarchical VM - Inherit Builtin", []() {
        VM parentVM;
        parentVM.registerBuiltin("parentFunc", [](const std::vector<ValuePtr>& args) {
            return Value::makeString("from parent");
        });

        VM childVM(&parentVM);

        Parser parser("trace('Result:', parentFunc());");
        auto program = parser.parse();
        Compiler compiler;
        auto bytecode = compiler.compile(program.get());
        childVM.execute(bytecode);

        if (childVM.hasError()) {
            throw std::runtime_error("Runtime error: " + childVM.getError());
        }
    });

    suite.addTest("Hierarchical VM - Override Builtin", []() {
        VM parentVM;
        parentVM.registerBuiltin("myFunc", [](const std::vector<ValuePtr>& args) {
            return Value::makeString("from parent");
        });

        VM childVM(&parentVM);
        childVM.registerBuiltin("myFunc", [](const std::vector<ValuePtr>& args) {
            return Value::makeString("from child");
        });

        Parser parser("trace('Result:', myFunc());");
        auto program = parser.parse();
        Compiler compiler;
        auto bytecode = compiler.compile(program.get());
        childVM.execute(bytecode);

        if (childVM.hasError()) {
            throw std::runtime_error("Runtime error: " + childVM.getError());
        }
    });

    suite.addTest("Hierarchical VM - Parent Unaffected", []() {
        VM parentVM;
        parentVM.registerBuiltin("myFunc", [](const std::vector<ValuePtr>& args) {
            return Value::makeString("parent");
        });

        VM childVM(&parentVM);
        childVM.registerBuiltin("myFunc", [](const std::vector<ValuePtr>& args) {
            return Value::makeString("child");
        });

        Parser parserParent("trace('Parent:', myFunc());");
        auto program = parserParent.parse();
        Compiler compiler;
        auto bytecode = compiler.compile(program.get());
        parentVM.execute(bytecode);

        Parser parserChild("trace('Child:', myFunc());");
        auto programChild = parserChild.parse();
        auto bytecodeChild = compiler.compile(programChild.get());
        childVM.execute(bytecodeChild);

        if (parentVM.hasError() || childVM.hasError()) {
            throw std::runtime_error("Runtime error");
        }
    });

    suite.addTest("Hierarchical VM - Context Inheritance", []() {
        VM parentVM;
        parentVM.registerBuiltin("getBaseValue", [](const std::vector<ValuePtr>& args) {
            return Value::makeNumber(100);
        });

        parentVM.defineExternClass("ParentClass", {
            {"getName", [](const std::vector<ValuePtr>& args) {
                return Value::makeString("ParentClass");
            }},
            {"getValue", [](const std::vector<ValuePtr>& args) {
                return Value::makeNumber(50);
            }}
        });

        parentVM.registerObject("sharedObj", "ParentClass");

        VM childVM(&parentVM);
        childVM.registerBuiltin("getChildValue", [](const std::vector<ValuePtr>& args) {
            return Value::makeNumber(200);
        });

        childVM.defineExternClass("ChildClass", {
            {"getChildName", [](const std::vector<ValuePtr>& args) {
                return Value::makeString("ChildClass");
            }}
        });

        Parser parser("trace('Base:', getBaseValue()); trace('Child:', getChildValue()); var obj = new ParentClass(); trace('Name:', obj.getName()); trace('Value:', obj.getValue()); trace('Shared:', sharedObj.getName());");
        auto program = parser.parse();
        Compiler compiler;
        auto bytecode = compiler.compile(program.get());
        childVM.execute(bytecode);

        if (childVM.hasError()) {
            throw std::runtime_error("Runtime error: " + childVM.getError());
        }
    });

    // The inline caches are in the code: a VM running it after another
    // finds its own globals and classes, not what the first one found.
    suite.addTest("Compiled code run on two VMs", []() {
        Parser parser("class Counter { public var n:int = 0; public function bump():int { return ++n; } }\n"
                      "function total():Number { var c:Counter = new Counter(); c.bump(); return c.bump() + base; }\n"
                      "var result = total();\n");
        auto program = parser.parse();
        Compiler compiler;
        auto bytecode = compiler.compile(program.get());
        for (int i = 0; i < 2; ++i) {
            VM vm;
            vm.setGlobal("base", Value::makeNumber(i * 100));
            vm.execute(bytecode);
            if (vm.hasError()) {
                throw std::runtime_error("Runtime error: " + vm.getError());
            }
            const double result = Val(vm.getGlobal("result")).toNumber();
            if (result != 2 + i * 100) {
                throw std::runtime_error("VM " + std::to_string(i) + " got " + numberToString(result));
            }
        }
    });

    // Dynamic properties: found, replaced and deleted among many, and
    // visited by for-in in the order of their names.
    suite.addTest("Many dynamic properties", []() {
        Parser parser("var o = {};\n"
                      "var names = ['m', 'b', 'z', 'a', 'q', 'c', 'x', 'd', 'k', 'e', 'y', 'f'];\n"
                      "for (var i = 0; i < names.length; i++) o[names[i]] = i;\n"
                      "delete o.q; o.b = 100;\n"
                      "var keys = []; for (var k in o) keys.push(k);\n"
                      "var result = keys.join(',') + '|' + o.b + '|' + o.y + '|' + ('q' in o) + '|' + o.m;\n");
        auto program = parser.parse();
        Compiler compiler;
        VM vm;
        vm.execute(compiler.compile(program.get()));
        if (vm.hasError()) {
            throw std::runtime_error("Runtime error: " + vm.getError());
        }
        const std::string result = Val(vm.getGlobal("result")).toString();
        if (result != "a,b,c,d,e,f,k,m,x,y,z|100|10|false|0") {
            throw std::runtime_error("got " + result);
        }
    });

    // What the compiler runs as one instruction (Chunk::fuse): setters
    // assigned as statements, a string stored in an int, comparisons of
    // what are not numbers jumping.
    suite.addTest("Fused instructions", []() {
        Parser parser("class Box {\n"
                      "  private var _v:int = 0; public var log:String = '';\n"
                      "  public function set v(x:int):void { _v = x; log += 's' + x; }\n"
                      "  public function get v():int { return _v; }\n"
                      "  public function run():String { v = 3; this.v = 4; return log; }\n"
                      "}\n"
                      "function f():String {\n"
                      "  var b:Box = new Box(); b.v = 5; b.v = 6;\n"
                      "  var n:int = '7';\n"
                      "  var count:int = 0;\n"
                      "  for (var i:int = 0; i < 3; i++) {\n"
                      "    if ('a' < 'b') count++;\n"
                      "    if (null == undefined) count++;\n"
                      "    if ('1' == 1) count++;\n"
                      "    if ('1' === 1) count--;\n"
                      "    if (NaN < 1) count += 100;\n"
                      "    if ('b' >= 'a') count++;\n"
                      "  }\n"
                      "  return b.log + b.run() + '|' + n + '|' + count + '|' + b.v;\n"
                      "}\n"
                      "var result = f();\n");
        auto program = parser.parse();
        Compiler compiler;
        VM vm;
        vm.execute(compiler.compile(program.get()));
        if (vm.hasError()) {
            throw std::runtime_error("Runtime error: " + vm.getError());
        }
        const std::string result = Val(vm.getGlobal("result")).toString();
        if (result != "s5s6s5s6s3s4|7|12|4") {
            throw std::runtime_error("got " + result);
        }
    });

    // Strings' members found through the caches (a string value's units
    // kept with it), at a site that sees strings and arrays in turn;
    // whole numbers printed; a numeric sort.
    suite.addTest("String members through the caches", []() {
        Parser parser("function f():String {\n"
                      "  var s:String = 'h\\u00e9llo w\\u00f6rld';\n"
                      "  var out:String = '';\n"
                      "  for (var i:int = 0; i < s.length; i++) out += s.charAt(i) + ':' + s.charCodeAt(i) + ',';\n"
                      "  var things:Array = ['abc', [1, 2, 3, 4], 'd\\u00e9f'];\n"
                      "  var lens:String = '';\n"
                      "  for (var k:int = 0; k < things.length; k++) lens += things[k].length + ';';\n"
                      "  var n:Number = 12; var b:Boolean = true;\n"
                      "  return out + '|' + lens + '|' + n.toString() + b.toString() + (12345).toString() +\n"
                      "    (-7).toString() + (9007199254740991).toString() + '|' + 'xyz'.charAt(1) +\n"
                      "    [3, 1, 2].sort(Array.NUMERIC).join('') + [3, 1, 2].sort(Array.DESCENDING | Array.NUMERIC).join('');\n"
                      "}\n"
                      "var result = f();\n");
        auto program = parser.parse();
        Compiler compiler;
        VM vm;
        vm.execute(compiler.compile(program.get()));
        if (vm.hasError()) {
            throw std::runtime_error("Runtime error: " + vm.getError());
        }
        const std::string result = Val(vm.getGlobal("result")).toString();
        const std::string expected = "h:104,\xc3\xa9:233,l:108,l:108,o:111, :32,w:119,\xc3\xb6:246,r:114,l:108,d:100,"
                                     "|3;4;3;|12true12345-79007199254740991|y123321";
        if (result != expected) {
            throw std::runtime_error("got " + result);
        }
    });

    // A global read in a loop sees each new value given it, here or by a
    // parent VM.
    suite.addTest("Globals read after they change", []() {
        VM parentVM;
        parentVM.setGlobal("step", Value::makeNumber(1));
        VM childVM(&parentVM);
        childVM.registerBuiltin("bumpStep", [&parentVM](const std::vector<ValuePtr>&) {
            parentVM.setGlobal("step", Value::makeNumber(10));
            return Value::makeUndefined();
        });
        Parser parser("var sum = 0;\n"
                      "for (var i = 0; i < 4; i++) { sum = sum + step; if (i == 1) bumpStep(); }\n");
        auto program = parser.parse();
        Compiler compiler;
        childVM.execute(compiler.compile(program.get()));
        if (childVM.hasError()) {
            throw std::runtime_error("Runtime error: " + childVM.getError());
        }
        const double sum = Val(childVM.getGlobal("sum")).toNumber();
        if (sum != 22) {
            throw std::runtime_error("sum is " + numberToString(sum));
        }
    });
}

// Auto-initialize tests
static struct VMTestInit {
    VMTestInit() { init_vm_tests(); }
} vm_test_init;
