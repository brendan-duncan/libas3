#include "test_runner.h"
#include "../src/parser/parser.h"
#include "../src/bytecode/compiler.h"
#include "../src/bytecode/vm.h"
#include <stdexcept>

using namespace as3;
using namespace as3test;

// Errors: compile errors reported with their file and line, runtime errors
// as ActionScript's, stack traces, and nothing that takes the host down.
TEST_SUITE(Errors)

namespace {

void expectContains(const std::string& text, const std::string& part) {
    if (text.find(part) == std::string::npos) {
        throw std::runtime_error("\"" + text + "\" does not have \"" + part + "\"");
    }
}

std::string compileError(const std::string& code, const std::string& file) {
    Parser parser(code, file);
    auto program = parser.parse();
    if (parser.hasErrors()) {
        return parser.getErrors();
    }
    try {
        Compiler compiler;
        compiler.compile(program.get());
    } catch (const CompileError& failed) {
        return std::string(failed.what());
    }
    return std::string();
}

} // namespace

static void init_error_tests() {
    auto& suite = suite_Errors;

    suite.addTest("Compile errors name the file and line", []() {
        expectContains(compileError("var a = 1;\nbreak;\n", "Frame.as"), "Frame.as:2: break outside a loop");
        expectContains(compileError("for (;;) {\n  continue nowhere;\n}\n", "Loop.as"), "Loop.as:2: continue to an unknown label");
        expectContains(compileError("function f() {\n  const K = 1;\n  K = 2;\n}\n", "Const.as"), "Const.as:3: Cannot assign to the constant 'K'");
        expectContains(compileError("var x = ;\n", "Bad.as"), "Parse error in Bad.as at line 1");
        expectContains(compileError("class A {\n  function f() {\n    x = = 2;\n  }\n}\n", "A.as"), "at line 3");
    });

    suite.addTest("Broken input never crashes", []() {
        const char* inputs[] = {
            "class", "class {", "package a.b {", "function (", "var x:Vector.<", "x = [1, 2", "{{{{", ")))",
            "if (x) else", "switch (x) { case }", "try {}", "new", "a.b.c.", "/unterminated", "'unterminated",
            "for each (x in) {}", "class A extends { }", "interface I { function f() }", "[Embed(", "@#$%^",
            "super.", "x ? y", "function f(...) {}", "var v = new <int>;", "a >>>= >>",
        };
        for (const char* input : inputs) {
            VM vm;
            vm.load(input, "broken.as");
        }
    });

    suite.addTest("Long chains work, deep nesting is an error", []() {
        // 20,000 terms: no recursion as deep as the chain.
        std::string sum = "var total = 0";
        for (int i = 0; i < 20000; ++i) sum += " + 1";
        sum += ";";
        VM vm;
        if (!vm.load(sum, "Sum.as") || Val(vm.getGlobal("total")).toNumber() != 20000) {
            throw std::runtime_error("long chain: " + vm.getError());
        }
        // 150 parentheses deep is fine; 5,000 is an error, not a crash.
        const auto nested = [](int depth) {
            return "var x = " + std::string(static_cast<size_t>(depth), '(') + "1" +
                   std::string(static_cast<size_t>(depth), ')') + ";";
        };
        if (!vm.load(nested(150), "Nested.as")) {
            throw std::runtime_error("150 deep: " + vm.getError());
        }
        if (vm.load(nested(5000), "Deep.as") || vm.getError().find("Nested too deeply") == std::string::npos) {
            throw std::runtime_error("5000 deep: " + vm.getError());
        }
        std::string arrays = "var a = " + std::string(5000, '[') + std::string(5000, ']') + ";";
        if (vm.load(arrays, "Arrays.as")) {
            throw std::runtime_error("5000 arrays deep parsed");
        }
        std::string blocks = std::string(5000, '{') + std::string(5000, '}');
        if (vm.load(blocks, "Blocks.as")) {
            throw std::runtime_error("5000 blocks deep parsed");
        }
    });

    suite.addTest("Runtime errors have ActionScript's messages", []() {
        VM vm;
        vm.load("var o = null;\no.x = 1;\n", "Null.as");
        expectContains(vm.getError(), "TypeError: Error #1009: Cannot access a property or method of a null object reference.");
        if (vm.getErrorLine() != 2) throw std::runtime_error("line " + std::to_string(vm.getErrorLine()));
        vm.load("var u;\nu.x;\n", "Undefined.as");
        expectContains(vm.getError(), "TypeError: Error #1010");
        vm.load("missing();\n", "Missing.as");
        expectContains(vm.getError(), "ReferenceError: Error #1065: Variable missing is not defined.");
        vm.load("var n = 5;\nn();\n", "Call.as");
        expectContains(vm.getError(), "TypeError: Error #1006");
        vm.load("class C { public function C() { this.go(); } }\nnew C();\n", "Method.as");
        expectContains(vm.getError(), "TypeError: Error #1006: go is not a function.");
    });

    suite.addTest("Stack traces give the functions and lines", []() {
        VM vm;
        vm.load("package app {\n"
                "  public class Deep {\n"
                "    public function outer():void {\n"
                "      inner();\n"
                "    }\n"
                "    private function inner():void {\n"
                "      var o = null;\n"
                "      o.go();\n"
                "    }\n"
                "  }\n"
                "}\n"
                "new app.Deep().outer();\n",
                "Deep.as");
        const std::string stack = vm.getErrorStack();
        expectContains(stack, "at app.Deep/inner()[Deep.as:8]");
        expectContains(stack, "at app.Deep/outer()[Deep.as:4]");
        if (vm.getErrorLine() != 8) throw std::runtime_error("line " + std::to_string(vm.getErrorLine()));
    });

    suite.addTest("Runaway recursion is an error, and the VM goes on", []() {
        VM vm;
        vm.load("function down(n) { return down(n + 1) + 1; }\ndown(0);\n", "Recurse.as");
        expectContains(vm.getError(), "Error #1023");
        // Through native calls too: toString calling itself.
        vm.load("class Loop { public function toString():String { return 'x' + this; } }\nvar s = '' + new Loop();\n", "Loop.as");
        expectContains(vm.getError(), "Error #1023");
        // And through getters.
        vm.load("class G { public function get v():int { return v + 1; } }\nnew G().v;\n", "Getter.as");
        expectContains(vm.getError(), "Error #1023");
        vm.load("var ok = 1 + 1;\n", "After.as");
        if (vm.hasError() || Val(vm.getGlobal("ok")).toNumber() != 2) {
            throw std::runtime_error("VM not usable after: " + vm.getError());
        }
    });

    suite.addTest("C++ exceptions from natives become errors", []() {
        VM vm;
        vm.defineFunction("hostFails", [](VM&, const Val&, Args) -> Val { throw std::runtime_error("host trouble"); });
        vm.load("var got = '';\ntry { hostFails(); } catch (e:Error) { got = e.message; }\nhostFails();\n", "Host.as");
        expectContains(vm.getError(), "host trouble");
        if (Val(vm.getGlobal("got")).toString() != "host trouble") {
            throw std::runtime_error("not caught as an Error");
        }
    });

    suite.addTest("Errors in the class resolver's classes are reported", []() {
        VM vm;
        vm.setClassResolver([](const std::string& name) -> std::optional<ClassSource> {
            if (name == "bad.Broken") return ClassSource{"package bad { public class Broken { function f( { } }", "Broken.as"};
            return std::nullopt;
        });
        vm.load("import bad.Broken;\nvar b = new Broken();\n", "Use.as");
        expectContains(vm.getError(), "SyntaxError");
        expectContains(vm.getError(), "Broken.as");
    });

    // A frame's line is right while a call it did not write is in it:
    // the valueOf an addition runs, the toString a coercion runs.
    suite.addTest("Stack traces through implicit calls give the caller's line", []() {
        VM vm;
        vm.load("class Bad {\n"
                "  public function valueOf():Number { throw new Error('no value'); }\n"
                "  public function toString():String { throw new Error('no text'); }\n"
                "}\n"
                "function sum():Number {\n"
                "  var b:Bad = new Bad();\n"
                "  var n:Number = 1;\n"
                "  n = b * 2;\n"
                "  return n;\n"
                "}\n"
                "function text():String {\n"
                "  var b:Bad = new Bad();\n"
                "  var s:String = b;\n"
                "  return s;\n"
                "}\n"
                "var where:String = '';\n"
                "try { sum(); } catch (e:Error) { where += e.getStackTrace(); }\n"
                "try { text(); } catch (e:Error) { where += e.getStackTrace(); }\n"
                "var result:String = where;\n",
                "Implicit.as");
        if (vm.hasError()) throw std::runtime_error(vm.getError());
        const std::string where = Val(vm.getGlobal("result")).toString();
        expectContains(where, "at Bad/valueOf()[Implicit.as:2]");
        expectContains(where, "at sum()[Implicit.as:8]");
        expectContains(where, "at Bad/toString()[Implicit.as:3]");
        expectContains(where, "at text()[Implicit.as:13]");
    });
}

static struct ErrorTestInit {
    ErrorTestInit() { init_error_tests(); }
} error_test_init;
