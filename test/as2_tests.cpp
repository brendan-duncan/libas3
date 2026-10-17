#include "test_runner.h"
#include "../src/parser/parser.h"
#include "../src/bytecode/compiler.h"
#include "../src/bytecode/vm.h"
#include <stdexcept>

using namespace as3;
using namespace as3test;

// ActionScript 1 and 2, as the scripts in old Flash movies are written, and
// what a host such as a player needs to run them.
TEST_SUITE(AS2)

namespace {

std::shared_ptr<BytecodeFunction> compileSource(const std::string& code) {
    Parser parser(code);
    auto program = parser.parse();
    if (parser.hasErrors()) {
        throw std::runtime_error("Parse error: " + parser.getErrors());
    }
    Compiler compiler;
    return compiler.compile(program.get());
}

// Runs code, gathering what it traces, one line a call.
std::string run(VM& vm, const std::string& code, ValuePtr scope = nullptr) {
    std::string out;
    vm.registerBuiltin("trace", [&out](const std::vector<ValuePtr>& args) {
        for (size_t i = 0; i < args.size(); ++i) {
            out += (i ? " " : "") + args[i]->toString();
        }
        out += "\n";
        return Value::makeUndefined();
    });
    vm.execute(compileSource(code), scope);
    if (vm.hasError()) {
        throw std::runtime_error("Runtime error: " + vm.getError());
    }
    return out;
}

void expect(const std::string& got, const std::string& wanted) {
    if (got != wanted) {
        throw std::runtime_error("got \"" + got + "\", wanted \"" + wanted + "\"");
    }
}

// A movie clip as a host keeps it: _visible and _x worked out by the host,
// and a method bound to the clip.
class Clip : public HostObject {
public:
    bool visible = true;
    double x = 0;
    int frame = 1;

    ValuePtr get(const std::string& name) override {
        if (name == "_visible") return Value::makeBoolean(visible);
        if (name == "_x") return Value::makeNumber(x);
        if (name == "gotoAndStop") {
            return Value::makeNativeFunction([this](const std::vector<ValuePtr>& args) {
                frame = args.empty() ? frame : static_cast<int>(args[0]->toNumber());
                return Value::makeUndefined();
            });
        }
        return nullptr;
    }

    bool set(const std::string& name, ValuePtr value) override {
        if (name == "_visible") {
            visible = value->toBoolean();
            return true;
        }
        if (name == "_x") {
            x = value->toNumber();
            return true;
        }
        return false;
    }
};

} // namespace

