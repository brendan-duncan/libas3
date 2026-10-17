#include "test_runner.h"
#include "../src/parser/parser.h"
#include "../src/ast/ast_nodes.h"
#include <stdexcept>

using namespace as3;
using namespace as3test;

TEST_SUITE(Parser)

static void init_parser_tests() {
    auto& suite = suite_Parser;

    suite.addTest("Empty program", []() {
        Parser p("");
        auto prog = p.parse();
        if (prog == nullptr || !prog->statements.empty()) {
            throw std::runtime_error("Expected empty program");
        }
    });

    suite.addTest("Variable with type annotation", []() {
        Parser p("var x:int = 5;");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Variable without type annotation", []() {
        Parser p("var y = 10;");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Variable without initializer", []() {
        Parser p("var z:int;");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Multiple variables", []() {
        Parser p("var a:int, b:String, c;");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Const declaration", []() {
        Parser p("const MAX_VALUE:int = 100;");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Let declaration", []() {
        Parser p("let count:int = 0;");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Function declaration", []() {
        Parser p("function foo():void { }");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Function with parameters", []() {
        Parser p("function add(a:int, b:int):int { return a + b; }");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Function with untyped parameters", []() {
        Parser p("function greet(name) { }");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("If statement", []() {
        Parser p("if (x > 0) { }");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("If-else statement", []() {
        Parser p("if (x > 0) { } else { }");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("If-else-if chain", []() {
        Parser p("if (x > 0) { } else if (x < 0) { } else { }");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("While statement", []() {
        Parser p("while (true) { break; }");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Do-while statement", []() {
        Parser p("do { } while (true);");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("For statement", []() {
        Parser p("for (var i:int = 0; i < 10; i++) { }");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("For-in statement", []() {
        Parser p("for (var item in arr) { }");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("For-of statement", []() {
        Parser p("for each (var item in arr) { }");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Switch statement", []() {
        Parser p("switch (x) { case 1: break; default: break; }");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Return statement", []() {
        Parser p("function f():int { return 42; }");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Return without value", []() {
        Parser p("function f():void { return; }");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Break statement", []() {
        Parser p("while (true) { break; }");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Continue statement", []() {
        Parser p("while (true) { continue; }");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Try-catch statement", []() {
        Parser p("try { } catch (e:Error) { }");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Try-catch-finally statement", []() {
        Parser p("try { } catch (e:Error) { } finally { }");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Throw statement", []() {
        Parser p("throw new Error('msg');");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Class declaration", []() {
        Parser p("class Foo { }");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Class with extends", []() {
        Parser p("class Foo extends Bar { }");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Class with implements", []() {
        Parser p("class Foo implements IBar { }");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Class with constructor", []() {
        Parser p("class Foo { public function Foo() { } }");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Class with method", []() {
        Parser p("class Foo { public function bar():void { } }");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Block statement", []() {
        Parser p("{ var x:int = 1; var y:int = 2; }");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Empty statement", []() {
        Parser p(";");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Expression statement", []() {
        Parser p("x = 5;");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Function call", []() {
        Parser p("foo();");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Function call with args", []() {
        Parser p("foo(1, 2, 3);");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("New expression", []() {
        Parser p("new Foo();");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("New expression with args", []() {
        Parser p("new Foo(1, 2);");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Member expression", []() {
        Parser p("obj.property;");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Chained member", []() {
        Parser p("obj.foo.bar;");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Array literal", []() {
        Parser p("[1, 2, 3];");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Object literal", []() {
        Parser p("var o = {a:1, b:2};");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("This expression", []() {
        Parser p("function f():void { this.x = 1; }");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Binary expression", []() {
        Parser p("var x = 1 + 2;");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Comparison expression", []() {
        Parser p("var x = a > b;");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Logical expression", []() {
        Parser p("var x = a && b;");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Unary expression", []() {
        Parser p("var x = -5;");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Postfix expression", []() {
        Parser p("i++;");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Conditional expression", []() {
        Parser p("var x = a > 0 ? a : -a;");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Package declaration", []() {
        Parser p("package com.example { class Foo { } }");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Import statement", []() {
        Parser p("import com.example.Foo;");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("Namespace declaration", []() {
        Parser p("namespace ns = 'http://example.com';");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    suite.addTest("setProperty", []() {
        Parser p("setProperty(\"hair_01\", _visible, \"1\");");
        auto prog = p.parse();
        if (prog == nullptr || prog->statements.empty() || p.hasErrors()) {
            throw std::runtime_error("Parse failed");
        }
    });

    // What ActionScript 3 programs are written with.
    const auto accepts = [](const char* code) {
        Parser p(code);
        p.parse();
        if (p.hasErrors()) {
            throw std::runtime_error(std::string(code) + "\n" + p.getErrors());
        }
        if (!p.getWarnings().empty()) {
            throw std::runtime_error(std::string(code) + "\n" + p.getWarnings());
        }
    };

    suite.addTest("Words that are keywords only in places", [accepts]() {
        accepts("var get = 1; var set = 2; var each = 3; var namespace = 4; var dynamic = 5; var of = 6;");
        accepts("class A { public function get(i:int):int { return i; } public function set(i:int):void {} }");
        accepts("var o = {default: 1, class: 2, function: 3}; o.default = o['class'] + o.function; o.delete = 1;");
        accepts("class B { public function get each():int { return 1; } static function static():void {} }");
    });

    suite.addTest("Type annotations", [accepts]() {
        accepts("var a:*; var b:Vector.<int>; var c:Vector.<Vector.<Number>> = new Vector.<Vector.<Number>>(); "
                "var d:flash.display.Sprite; function f(x:*, ...rest):void {} var e:Vector.<Vector.<Vector.<int>>>;");
        accepts("var v = new <int>[1, 2, 3]; var w:Vector.<String> = Vector.<String>(['a']); if (v is Vector.<int>) {}");
    });

    suite.addTest("Regular expressions and division", [accepts]() {
        accepts("var r = /a\\/b[/]c/gi; var d = 4 / 2 / 1; var e = (d) / 2; x = a[1] / 2; y = s.replace(/x/g, '');");
        accepts("if (/^\\d+$/.test(s)) {} var q = r ? /a/ : /b/; z /= 2; f(/=/);");
    });

    suite.addTest("Metadata, attributes and namespaces", [accepts]() {
        accepts("package p {\n[SWF(width=\"320\", height=\"240\")]\n[Event(name=\"change\", type=\"flash.events.Event\")]\n"
                "public dynamic class C {\n  [Embed(source=\"a.png\")] public static var A:Class;\n  [Bindable]\n"
                "  public var b:int;\n  mx_internal function hidden():void {}\n  mx_internal\n  override protected function f():void {}\n"
                "  use namespace mx_internal;\n}\n}");
        accepts("interface I extends J, K { function f(a:int):void; function get g():int; function set g(v:int):void }");
    });

    suite.addTest("Statements", [accepts]() {
        accepts("outer: for (var i:int = 0, n:int = 2; i < n; i++, n--) { inner: while (true) { continue outer; } }");
        accepts("for each (var x:Object in list) {} for (var k:String in o) {} for (k in o) {} for each (x in o) {}");
        accepts("switch (x) { case 1: case 'a': break; default: }  try {} catch (e:TypeError) {} catch (e) {} finally {}");
        accepts("do x++; while (x < 5) var y = 1");
        accepts("var a = b, c = (d, e); a = (b ? c : d) ? e : f; delete o.x; delete o[k]; void 0; typeof x;");
        accepts("super(1); super.f(); super.x = 2; x = super.y;");
    });

    suite.addTest("Expressions read in order", []() {
        // a - b - c, a ? b : c ? d : e, ++ after a line break.
        Parser p("x = a - b - c;\ny = a ? b : c ? d : e;\nz = a\n++b");
        auto prog = p.parse();
        if (p.hasErrors() || prog->statements.size() != 4) {
            throw std::runtime_error("statements: " + std::to_string(prog->statements.size()) + "\n" + p.getErrors());
        }
    });

    suite.addTest("Numbers and strings", [accepts]() {
        Parser p("var a = 0xFF000000; var b = 1e3; var c = .5; var d = 'a\\u00e9\\x41\\n'; var e = 1.5e-3;");
        auto prog = p.parse();
        if (p.hasErrors()) throw std::runtime_error(p.getErrors());
        accepts("\xEF\xBB\xBFvar withByteOrderMark = 1;");
    });
}

// Auto-initialize tests
static struct ParserTestInit {
    ParserTestInit() { init_parser_tests(); }
} parser_test_init;
