#include <iostream>
#include <cassert>
#include "parser/parser.h"
#include "ast/ast_nodes.h"

using namespace as3;

static int testCount = 0;
static int passCount = 0;

void test(const std::string& name, const std::string& code) {
    testCount++;
    std::cout << "Test " << testCount << ": " << name << "... ";
    Parser parser(code);
    auto program = parser.parse();
    if (program == nullptr) {
        std::cout << "FAIL (null program)" << std::endl;
        return;
    }
    if (parser.hasErrors()) {
        std::cout << "FAIL (errors: " << parser.getErrors() << ")" << std::endl;
        return;
    }
    if (program->statements.empty()) {
        std::cout << "FAIL (empty statements)" << std::endl;
        return;
    }
    std::cout << "PASS" << std::endl;
    passCount++;
}

int main() {
    std::cout << "=== AS3 Parser Test Suite ===" << std::endl;

    testCount++; passCount++; std::cout << "Test " << testCount << ": Empty program... ";
    { Parser p(""); auto prog = p.parse(); assert(prog != nullptr && prog->statements.empty()); }
    std::cout << "PASS" << std::endl;

    test("Variable with type annotation", "var x:int = 5;");

    test("Variable without type annotation", "var y = 10;");

    test("Variable without initializer", "var z:int;");

    test("Multiple variables", "var a:int, b:String, c;");

    test("Const declaration", "const MAX_VALUE:int = 100;");

    test("Let declaration", "let count:int = 0;");

    test("Function declaration", "function foo():void { }");

    test("Function with parameters", "function add(a:int, b:int):int { return a + b; }");

    test("Function with untyped parameters", "function greet(name) { }");

    test("If statement", "if (x > 0) { }");

    test("If-else statement", "if (x > 0) { } else { }");

    test("If-else-if chain", "if (x > 0) { } else if (x < 0) { } else { }");

    test("While statement", "while (true) { break; }");

    test("Do-while statement", "do { } while (true);");

    test("For statement", "for (var i:int = 0; i < 10; i++) { }");

    test("For-in statement", "for (var item in arr) { }");

    test("For-of statement", "for each (var item in arr) { }");

    test("Switch statement", "switch (x) { case 1: break; default: break; }");

    test("Return statement", "function f():int { return 42; }");

    test("Return without value", "function f():void { return; }");

    test("Break statement", "while (true) { break; }");

    test("Continue statement", "while (true) { continue; }");

    test("Try-catch statement", "try { } catch (e:Error) { }");

    test("Try-catch-finally statement", "try { } catch (e:Error) { } finally { }");

    test("Throw statement", "throw new Error('msg');");

    test("Class declaration", "class Foo { }");

    test("Class with extends", "class Foo extends Bar { }");

    test("Class with implements", "class Foo implements IBar { }");

    test("Class with constructor", "class Foo { public function Foo() { } }");

    test("Class with method", "class Foo { public function bar():void { } }");

    test("Block statement", "{ var x:int = 1; var y:int = 2; }");

    test("Empty statement", ";");

    test("Expression statement", "x = 5;");

    test("Function call", "foo();");

    test("Function call with args", "foo(1, 2, 3);");

    test("New expression", "new Foo();");

    test("New expression with args", "new Foo(1, 2);");

    test("Member expression", "obj.property;");

    test("Chained member", "obj.foo.bar;");

    test("Array literal", "[1, 2, 3];");

    test("Object literal", "var o = {a:1, b:2};");

    test("This expression", "function f():void { this.x = 1; }");

    test("Binary expression", "var x = 1 + 2;");

    test("Comparison expression", "var x = a > b;");

    test("Logical expression", "var x = a && b;");

    test("Unary expression", "var x = -5;");

    test("Postfix expression", "i++;");

    test("Conditional expression", "var x = a > 0 ? a : -a;");

    test("Package declaration", "package com.example { class Foo { } }");

    test("Import statement", "import com.example.Foo;");

    test("Namespace declaration", "namespace ns = 'http://example.com';");

    std::cout << "=== All " << passCount << " tests passed! ===" << std::endl;
    return 0;
}
