#include "test_runner.h"
#include "js_programs.h"
#include "../src/js/js.h"
#include "../src/js/js_writer.h"
#include "../src/bytecode/vm.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
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
std::vector<std::string> runInNode(const std::vector<std::string>& bundles, const std::string& prelude = std::string()) {
    const fs::path dir = scratch();
    writeText(dir / "as3.js", js::runtime());
    std::string list = "[";
    for (size_t i = 0; i < bundles.size(); ++i) list += (i ? ",\n" : "") + js::quote(bundles[i]);
    list += "]";
    writeText(dir / "cases.json", list);
    writeText(dir / "prelude.js", prelude);
    writeText(dir / "run.js", R"(const vm = require("vm");
const fs = require("fs");
const runtime = fs.readFileSync(process.argv[2], "utf8");
const cases = JSON.parse(fs.readFileSync(process.argv[3], "utf8"));
const prelude = fs.readFileSync(process.argv[4], "utf8");
let all = "";
for (let i = 0; i < cases.length; i++) {
    let out = "";
    const context = vm.createContext({ setTimeout, clearTimeout, performance });
    context.print = (text) => { out += text + "\n"; };
    try {
        vm.runInContext(runtime, context, { filename: "as3.js" });
        vm.runInContext("as3.output = print; as3.manualTimers = true;", context);
        vm.runInContext(prelude, context, { filename: "prelude.js" });
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
        capture("node \"" + (dir / "run.js").string() + "\" \"" + (dir / "as3.js").string() + "\" \"" + (dir / "cases.json").string() + "\" \"" +
                (dir / "prelude.js").string() + "\"");
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

// bunnyPaint: its classes, as3corelib's JPEG encoder, and the frame script
// and stage instances of its document's main timeline (as the ABC
// compiler's suite reads them).
const char* kPaint = "D:/Backup/hulabunny/bunnyPaint/";

bool paintThere() {
    return fs::exists(fs::path(kPaint) / "com/hulabunny/bunnyPaint/BP_Canvas.as");
}

std::vector<js::Source> paintSources() {
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(fs::path(kPaint) / "com/hulabunny/bunnyPaint")) {
        if (entry.is_regular_file() && entry.path().extension() == ".as") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    files.push_back(fs::path(kPaint) / "as3corelib-.93/src/com/adobe/images/JPGEncoder.as");
    files.push_back(fs::path(kPaint) / "as3corelib-.93/src/com/adobe/images/BitString.as");
    std::vector<js::Source> sources;
    for (const fs::path& file : files) sources.push_back({readText(file), file.filename().string()});
    return sources;
}

std::string attribute(const std::string& tag, const std::string& name) {
    const size_t at = tag.find(" " + name + "=\"");
    if (at == std::string::npos) return std::string();
    const size_t start = at + name.size() + 3;
    return tag.substr(start, tag.find('"', start) - start);
}

js::Timeline paintTimeline() {
    const std::string document = readText(fs::path(kPaint) / "test/DOMDocument.xml");
    js::Timeline timeline;
    timeline.className = js::timelineClassName("bunnyPaint");
    size_t at = 0;
    while ((at = document.find("<DOM", at)) != std::string::npos) {
        const std::string tag = document.substr(at, document.find('>', at) - at);
        at += 4;
        const std::string name = attribute(tag, "name");
        if (name.empty()) continue;
        std::string type;
        if (tag.compare(0, 18, "<DOMSymbolInstance") == 0) {
            type = attribute(tag, "symbolType") == "button" ? "flash.display.SimpleButton" : "flash.display.MovieClip";
        } else if (tag.compare(0, 21, "<DOMComponentInstance") == 0) {
            type = "fl.controls." + attribute(tag, "libraryItemName");
        } else if (tag.compare(0, 12, "<DOMInputText") == 0 || tag.find("Text ") != std::string::npos) {
            type = "flash.text.TextField";
        } else {
            continue;
        }
        timeline.instances.push_back({name, type});
    }
    const size_t script = document.find("<script><![CDATA[");
    const size_t end = document.find("]]></script>", script);
    if (script == std::string::npos || end == std::string::npos) throw std::runtime_error("no frame script");
    timeline.frames.push_back({0, document.substr(script + 17, end - script - 17)});
    return timeline;
}

// Stand-ins for the flash.* and fl.* classes and functions a bundle uses:
// empty classes following the runtime's protocol ($construct), extending
// what the player's do; ByteArray keeps floats, as BP_Random needs.
const char* kStandIns = R"js(
(function () {
    const bases = {
        "flash.display.DisplayObject": "flash.events.EventDispatcher",
        "flash.display.InteractiveObject": "flash.display.DisplayObject",
        "flash.display.DisplayObjectContainer": "flash.display.InteractiveObject",
        "flash.display.Sprite": "flash.display.DisplayObjectContainer",
        "flash.display.MovieClip": "flash.display.Sprite",
        "flash.display.Shape": "flash.display.DisplayObject",
        "flash.display.Bitmap": "flash.display.DisplayObject",
        "flash.display.SimpleButton": "flash.display.InteractiveObject",
        "flash.display.Loader": "flash.display.DisplayObjectContainer",
        "flash.text.TextField": "flash.display.InteractiveObject",
        "flash.events.MouseEvent": "flash.events.Event",
        "flash.filters.BevelFilter": "flash.filters.BitmapFilter",
        "flash.filters.BlurFilter": "flash.filters.BitmapFilter",
        "flash.filters.ColorMatrixFilter": "flash.filters.BitmapFilter",
        "flash.filters.DropShadowFilter": "flash.filters.BitmapFilter",
        "flash.filters.GlowFilter": "flash.filters.BitmapFilter",
        "flash.net.URLLoader": "flash.events.EventDispatcher",
        "fl.core.UIComponent": "flash.display.Sprite",
        "fl.data.DataProvider": "flash.events.EventDispatcher",
    };
    const made = new Set();
    function stand(name) {
        if (made.has(name) || as3.isDefined(name)) return;
        made.add(name);
        const short = name.slice(name.lastIndexOf(".") + 1);
        if (/^[a-z]/.test(short)) {
            as3.register(name, function () {});
            return;
        }
        let baseName = bases[name] || (name.startsWith("fl.controls.") ? "fl.core.UIComponent" :
            name.startsWith("fl.events.") ? "flash.events.Event" : null);
        let cls;
        if (baseName) {
            stand(baseName);
            const Base = as3.use(baseName);
            cls = class extends Base {
                constructor(...args) { super(); if (new.target === cls) this.$construct(...args); }
                $construct() { super.$construct(); }
            };
        } else {
            cls = class {
                constructor(...args) { if (new.target === cls) this.$construct(...args); }
                $construct() {}
            };
        }
        Object.defineProperty(cls, "name", { value: short });
        as3.register(name, cls);
    }
    globalThis.standIns = function (names) { names.forEach(stand); };
    class ByteArray {
        constructor(...args) { if (new.target === ByteArray) this.$construct(...args); }
        $construct() { this.values = []; this.position = 0; }
        writeFloat(v) { this.values.push(Math.fround(v)); this.position = this.values.length * 4; }
        readFloat() { const i = this.position >> 2; this.position += 4; return i < this.values.length ? this.values[i] : 0; }
        get length() { return this.values.length * 4; }
    }
    as3.register("flash.utils.ByteArray", ByteArray);
})();
)js";

// The flash.* and fl.* names a bundle uses.
std::vector<std::string> playerNames(const std::string& bundle) {
    std::vector<std::string> names;
    const std::string use = "as3.use(\"";
    for (size_t at = bundle.find(use); at != std::string::npos; at = bundle.find(use, at + 1)) {
        const size_t start = at + use.size();
        const std::string name = bundle.substr(start, bundle.find('"', start) - start);
        if ((name.compare(0, 6, "flash.") == 0 || name.compare(0, 3, "fl.") == 0) &&
            std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(name);
        }
    }
    return names;
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

static void init_js_paint_tests() {
    auto& suite = suite_JavaScript;

    suite.addTest("bunnyPaint's classes that need no display run", []() {
        if (!paintThere() || !nodeThere()) {
            std::cout << "    (sources or node not found; skipped)" << std::endl;
            return;
        }
        std::vector<js::Source> sources = paintSources();
        sources.push_back({"import com.hulabunny.bunnyPaint.*;\n"
                           "var c = BP_FloatColor.fromInt(0x336699);\n"
                           "c.mixWith(new BP_FloatColor(1, 1, 1), 0.5);\n"
                           "trace(c.uintValue.toString(16), c.uintValue);\n"
                           "var random = new BP_Random(); random.reset(5); var v = random.value();\n"
                           "var copy = new BP_Random(random); copy.reset(5);\n"
                           "trace(v >= 0 && v < 1, copy.value() >= 0);\n"
                           "var options = new BP_ToolOptions(15, 0, 0.6);\n"
                           "options.brushSize = 7.2; options.brushAlpha = 2;\n"
                           "trace(options.brushSize, options.brushSizeInt, options.sizeInt, options.brushAlpha, options.brushAlphaInt, options.type == BP_ToolOptions.B_ROUND_AIRBRUSH, options.spacing);\n"
                           "var dab = new BP_BrushDab(); dab.width = 4; dab.setPosition(10, 10);\n"
                           "trace(dab.x, dab.y, dab.brush);\n",
                           "paint.as"});
        const js::Output out = js::translate(sources);
        expect(out.ok, out.errorText());
        std::string names;
        for (const std::string& name : playerNames(out.bundle)) names += (names.empty() ? "" : ", ") + js::quote(name);
        const std::vector<std::string> got = runInNode({out.bundle}, std::string(kStandIns) + "standIns([" + names + "]);\n");
        expect(got[0] == "99b2cc 10072780\ntrue true\n7.2 8 16 1 255 true 0.035\n0 0 null\n", "got \"" + got[0] + "\"");
    });

    suite.addTest("bunnyPaint translates", []() {
        if (!paintThere()) {
            std::cout << "    (sources not found; skipped)" << std::endl;
            return;
        }
        js::Timeline closeButton;
        closeButton.className = "CloseButton";
        const js::Output out = js::translate(paintSources(), {closeButton, paintTimeline()});
        std::string problems;
        for (const auto& diagnostic : out.diagnostics) {
            // The one known slip, MultiSmudgeTool.as line 50.
            if (diagnostic.fileName == "MultiSmudgeTool.as" && diagnostic.line == 50) continue;
            problems += diagnostic.text() + "\n";
        }
        if (const char* dump = std::getenv("AS3_JS_DUMP")) {
            for (const auto& file : out.files) writeText(fs::path(dump) / "paint" / file.first, file.second);
            writeText(fs::path(dump) / "paint" / "bundle.js", out.bundle);
        }
        expect(problems.empty(), "\n" + problems);
        expect(out.ok, "not ok");
        if (nodeThere()) {
            // Every class defined, its statics initialized, with the
            // stand-ins.
            std::string names;
            for (const std::string& name : playerNames(out.bundle)) names += (names.empty() ? "" : ", ") + js::quote(name);
            std::string classes;
            for (const std::string& name : out.classes) classes += (classes.empty() ? "" : ", ") + js::quote(name);
            const std::string load = "for (const name of [" + classes + "]) {\n"
                                     "    const value = as3.use(name);\n"
                                     "    if (typeof value !== \"function\") as3.trace(name + \" is not a class\");\n"
                                     "}\n"
                                     "as3.trace(\"loaded\");\n";
            const std::vector<std::string> got = runInNode({out.bundle + load}, std::string(kStandIns) + "standIns([" + names + "]);\n");
            expect(got[0] == "loaded\n", "loading bunnyPaint:\n" + got[0]);
        }
        std::cout << "    " << out.classes.size() << " classes, " << out.files.size() << " files, " << out.bundle.size()
                  << " bytes" << std::endl;
    });
}

static struct JsTestInit {
    JsTestInit() {
        init_js_tests();
        init_js_paint_tests();
    }
} js_test_init;
