#include "test_runner.h"
#include "../src/js/js.h"
#include "../src/js/js_writer.h"
#include "../src/bytecode/vm.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

using namespace as3;
using namespace as3test;

// ActionScript translated to JavaScript: what it makes, and programs run
// both in the VM and, translated, in Node (where it is installed), tracing
// the same.
TEST_SUITE(JavaScript)

namespace {

namespace fs = std::filesystem;

std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

fs::path scratch() {
    const fs::path dir = fs::temp_directory_path() / "libas3-js-tests";
    fs::create_directories(dir);
    return dir;
}

// What a command prints (and its errors).
std::string capture(const std::string& command) {
    std::string out;
#ifdef _WIN32
    FILE* pipe = _popen(("\"" + command + " 2>&1\"").c_str(), "r");
#else
    FILE* pipe = popen((command + " 2>&1").c_str(), "r");
#endif
    if (!pipe) return out;
    char buffer[4096];
    size_t got;
    while ((got = fread(buffer, 1, sizeof buffer, pipe)) > 0) out.append(buffer, got);
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return out;
}

bool nodeThere() {
    static const bool there = capture("node --version").compare(0, 1, "v") == 0;
    return there;
}

// The translated program's output in Node, each in a context of its own:
// the runtime, the bundle, what it traces (and an error not caught).
std::vector<std::string> runInNode(const std::vector<std::string>& bundles) {
    const fs::path dir = scratch();
    writeText(dir / "as3.js", js::runtime());
    std::string list = "[";
    for (size_t i = 0; i < bundles.size(); ++i) list += (i ? ",\n" : "") + js::quote(bundles[i]);
    list += "]";
    writeText(dir / "cases.json", list);
    writeText(dir / "run.js", R"(const vm = require("vm");
const fs = require("fs");
const runtime = fs.readFileSync(process.argv[2], "utf8");
const cases = JSON.parse(fs.readFileSync(process.argv[3], "utf8"));
let all = "";
for (let i = 0; i < cases.length; i++) {
    let out = "";
    const context = vm.createContext({ setTimeout, clearTimeout, performance });
    context.print = (text) => { out += text + "\n"; };
    try {
        vm.runInContext(runtime, context, { filename: "as3.js" });
        vm.runInContext("as3.output = print; as3.manualTimers = true;", context);
        vm.runInContext(cases[i], context, { filename: "case" + i + ".js" });
    } catch (e) {
        let text;
        try { text = String(context.as3.error(e)); } catch (f) { text = String(e); }
        out += "Error: " + text + "\n";
    }
    all += "\u0001CASE " + i + "\n" + out;
}
process.stdout.write(all);
)");
    const std::string printed =
        capture("node \"" + (dir / "run.js").string() + "\" \"" + (dir / "as3.js").string() + "\" \"" + (dir / "cases.json").string() + "\"");
    std::vector<std::string> outputs(bundles.size());
    size_t at = 0;
    for (size_t i = 0; i < bundles.size(); ++i) {
        const std::string head = "\x01" "CASE " + std::to_string(i) + "\n";
        const size_t start = printed.find(head, at);
        if (start == std::string::npos) {
            outputs[i] = "(no output: " + printed.substr(at, 400) + ")";
            continue;
        }
        const size_t from = start + head.size();
        const size_t next = printed.find("\x01" "CASE ", from);
        outputs[i] = printed.substr(from, next == std::string::npos ? std::string::npos : next - from);
        at = from;
    }
    return outputs;
}

// What a program traces in the VM, and its error when one is not caught.
std::string runInVm(const std::string& code) {
    VM vm;
    std::string out;
    vm.defineFunction("trace", [&out](VM& vm, const Val&, Args args) {
        for (size_t i = 0; i < args.size(); ++i) out += (i ? " " : "") + vm.toString(args[i]);
        out += "\n";
        return Val();
    });
    vm.setClock([]() { return 0.0; });
    if (!vm.load(code, "main.as")) out += "Error: " + vm.getError() + "\n";
    return out;
}

js::Output translated(const std::string& code, const js::Options& options = js::Options()) {
    return js::translate({{code, "main.as"}}, {}, options);
}

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void expectIn(const std::string& text, const std::string& wanted) {
    if (text.find(wanted) == std::string::npos) throw std::runtime_error("no \"" + wanted + "\" in:\n" + text);
}

// Programs for both: classes, statements, the library's semantics.
struct TestProgram {
    const char* name;
    const char* code;
};

const TestProgram kPrograms[] = {
    {"trace and arithmetic", R"(
trace(1 + 2, "a" + 1, 7 / 2, 7 % 3, -5 >> 1, -5 >>> 28, 1 << 31);
trace(null, undefined, true, 0.1 + 0.2, 1e21, 1 / 3, -0);
)"},
};

} // namespace

static void init_js_tests() {
    auto& suite = suite_JavaScript;

    suite.addTest("A class translates to a definition", []() {
        js::Output out = translated(R"(
package com.example {
    import flash.display.Sprite;
    public class Ball extends Sprite {
        public static const SIDES:int = 4;
        public var speed:Number = 2;
        private var label:String = "ball";
        public function Ball(size:int = 3) { trace(size); }
        public function get area():Number { return speed * 2; }
        public function move(dx:int):void { x += dx; }
    }
}
)");
        expect(out.ok, out.errorText());
        expect(out.files.count("com/example/Ball.js") == 1, "no com/example/Ball.js");
        const std::string& text = out.files["com/example/Ball.js"];
        expectIn(text, "as3.define(\"com.example.Ball\", () => {");
        expectIn(text, "const Sprite = as3.use(\"flash.display.Sprite\");");
        expectIn(text, "class Ball extends Sprite {");
        expectIn(text, "$construct(size = 3) {");
        expectIn(text, "get area() {");
        expectIn(text, "this.x += dx;");
        expectIn(text, "Ball.SIDES = 4;");
        if (std::getenv("AS3_JS_DUMP")) std::cout << text << std::endl;
    });

    suite.addTest("Programs trace the same in the VM and in JavaScript", []() {
        if (!nodeThere()) {
            std::cout << "    (node not found; skipped)" << std::endl;
            return;
        }
        std::vector<std::string> bundles;
        std::vector<std::string> wanted;
        std::string problems;
        for (const TestProgram& program : kPrograms) {
            wanted.push_back(runInVm(program.code));
            js::Output out = translated(program.code);
            if (!out.ok) problems += std::string(program.name) + ": does not translate:\n" + out.errorText();
            bundles.push_back(out.bundle);
        }
        const std::vector<std::string> got = runInNode(bundles);
        size_t same = 0;
        for (size_t i = 0; i < got.size(); ++i) {
            if (got[i] == wanted[i]) {
                ++same;
                continue;
            }
            problems += std::string("--- ") + kPrograms[i].name + "\nVM:\n" + wanted[i] + "JavaScript:\n" + got[i] + "\n";
            if (std::getenv("AS3_JS_DUMP")) problems += bundles[i] + "\n";
        }
        std::cout << "    " << same << " of " << got.size() << " programs the same" << std::endl;
        if (!problems.empty()) throw std::runtime_error("\n" + problems);
    });
}

static struct JsTestInit {
    JsTestInit() { init_js_tests(); }
} js_test_init;