static void init_as2_tests() {
    auto& suite = suite_AS2;

    suite.addTest("Statements end at line breaks", []() {
        VM vm;
        expect(run(vm, "var a = 1\nvar b = 2\ntrace(a + b)\n"), "3\n");
        expect(run(vm, "function f() {\n  return\n}\ntrace(f())"), "undefined\n");
    });

    suite.addTest("If without else", []() {
        VM vm;
        expect(run(vm, "var n = 0;\nif (n == 0) {\n\ttrace('then');\n}\ntrace('after');\n"), "then\nafter\n");
    });

    suite.addTest("Loose equality", []() {
        VM vm;
        expect(run(vm, "trace(false == '0', true == '1', 1 == '1', null == undefined, 0 == null, '12abc' == 12, "
                       "'a' != 'a');"),
               "true true true true false false false\n");
    });

    suite.addTest("Missing and extra arguments", []() {
        VM vm;
        expect(run(vm, "function f(a, b) { return b; }\ntrace(f(1), f(1, 2, 3));"), "undefined 2\n");
    });

    suite.addTest("Runtime errors are kept, not thrown", []() {
        VM vm;
        vm.execute(compileSource("var x = 1;\nnothing();"));
        if (!vm.hasError() || vm.getError().empty()) {
            throw std::runtime_error("no error kept");
        }
        // And the VM runs on afterwards.
        expect(run(vm, "trace('again');"), "again\n");
    });

    suite.addTest("on, onClipEvent and tellTarget become calls", []() {
        VM vm;
        std::vector<std::string> events;
        ValuePtr handler;
        vm.registerBuiltin("__on", [&](const std::vector<ValuePtr>& args) {
            for (const ValuePtr& event : args[0]->elements) {
                events.push_back(event->toString());
            }
            handler = args[1];
            return Value::makeUndefined();
        });
        expect(run(vm, "on (release, keyPress \"<Enter>\") {\n\ttrace('clicked');\n}\n"), "");
        if (events.size() != 2 || events[0] != "release" || events[1] != "keyPress <Enter>") {
            throw std::runtime_error("events not given");
        }
        // The host runs the block when the event comes.
        std::string out;
        vm.registerBuiltin("trace", [&out](const std::vector<ValuePtr>& args) {
            out += args[0]->toString();
            return Value::makeUndefined();
        });
        vm.call(handler);
        expect(out, "clicked");
        if (vm.hasError()) {
            throw std::runtime_error(vm.getError());
        }
        // A function called on is still a call.
        vm.registerBuiltin("on", [](const std::vector<ValuePtr>& args) {
            return Value::makeNumber(args[0]->toNumber() * 2);
        });
        expect(run(vm, "trace(on(4));"), "8\n");
    });

    suite.addTest("ifFrameLoaded is an if", []() {
        VM vm;
        int asked = 0;
        vm.registerBuiltin("ifFrameLoaded", [&asked](const std::vector<ValuePtr>& args) {
            asked = static_cast<int>(args[0]->toNumber());
            return Value::makeBoolean(true);
        });
        expect(run(vm, "ifFrameLoaded (10) {\n\ttrace('loaded');\n}\n"), "loaded\n");
        if (asked != 10) {
            throw std::runtime_error("frame not given");
        }
    });

    suite.addTest("Host objects and the timeline's scope", []() {
        VM vm;
        auto root = std::make_shared<Clip>();
        auto hat = std::make_shared<Clip>();
        ValuePtr rootValue = Value::makeHostObject(root);
        rootValue->setProperty("hat", Value::makeHostObject(hat));
        // Bare names are the timeline's: its clips, and variables kept on it.
        expect(run(vm, "hat._visible = false;\nhat._x = hat._x + 20;\nhat.gotoAndStop(3);\ncount = 2;\n"
                       "trace(hat._visible, hat._x, count);\n",
                   rootValue),
               "false 20 2\n");
        if (hat->visible || hat->x != 20 || hat->frame != 3) {
            throw std::runtime_error("host object not changed");
        }
        if (rootValue->getProperty("count")->toNumber() != 2) {
            throw std::runtime_error("variable not kept on the timeline");
        }
        // Another script on the same timeline sees them.
        expect(run(vm, "trace(count + 1, this == undefined);", rootValue), "3 false\n");
    });

    suite.addTest("with on a clip", []() {
        VM vm;
        auto root = std::make_shared<Clip>();
        auto hat = std::make_shared<Clip>();
        ValuePtr rootValue = Value::makeHostObject(root);
        rootValue->setProperty("hat", Value::makeHostObject(hat));
        run(vm, "with (hat) {\n  gotoAndStop(3);\n  _x = 4;\n  _visible = !_visible;\n}\n", rootValue);
        if (hat->frame != 3 || hat->x != 4 || hat->visible || root->x != 0) {
            throw std::runtime_error("with did not act on the clip");
        }
    });

    suite.addTest("On a timeline, a global wins over the timeline's own", []() {
        VM vm;
        auto root = std::make_shared<Clip>();
        ValuePtr rootValue = Value::makeHostObject(root);
        // The clip has its own gotoAndStop; the host's global one (which
        // knows scenes) is the one a frame script calls.
        std::string called;
        vm.registerBuiltin("gotoAndStop", [&called](const std::vector<ValuePtr>& args) {
            called = "global " + args[0]->toString();
            return Value::makeUndefined();
        });
        run(vm, "gotoAndStop('ending');\n_x = 7;\n", rootValue);
        expect(called, "global ending");
        if (root->frame != 1 || root->x != 7) {
            throw std::runtime_error("the clip's own was called, or _x not set on it");
        }
    });

    suite.addTest("getProperty and setProperty name the property", []() {
        VM vm;
        std::string seen;
        vm.registerBuiltin("getProperty", [&seen](const std::vector<ValuePtr>& args) {
            seen += args[0]->toString() + ":" + args[1]->toString() + " ";
            return Value::makeNumber(7);
        });
        vm.registerBuiltin("setProperty", [&seen](const std::vector<ValuePtr>& args) {
            seen += args[0]->toString() + ":" + args[1]->toString() + "=" + args[2]->toString() + " ";
            return Value::makeUndefined();
        });
        // Even with a variable called _x about.
        expect(run(vm, "var _x = 99;\nx = getProperty(\"hat\", _x);\nsetProperty(\"hat\", _visible, x + 1);\ntrace(x);"),
               "7\n");
        expect(seen, "hat:_x hat:_visible=8 ");
    });

    suite.addTest("Compound assignment, ++ and --", []() {
        VM vm;
        // x += y was x = y before.
        expect(run(vm, "var x = 5;\nx += 3;\nx -= 1;\nx *= 2;\ntrace(x);"), "14\n");
        expect(run(vm, "var s = 'a';\ns += 'b';\ntrace(s);"), "ab\n");
        // Prefix gives the new value, postfix the old; both store the new.
        expect(run(vm, "var i = 1;\nvar a = i++;\nvar b = ++i;\nvar c = i--;\ntrace(a + ' ' + b + ' ' + c + ' ' + i);"),
               "1 3 3 2\n");
        // On properties and elements too.
        expect(run(vm, "var o = {n: 1};\no.n++;\no.n += 10;\nvar list = [5, 6];\nlist[1]++;\nlist[0] -= 5;\n"
                       "trace(o.n + ' ' + list[0] + ' ' + list[1]);"),
               "12 0 7\n");
        expect(run(vm, "var count = 0;\nfor (var k = 0; k < 4; k++) { count += k; }\ntrace(count);"), "6\n");
        // a[i] = v puts v at i (it once put i at v).
        expect(run(vm, "var slots = [0, 0, 0];\nslots[2] = 9;\nvar map = {};\nmap['k'] = 'v';\n"
                       "trace(slots[2] + ' ' + slots.length + ' ' + map.k);"),
               "9 3 v\n");
    });

    suite.addTest("new Object() and new Array()", []() {
        VM vm;
        expect(run(vm, "var o = new Object();\no.name = 'hat';\nvar a = new Array(1, 2, 3);\n"
                       "trace(o.name + ' ' + a.length + ' ' + new Array().length);"),
               "hat 3 0\n");
    });

    suite.addTest("A class file, as a document class is written", []() {
        VM vm;
        expect(run(vm,
                   "package {\n"
                   "    import flash.display.MovieClip;\n"
                   "    import flash.events.MouseEvent;\n"
                   "    public class Main extends MovieClip {\n"
                   "        private var count:int = 0;\n"
                   "        public function Main() {\n"
                   "            count = 5;\n"
                   "        }\n"
                   "        public function add(amount:int):int {\n"
                   "            count += amount;\n"
                   "            return count;\n"
                   "        }\n"
                   "    }\n"
                   "}\n"
                   "var main = new Main();\n"
                   "trace(main.add(2));\n"),
               "7\n");
    });

    suite.addTest("new on a function", []() {
        VM vm;
        // An ActionScript 1 constructor function.
        expect(run(vm, "function Point(x, y) {\n    this.x = x;\n    this.y = y;\n}\nvar p = new Point(2, 3);\n"
                       "trace(p.x + p.y);"),
               "5\n");
        // A host's constructor.
        vm.registerBuiltin("Box", [](const std::vector<ValuePtr>& args) {
            ValuePtr box = Value::makeObject();
            box->setProperty("w", Value::makeNumber(args.empty() ? 4 : args[0]->toNumber()));
            return box;
        });
        expect(run(vm, "var b = new Box(7);\ntrace(b.w + ' ' + new Box().w);"), "7 4\n");
    });

    suite.addTest("Calls from the host", []() {
        VM vm;
        run(vm, "function add(a, b) { return a + b; }");
        ValuePtr add = vm.getGlobal("add");
        ValuePtr sum = vm.call(add, {Value::makeNumber(2), Value::makeNumber(3)});
        if (vm.hasError() || sum->toNumber() != 5) {
            throw std::runtime_error("call failed: " + vm.getError());
        }
        // A failing function leaves the error and the VM usable.
        run(vm, "function bad() { nothing(); }");
        vm.call(vm.getGlobal("bad"));
        if (!vm.hasError()) {
            throw std::runtime_error("no error from the call");
        }
        sum = vm.call(add, {Value::makeNumber(1), Value::makeNumber(1)});
        if (vm.hasError() || sum->toNumber() != 2) {
            throw std::runtime_error("VM not usable after an error");
        }
    });

    suite.addTest("Errors give their line", []() {
        VM vm;
        vm.execute(compileSource("var a = 1;\n\nif (a == 1) {\n    nothing();\n}\n"));
        if (!vm.hasError() || vm.getErrorLine() != 4) {
            throw std::runtime_error("error line " + std::to_string(vm.getErrorLine()));
        }
        // In a function too, called later.
        run(vm, "function later() {\n    var b = 2;\n    missing(b);\n}");
        vm.call(vm.getGlobal("later"));
        if (!vm.hasError() || vm.getErrorLine() != 3) {
            throw std::runtime_error("error line in a function " + std::to_string(vm.getErrorLine()));
        }
    });
}

static struct AS2TestInit {
    AS2TestInit() { init_as2_tests(); }
} as2_test_init;
