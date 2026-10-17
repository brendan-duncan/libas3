#include "test_runner.h"
#include "../src/parser/parser.h"
#include "../src/bytecode/compiler.h"
#include "../src/bytecode/vm.h"
#include <stdexcept>

using namespace as3;
using namespace as3test;

// The language: classes, packages, statements, expressions, types.
TEST_SUITE(Language)

namespace {

// Runs code on a VM, gathering what it traces, one line a call.
std::string run(VM& vm, const std::string& code) {
    std::string out;
    vm.defineFunction("trace", [&out](VM& vm, const Val&, Args args) {
        for (size_t i = 0; i < args.size(); ++i) {
            out += (i ? " " : "") + vm.toString(args[i]);
        }
        out += "\n";
        return Val();
    });
    Parser parser(code);
    auto program = parser.parse();
    if (parser.hasErrors()) {
        throw std::runtime_error("Parse error: " + parser.getErrors());
    }
    Compiler compiler;
    vm.execute(compiler.compile(program.get()));
    if (vm.hasError()) {
        throw std::runtime_error("Runtime error: " + vm.getError() + " (line " + std::to_string(vm.getErrorLine()) +
                                 ")\n" + vm.getErrorStack() + "\nOutput so far:\n" + out);
    }
    return out;
}

std::string run(const std::string& code) {
    VM vm;
    return run(vm, code);
}

void expect(const std::string& got, const std::string& wanted) {
    if (got != wanted) {
        throw std::runtime_error("got \"" + got + "\", wanted \"" + wanted + "\"");
    }
}

} // namespace

