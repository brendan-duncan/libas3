#include "test_runner.h"
#include "js_programs.h"
#include "../src/js/js.h"
#include "../src/js/js_writer.h"
#include "../src/bytecode/vm.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
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
    // XML's settings are the process's: back to their defaults.
    vm.load("XML.ignoreComments = true; XML.ignoreProcessingInstructions = true; XML.ignoreWhitespace = true;"
            "XML.prettyPrinting = true; XML.prettyIndent = 2;", "settings.as");
    if (!vm.load(code, "main.as")) out += "Error: " + vm.getError() + "\n";
    return out;
}

// Programs whose output differs, and why: what translated code does not
// do as the VM does.
const std::map<std::string, std::string> kKnownDifferences = {
    {"language_tests.cpp: Default and rest parameters, arguments",
     "a function's length counts the parameters before the first with a default"},
    {"library_tests.cpp: Vector", "writing past a Vector's end extends it rather than throwing RangeError #1125"},
    {"library_tests.cpp: JSON", "JSON.stringify gives a class's fields in their order, the VM's sorted"},
    {"xml_tests.cpp: describeType", "describeType gives no metadata"},
};

js::Output translated(const std::string& code, const js::Options& options = js::Options()) {
    return js::translate({{code, "main.as"}}, {}, options);
}

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void expectIn(const std::string& text, const std::string& wanted) {
    if (text.find(wanted) == std::string::npos) throw std::runtime_error("no \"" + wanted + "\" in:\n" + text);
}

// The programs other suites run in the VM: each run("...") of a string,
// read from their sources (beside this file).
std::vector<std::pair<std::string, std::string>> testPrograms(const std::string& fileName,
                                                              const std::string& function = "run") {
    std::vector<std::pair<std::string, std::string>> programs;
    const fs::path path = fs::path(__FILE__).parent_path() / fileName;
    const std::string text = readText(path);
    std::string testName;
    for (size_t at = 0; at < text.size(); ++at) {
        if (text.compare(at, 15, "suite.addTest(\"") == 0) {
            const size_t end = text.find('"', at + 15);
            testName = text.substr(at + 15, end - at - 15);
        }
        if (text.compare(at, function.size() + 1, function + "(") != 0 ||
            (at > 0 && (std::isalnum(static_cast<unsigned char>(text[at - 1])) || text[at - 1] == '_'))) {
            continue;
        }
        size_t i = at + function.size() + 1;
        std::string code;
        bool any = false;
        for (;;) {
            while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
            if (text.compare(i, 3, "R\"(") == 0) {
                const size_t end = text.find(")\"", i + 3);
                code += text.substr(i + 3, end - i - 3);
                i = end + 2;
                any = true;
            } else if (i < text.size() && text[i] == '"') {
                ++i;
                while (i < text.size() && text[i] != '"') {
                    char c = text[i++];
                    if (c == '\\') {
                        const char e = text[i++];
                        switch (e) {
                            case 'n': c = '\n'; break;
                            case 't': c = '\t'; break;
                            case 'r': c = '\r'; break;
                            case 'x': {
                                size_t used = 0;
                                c = static_cast<char>(std::stoi(text.substr(i, 2), &used, 16));
                                i += used;
                                break;
                            }
                            default: c = e; break;
                        }
                    }
                    code += c;
                }
                ++i;
                any = true;
            } else {
                break;
            }
        }
        if (any && i < text.size() && (text[i] == ')' || text[i] == ',')) {
            programs.emplace_back(fileName + ": " + testName, code);
        }
    }
    return programs;
}

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
        std::vector<std::pair<std::string, std::string>> programs;
        for (const TestProgram& program : kPrograms) programs.emplace_back(program.name, program.code);
        for (const char* file : {"language_tests.cpp", "library_tests.cpp", "xml_tests.cpp"}) {
            for (auto& program : testPrograms(file)) programs.push_back(program);
        }
        for (auto& program : testPrograms("vm_tests.cpp", "executeAndVerify")) programs.push_back(program);
        size_t untranslated = 0;
        for (const auto& program : programs) {
            wanted.push_back(runInVm(program.second));
            js::Output out = translated(program.second);
            if (!out.ok) {
                problems += program.first + ": does not translate:\n" + out.errorText();
                ++untranslated;
            }
            bundles.push_back(out.bundle);
        }
        const std::vector<std::string> got = runInNode(bundles);
        size_t same = 0;
        size_t known = 0;
        for (size_t i = 0; i < got.size(); ++i) {
            if (got[i] == wanted[i]) {
                ++same;
                continue;
            }
            if (kKnownDifferences.count(programs[i].first)) {
                ++known;
                continue;
            }
            problems += "--- " + programs[i].first + "\n" + programs[i].second + "\nVM:\n" + wanted[i] + "JavaScript:\n" + got[i] + "\n";
            if (std::getenv("AS3_JS_DUMP")) problems += bundles[i] + "\n";
        }
        std::cout << "    " << same << " of " << got.size() << " programs the same, " << known << " known to differ, "
                  << untranslated << " not translated" << std::endl;
        if (!problems.empty()) throw std::runtime_error("\n" + problems);
    });
}

static struct JsTestInit {
    JsTestInit() { init_js_tests(); }
} js_test_init;