static void init_language_tests() {
    auto& suite = suite_Language;

    // --- Classes ---

    suite.addTest("super() runs the base constructor, implicit too", []() {
        expect(run("class A { public var log:String = 'a'; public function A(x:int = 1) { log += x; } }\n"
                   "class B extends A { public function B() { super(7); log += 'b'; } }\n"
                   "class C extends A { public function C() { log += 'c'; } }\n"
                   "class D extends A { }\n"
                   "trace(new B().log, new C().log, new D().log);"),
               "a7b a1c a1\n");
    });

    suite.addTest("super.method() and super.prop", []() {
        expect(run("class A { public function name():String { return 'A'; } public function get size():int { return 1; } }\n"
                   "class B extends A {\n"
                   "  override public function name():String { return 'B<' + super.name() + '>'; }\n"
                   "  override public function get size():int { return super.size + 10; }\n"
                   "}\n"
                   "var b:A = new B(); trace(b.name(), b.size);"),
               "B<A> 11\n");
    });

    suite.addTest("Field initializers run for each instance", []() {
        expect(run("class P { public var list:Array = []; public var n:int = 3; }\n"
                   "var a = new P(); var b = new P(); a.list.push(1); a.n = 5;\n"
                   "trace(a.list.length, b.list.length, a.n, b.n);"),
               "1 0 5 3\n");
    });

    suite.addTest("Static members: on the class, unqualified inside it", []() {
        expect(run("class K {\n"
                   "  public static const MAX:int = 10;\n"
                   "  public static var count:int = 0;\n"
                   "  private static var _secret:String = 's';\n"
                   "  public static function make():K { count++; return new K(); }\n"
                   "  public static function get secret():String { return _secret + MAX; }\n"
                   "  public function K() { count += 100; }\n"
                   "  public function where():int { return count; }\n"
                   "}\n"
                   "K.make(); var k = K.make();\n"
                   "trace(K.MAX, K.count, K.secret, k.where(), k.count);"),
               "10 202 s10 202 undefined\n");
    });

    suite.addTest("Getters and setters, inherited and overridden", []() {
        expect(run("class Box {\n"
                   "  private var _w:Number = 0;\n"
                   "  public function get width():Number { return _w; }\n"
                   "  public function set width(v:Number):void { _w = v * 2; }\n"
                   "}\n"
                   "class Big extends Box {\n"
                   "  override public function set width(v:Number):void { super.width = v + 1; }\n"
                   "}\n"
                   "var b = new Box(); b.width = 3; var g = new Big(); g.width = 3; g.width += 1;\n"
                   "trace(b.width, g.width, (g.width = 5));"),
               "6 20 5\n");
    });

    suite.addTest("Methods are bound closures", []() {
        expect(run("class Counter {\n"
                   "  public var n:int = 0;\n"
                   "  public function hit():void { n++; }\n"
                   "  public function handler():Function { return hit; }\n"
                   "}\n"
                   "var c = new Counter(); var f = c.hit; f(); f(); var g = c.handler(); g();\n"
                   "trace(c.n, c.hit == c.hit, f === c.hit);"),
               "3 true true\n");
    });

    suite.addTest("Interfaces: is and as", []() {
        expect(run("interface IShape { function area():Number; }\n"
                   "interface ISolid extends IShape { function volume():Number; }\n"
                   "class Cube implements ISolid { public function area():Number { return 6; } public function volume():Number { return 1; } }\n"
                   "class Dot {}\n"
                   "var c = new Cube();\n"
                   "trace(c is IShape, c is ISolid, new Dot() is IShape, (c as IShape).area(), new Dot() as IShape);"),
               "true true false 6 null\n");
    });

    suite.addTest("Packages: qualified names and imports", []() {
        VM vm;
        run(vm, "package com.example.shapes { public class Circle { public var r:Number; public function Circle(r:Number) { this.r = r; } } }");
        run(vm, "package com.example.app {\n"
                "  import com.example.shapes.Circle;\n"
                "  public class Main { public static function go():Number { return new Circle(2).r; } }\n"
                "}");
        expect(run(vm, "import com.example.app.*;\n"
                       "import flash.utils.getQualifiedClassName;\n"
                       "trace(Main.go(), new com.example.shapes.Circle(3).r, getQualifiedClassName(new com.example.shapes.Circle(1)));"),
               "2 3 com.example.shapes::Circle\n");
    });

    suite.addTest("Same-package classes need no import", []() {
        VM vm;
        run(vm, "package a.b { public class Helper { public static function twice(x:int):int { return x * 2; } } }");
        run(vm, "package a.b { public class User { public static function go():int { return Helper.twice(21); } } }");
        expect(run(vm, "trace(a.b.User.go());"), "42\n");
    });

    suite.addTest("Class resolver loads classes when needed", []() {
        VM vm;
        std::vector<std::string> asked;
        vm.setClassResolver([&asked](const std::string& name) -> std::optional<ClassSource> {
            asked.push_back(name);
            if (name == "lib.Base") {
                return ClassSource{"package lib { public class Base { public function hello():String { return 'base'; } } }", "Base.as"};
            }
            if (name == "lib.Derived") {
                return ClassSource{"package lib { public class Derived extends Base { override public function hello():String { return 'derived+' + super.hello(); } } }", "Derived.as"};
            }
            return std::nullopt;
        });
        expect(run(vm, "import lib.Derived;\ntrace(new Derived().hello());"), "derived+base\n");
        bool askedBase = false;
        for (const auto& name : asked) askedBase = askedBase || name == "lib.Base";
        if (!askedBase) throw std::runtime_error("base not asked for");
        expect(run(vm, "import flash.utils.getDefinitionByName;\ntrace(new (getDefinitionByName('lib.Base'))().hello());"),
               "base\n");
    });

    // --- Statements ---

    suite.addTest("switch with fall-through, default, strings", []() {
        expect(run("function f(x):String { var s = '';\n"
                   "  switch (x) { case 1: s += 'one'; case 2: s += 'two'; break; case 'a': s += 'A'; break; default: s += 'def'; case 9: s += 'nine'; }\n"
                   "  return s; }\n"
                   "trace(f(1), f(2), f('a'), f(5), f(9));"),
               "onetwo two A defnine nine\n");
    });

    suite.addTest("for-in and for each", []() {
        expect(run("var o = {a: 1, b: 2}; var keys = []; var sum = 0;\n"
                   "for (var k:String in o) keys.push(k);\n"
                   "for each (var v:int in o) sum += v;\n"
                   "var arr = [10, 20]; var total = 0; for each (var x in arr) total += x;\n"
                   "var idx = ''; for (var i in arr) idx += i;\n"
                   "keys.sort(); trace(keys, sum, total, idx);"),
               "a,b 3 30 01\n");
    });

    suite.addTest("break and continue, labeled, in all loops", []() {
        expect(run("var s = '';\n"
                   "for (var i = 0; i < 5; i++) { if (i == 1) continue; if (i == 3) break; s += i; }\n"
                   "var j = 0; while (j < 5) { j++; if (j == 2) continue; if (j == 4) break; s += j; }\n"
                   "var k = 0; do { k++; if (k == 2) continue; s += k; } while (k < 3);\n"
                   "outer: for (var a = 0; a < 3; a++) { for (var b = 0; b < 3; b++) { if (b == 1) continue outer; if (a == 2) break outer; s += a + '' + b; } }\n"
                   "block: { s += 'x'; break block; s += 'y'; }\n"
                   "trace(s);"),
               "0213130010x\n");
    });

    suite.addTest("try, catch by type, finally, throw", []() {
        expect(run("function f(n):String { var s = '';\n"
                   "  try { if (n == 1) throw new TypeError('t'); if (n == 2) throw new RangeError('r'); if (n == 3) throw 'str'; s += 'ok'; }\n"
                   "  catch (e:TypeError) { s += 'type:' + e.message; }\n"
                   "  catch (e:Error) { s += 'error:' + e.name; }\n"
                   "  catch (e) { s += 'any:' + e; }\n"
                   "  finally { s += '!'; }\n"
                   "  return s; }\n"
                   "trace(f(0), f(1), f(2), f(3));"),
               "ok! type:t! error:RangeError! any:str!\n");
    });

    suite.addTest("finally runs on return, break and rethrow", []() {
        expect(run("var log = '';\n"
                   "function r():int { try { return 1; } finally { log += 'r'; } return 2; }\n"
                   "for (var i = 0; i < 3; i++) { try { if (i == 1) break; } finally { log += i; } }\n"
                   "try { try { throw new Error('x'); } finally { log += 'f'; } } catch (e:Error) { log += e.message; }\n"
                   "trace(r(), log);"),
               "1 01fxr\n");
    });

    suite.addTest("Runtime errors are catchable", []() {
        expect(run("var o = null; var s = '';\n"
                   "try { o.x = 1; } catch (e:TypeError) { s += e.errorID; }\n"
                   "try { missing(); } catch (e:ReferenceError) { s += ' ' + e.errorID; }\n"
                   "try { var n = 5; n(); } catch (e:TypeError) { s += ' ' + e.errorID; }\n"
                   "try { new Array(-1); } catch (e:RangeError) { s += ' ' + e.errorID; }\n"
                   "trace(s);"),
               "1009 1065 1006 1005\n");
    });

    suite.addTest("Closures capture locals and parameters", []() {
        expect(run("function counter(start:int):Function { var n = start; return function():int { return ++n; }; }\n"
                   "var c = counter(5); c(); \n"
                   "var fs = []; for (var i = 0; i < 3; i++) { fs.push(function() { return i; }); }\n"
                   "function outer() { var x = 1; function inner() { return x + 1; } x = 10; return inner(); }\n"
                   "function deep() { var a = 1; return function() { return function() { return a++; }; }; }\n"
                   "var d = deep()(); d();\n"
                   "trace(c(), fs[0](), outer(), d());"),
               "7 3 11 2\n");
    });

    suite.addTest("Nested functions in methods and if blocks", []() {
        expect(run("class M { private var base:int = 100;\n"
                   "  public function go(flag:Boolean):int {\n"
                   "    if (flag) { function add(x:int):int { return x + base; } }\n"
                   "    var f = function(y:int):int { return y + base + add(1); };\n"
                   "    return f(2);\n"
                   "  } }\n"
                   "trace(new M().go(true));"),
               "203\n");
    });

    suite.addTest("typeof, delete, instanceof, in, is, as, ??", []() {
        expect(run("var o = {a: 1}; delete o.a; var arr = [1];\n"
                   "trace(typeof 1, typeof 'x', typeof o, typeof undefined, typeof nothing, typeof function() {}, typeof null, typeof true);\n"
                   "trace('a' in o, 0 in arr, arr instanceof Array, 5 is int, 5.5 is int, 5 is Number, 'x' as String, 5 as String);\n"
                   "trace(null ?? 'd', 0 ?? 'd');"),
               "number string object undefined undefined function object boolean\n"
               "false true true true false true x null\n"
               "d 0\n");
    });

    suite.addTest("Default and rest parameters, arguments", []() {
        expect(run("function f(a:int, b:int = 10, ...rest):String { return a + ',' + b + ',' + rest.length + ',' + arguments.length; }\n"
                   "function g() { return arguments.length + ':' + arguments[1]; }\n"
                   "trace(f(1), f(1, 2), f(1, 2, 3, 4), g(5, 6, 7), f.length);"),
               "1,10,0,1 1,2,0,2 1,2,2,4 3:6 2\n");
    });

    suite.addTest("Function.call and apply", []() {
        expect(run("function who(x, y) { return this.name + x + y; }\n"
                   "var o = {name: 'o'};\n"
                   "trace(who.call(o, 1, 2), who.apply(o, [3, 4]));"),
               "o12 o34\n");
    });

    suite.addTest("Regular expression literals", []() {
        expect(run("var r = /(\\d+)-(\\d+)/g; var s = 'a 1-2 b 33-4';\n"
                   "trace(s.replace(r, '$2:$1'), /ab+c/i.test('xABBC'), 'a,b;c'.split(/[,;]/), s.match(r));"),
               "a 2:1 b 4:33 true a,b,c 1-2,33-4\n");
    });

    suite.addTest("Conditional, comma, void", []() {
        expect(run("var a = 1, b = 2; var c = (a++, b++, a + b); trace(c, a > 1 ? 'y' : 'n', void 0);"),
               "5 y undefined\n");
    });

    suite.addTest("Metadata, :*, :void, qualified and generic types", []() {
        expect(run("package {\n"
                   "  [Event(name=\"change\", type=\"flash.events.Event\")]\n"
                   "  public class T {\n"
                   "    [Embed(source=\"x.png\")] public static var Pic:Class;\n"
                   "    public var any:* = 3;\n"
                   "    public var list:Vector.<int> = new Vector.<int>();\n"
                   "    public var nested:Vector.<Vector.<Number>> = new Vector.<Vector.<Number>>();\n"
                   "    public function f(a:*, b:flash.utils.Dictionary = null):void { list.push(a); }\n"
                   "  }\n"
                   "}\n"
                   "var t = new T(); t.f(2.9); t.list.push(7.5); var v:Vector.<int> = new <int>[1, 2];\n"
                   "trace(t.any, t.list, v, v.length, t.nested.length);"),
               "3 2,7 1,2 2 0\n");
    });

    suite.addTest("Metadata is kept for the host", []() {
        VM vm;
        run(vm, "package art {\n"
                "  [Event(name=\"change\", type=\"flash.events.Event\")]\n"
                "  public class Assets {\n"
                "    [Embed(source=\"brush.png\", mimeType=\"image/png\")] public static var Brush:Class;\n"
                "    [Bindable] public var size:int;\n"
                "  }\n"
                "}\n");
        auto cls = vm.findClass("art.Assets");
        if (!cls || !cls->metadata || cls->metadata->at(0).name != "Event" ||
            cls->metadata->at(0).arguments.at(1).second != "flash.events.Event") {
            throw std::runtime_error("class metadata missing");
        }
        const Trait* brush = cls->findStatic("Brush");
        if (!brush || !brush->metadata || brush->metadata->at(0).name != "Embed" ||
            brush->metadata->at(0).arguments.at(0).first != "source" ||
            brush->metadata->at(0).arguments.at(0).second != "brush.png") {
            throw std::runtime_error("member metadata missing");
        }
        const Trait* size = cls->findTrait("size");
        if (!size || !size->metadata || size->metadata->at(0).name != "Bindable") {
            throw std::runtime_error("field metadata missing");
        }
    });

    suite.addTest("Conditional compilation constants", []() {
        Parser parser("CONFIG::debug { trace('debug'); }\n"
                      "CONFIG::release { trace('release'); }\n"
                      "class C {\n"
                      "  CONFIG::debug public function f():String { return 'debug f'; }\n"
                      "  CONFIG::release public function f():String { return 'release f'; }\n"
                      "}\n"
                      "trace(new C().f(), CONFIG::debug, CONFIG::level + 1, CONFIG::name, CONFIG::unknown);\n");
        parser.defineConstant("CONFIG::debug", "true");
        parser.defineConstant("CONFIG::release", "false");
        parser.defineConstant("CONFIG::level", "2");
        parser.defineConstant("CONFIG::name", "\"phoenix\"");
        auto program = parser.parse();
        if (parser.hasErrors()) throw std::runtime_error(parser.getErrors());
        VM vm;
        std::string out;
        vm.defineFunction("trace", [&out](VM& vm, const Val&, Args args) {
            for (size_t i = 0; i < args.size(); ++i) out += (i ? " " : "") + vm.toString(args[i]);
            out += "\n";
            return Val();
        });
        Compiler compiler;
        vm.execute(compiler.compile(program.get()));
        if (vm.hasError()) throw std::runtime_error(vm.getError());
        expect(out, "debug\ndebug f true 3 phoenix false\n");
    });

    suite.addTest("with: names on its object first", []() {
        expect(run("var o = {x: 1, f: function() { return this.x * 10; }}; var x = 'outer'; var y = 'outer y';\n"
                   "function g() { var x = 'local'; with (o) { x = 2; y = x + 1; trace(x, y, f()); x++; } return x; }\n"
                   "trace(g(), o.x, y);"),
               "2 3 20\nlocal 3 3\n");
    });

    suite.addTest("include reads a file", []() {
        VM vm;
        vm.setIncludeResolver([](const std::string& path, const std::string& from) -> std::optional<std::string> {
            if (path == "helpers.as" && from == "main.as") return std::string("function twice(x) { return x * 2; }\n");
            if (path == "members.as") return std::string("public var extra:int = 5;\n");
            return std::nullopt;
        });
        std::string out;
        vm.defineFunction("trace", [&out](VM& vm, const Val&, Args args) {
            out += vm.toString(args[0]) + "\n";
            return Val();
        });
        if (!vm.load("include \"helpers.as\";\nclass K { include \"members.as\"; }\ntrace(twice(new K().extra));", "main.as")) {
            throw std::runtime_error(vm.getError());
        }
        expect(out, "10\n");
        if (vm.load("include \"missing.as\";", "main.as") || vm.getError().find("missing.as") == std::string::npos) {
            throw std::runtime_error("a missing include is not an error: " + vm.getError());
        }
    });

    suite.addTest("Const locals and labeled blocks", []() {
        expect(run("function f():int { const K:int = 4; return K * 2; } trace(f());"), "8\n");
    });

    // --- Types and numbers ---

    suite.addTest("int and uint wrap where declared", []() {
        expect(run("var u:uint = 0xFF000000; var i:int = 0xFF000000; var n:Number = 0xFF000000;\n"
                   "var g:int = (7 / 17.0) * 255; var big:int = 2147483647; big++;\n"
                   "var m:uint = -1; var a = 0xFF000000 & 0xFF000000; var b:uint = 0xFF000000 & 0xFF000000;\n"
                   "trace(u, i, n, g, big, m, a, b, 1 / 2, int(3.9), int(-3.9), uint(-1), int('0x10'));"),
               "4278190080 -16777216 4278190080 105 -2147483648 4294967295 -16777216 4278190080 0.5 3 -3 4294967295 16\n");
    });

    suite.addTest("ARGB colors with shifts", []() {
        expect(run("function argb(a:uint, r:uint, g:uint, b:uint):uint { return (a << 24) | (r << 16) | (g << 8) | b; }\n"
                   "var c:uint = argb(255, 1, 2, 3);\n"
                   "var alpha:uint = (c >> 24) & 0xFF; var alpha2:uint = c >>> 24; var red:uint = (c >> 16) & 0xFF;\n"
                   "trace(c, alpha, alpha2, red, c >> 24, c >>> 24, (c & 0xFF000000) >>> 0, 1 << 31, (1 << 31) >>> 0);"),
               "4278256131 255 255 1 -1 255 4278190080 -2147483648 2147483648\n");
    });

    suite.addTest("Typed fields, parameters and returns coerce", []() {
        expect(run("class C { public var i:int; public var n:Number; public var s:String; public var b:Boolean; public var o:Object; public var x;\n"
                   "  public function half(v:int):int { return v / 2; } }\n"
                   "var c = new C(); trace(c.i, c.n, c.s, c.b, c.o, c.x);\n"
                   "c.i = 3.7; c.s = 5; c.b = 'yes'; trace(c.i, c.s, c.b, c.half(7.9));"),
               "0 NaN null false null undefined\n3 5 true 3\n");
    });

    suite.addTest("Uninitialized typed locals", []() {
        expect(run("function f():String { var i:int; var n:Number; var b:Boolean; var s:String; var a; i++; return i + ' ' + n + ' ' + b + ' ' + s + ' ' + a; } trace(f());"),
               "1 NaN false null undefined\n");
    });

    suite.addTest("Number formatting", []() {
        expect(run("trace(0.1 + 0.2, 1e21, 1e-7, 123456789012, -0, 1/3, 100, 2.5e-5, (255).toString(16), (3.14159).toFixed(2), (0.5).toFixed(0), (1234.5678).toPrecision(6));"),
               "0.30000000000000004 1e+21 1e-7 123456789012 0 0.3333333333333333 100 0.000025 ff 3.14 1 1234.57\n");
    });
}

// Auto-initialize tests
static struct LanguageTestInit {
    LanguageTestInit() { init_language_tests(); }
} language_test_init;
