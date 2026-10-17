#include "test_runner.h"
#include "js_programs.h"
#include "../src/js/js.h"
#include "../src/js/js_writer.h"
#include "../src/bytecode/vm.h"

#include <optional>

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
    const context = vm.createContext({ setTimeout, clearTimeout, performance, TextEncoder, require });
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

// BitmapData and ByteArray as JPGEncoder needs them: for the VM, and as
// JavaScript.
struct Bitmap : HostObject {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;
};

struct Bytes : HostObject {
    std::vector<uint8_t> bytes;
    size_t position = 0;
    bool little = false;
    void put(uint8_t b) {
        if (position >= bytes.size()) bytes.resize(position + 1);
        bytes[position++] = b;
    }
    uint8_t take() { return position < bytes.size() ? bytes[position++] : 0; }
    void putInt(uint32_t v) {
        for (int i = 0; i < 4; ++i) put(static_cast<uint8_t>(little ? v >> (8 * i) : v >> (24 - 8 * i)));
    }
    uint32_t takeInt() {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const uint32_t b = take();
            v |= little ? b << (8 * i) : b << (24 - 8 * i);
        }
        return v;
    }
    bool getIndex(const Val& key, Val& out) override {
        if (!key.isNumber()) return false;
        const size_t i = static_cast<size_t>(key.num);
        out = i < bytes.size() ? Val::number(bytes[i]) : Val();
        return true;
    }
    bool setIndex(const Val& key, const Val& value) override {
        if (!key.isNumber()) return false;
        const size_t i = static_cast<size_t>(key.num);
        if (i >= bytes.size()) bytes.resize(i + 1);
        bytes[i] = static_cast<uint8_t>(value.toInt32());
        return true;
    }
};

void defineImageClasses(VM& vm) {
    vm.defineNativeClass("flash.display.BitmapData")
        .constructor([](VM&, const Val& self, Args args) {
            auto state = std::make_shared<Bitmap>();
            state->width = args.integer(0);
            state->height = args.integer(1);
            state->pixels.assign(static_cast<size_t>(state->width) * static_cast<size_t>(state->height),
                                 args.size() > 3 ? args[3].toUint32() : 0xFFFFFFFF);
            self->host = state;
            return Val();
        })
        .getter("width", [](VM&, const Val& self, Args) { return Val::number(self.host<Bitmap>()->width); })
        .getter("height", [](VM&, const Val& self, Args) { return Val::number(self.host<Bitmap>()->height); })
        .method("getPixel32", [](VM&, const Val& self, Args args) {
            Bitmap* bitmap = self.host<Bitmap>();
            const int x = args.integer(0);
            const int y = args.integer(1);
            if (x < 0 || y < 0 || x >= bitmap->width || y >= bitmap->height) return Val::number(0);
            return Val::number(bitmap->pixels[static_cast<size_t>(y * bitmap->width + x)]);
        })
        .method("setPixel32", [](VM&, const Val& self, Args args) {
            Bitmap* bitmap = self.host<Bitmap>();
            const int x = args.integer(0);
            const int y = args.integer(1);
            if (x >= 0 && y >= 0 && x < bitmap->width && y < bitmap->height) {
                bitmap->pixels[static_cast<size_t>(y * bitmap->width + x)] = args[2].toUint32();
            }
            return Val();
        })
        .define();
    vm.defineNativeClass("flash.utils.ByteArray")
        .constructor([](VM&, const Val& self, Args) {
            self->host = std::make_shared<Bytes>();
            return Val();
        })
        .method("writeByte", [](VM&, const Val& self, Args args) {
            Bytes* bytes = self.host<Bytes>();
            if (bytes->position >= bytes->bytes.size()) bytes->bytes.resize(bytes->position + 1);
            bytes->bytes[bytes->position++] = static_cast<uint8_t>(args[0].toInt32());
            return Val();
        })
        .method("readUnsignedByte", [](VM&, const Val& self, Args) {
            Bytes* bytes = self.host<Bytes>();
            return Val::number(bytes->position < bytes->bytes.size() ? bytes->bytes[bytes->position++] : 0);
        })
        .method("readByte", [](VM&, const Val& self, Args) {
            return Val::number(static_cast<int8_t>(self.host<Bytes>()->take()));
        })
        .method("writeInt", [](VM&, const Val& self, Args args) {
            self.host<Bytes>()->putInt(args[0].toUint32());
            return Val();
        })
        .method("writeUnsignedInt", [](VM&, const Val& self, Args args) {
            self.host<Bytes>()->putInt(args[0].toUint32());
            return Val();
        })
        .method("readInt", [](VM&, const Val& self, Args) {
            return Val::number(static_cast<int32_t>(self.host<Bytes>()->takeInt()));
        })
        .method("readUnsignedInt", [](VM&, const Val& self, Args) {
            return Val::number(self.host<Bytes>()->takeInt());
        })
        .method("writeBytes", [](VM&, const Val& self, Args args) {
            Bytes* from = args[0].isObject() ? args[0].host<Bytes>() : nullptr;
            if (!from) return Val();
            const size_t offset = static_cast<size_t>(args.integer(1, 0));
            size_t length = static_cast<size_t>(args.integer(2, 0));
            if (length == 0 || offset + length > from->bytes.size()) length = from->bytes.size() > offset ? from->bytes.size() - offset : 0;
            const std::vector<uint8_t> copy(from->bytes.begin() + static_cast<long>(offset), from->bytes.begin() + static_cast<long>(offset + length));
            for (uint8_t b : copy) self.host<Bytes>()->put(b);
            return Val();
        })
        .method("writeUTFBytes", [](VM& vm, const Val& self, Args args) {
            for (unsigned char c : vm.toString(args[0])) self.host<Bytes>()->put(c);
            return Val();
        })
        .getter("bytesAvailable", [](VM&, const Val& self, Args) {
            Bytes* bytes = self.host<Bytes>();
            return Val::number(bytes->position < bytes->bytes.size() ? static_cast<double>(bytes->bytes.size() - bytes->position) : 0);
        })
        .property("endian", [](VM&, const Val& self, Args) { return Val::string(self.host<Bytes>()->little ? "littleEndian" : "bigEndian"); },
                  [](VM& vm, const Val& self, Args args) {
                      self.host<Bytes>()->little = vm.toString(args[0]) == "littleEndian";
                      return Val();
                  })
        .getter("length", [](VM&, const Val& self, Args) { return Val::number(static_cast<double>(self.host<Bytes>()->bytes.size())); })
        .property("position", [](VM&, const Val& self, Args) { return Val::number(static_cast<double>(self.host<Bytes>()->position)); },
                  [](VM&, const Val& self, Args args) {
                      self.host<Bytes>()->position = args[0].toUint32();
                      return Val();
                  })
        .define();
}

const char* kImageClasses = R"js(
(function () {
    class BitmapData {
        constructor(...args) { if (new.target === BitmapData) this.$construct(...args); }
        $construct(width, height, transparent = true, fill = 0xFFFFFFFF) {
            this.w = width | 0;
            this.h = height | 0;
            this.pixels = new Uint32Array(this.w * this.h).fill(fill >>> 0);
        }
        get width() { return this.w; }
        get height() { return this.h; }
        getPixel32(x, y) {
            x |= 0; y |= 0;
            return x < 0 || y < 0 || x >= this.w || y >= this.h ? 0 : this.pixels[y * this.w + x];
        }
        setPixel32(x, y, color) {
            x |= 0; y |= 0;
            if (x >= 0 && y >= 0 && x < this.w && y < this.h) this.pixels[y * this.w + x] = color >>> 0;
        }
    }
    class ByteArray {
        constructor(...args) {
            if (new.target === ByteArray) this.$construct(...args);
            // b[i], as a ByteArray has it.
            return new Proxy(this, {
                get(target, key, receiver) {
                    if (typeof key === "string" && /^[0-9]+$/.test(key)) return target.bytes[Number(key)];
                    const value = Reflect.get(target, key, receiver);
                    return typeof value === "function" ? value.bind(target) : value;
                },
                set(target, key, value) {
                    if (typeof key === "string" && /^[0-9]+$/.test(key)) {
                        const i = Number(key);
                        while (target.bytes.length < i) target.bytes.push(0);
                        target.bytes[i] = value & 0xFF;
                    } else {
                        target[key] = value;
                    }
                    return true;
                },
            });
        }
        $construct() { this.bytes = []; this.position = 0; this.endian = "bigEndian"; }
        put(b) {
            while (this.bytes.length < this.position) this.bytes.push(0);
            this.bytes[this.position++] = b & 0xFF;
        }
        take() { return this.position < this.bytes.length ? this.bytes[this.position++] : 0; }
        writeByte(value) { this.put(value); }
        readUnsignedByte() { return this.take(); }
        readByte() { return (this.take() << 24) >> 24; }
        writeInt(v) {
            for (let i = 0; i < 4; i++) this.put(this.endian === "littleEndian" ? v >>> (8 * i) : v >>> (24 - 8 * i));
        }
        writeUnsignedInt(v) { this.writeInt(v); }
        readUnsignedInt() {
            let v = 0;
            for (let i = 0; i < 4; i++) {
                const b = this.take();
                v |= this.endian === "littleEndian" ? b << (8 * i) : b << (24 - 8 * i);
            }
            return v >>> 0;
        }
        readInt() { return this.readUnsignedInt() | 0; }
        writeUTFBytes(text) { for (const b of new TextEncoder().encode(String(text))) this.put(b); }
        writeBytes(from, offset = 0, length = 0) {
            const bytes = from.bytes;
            if (length === 0 || offset + length > bytes.length) length = Math.max(0, bytes.length - offset);
            for (const b of bytes.slice(offset, offset + length)) this.put(b);
        }
        get bytesAvailable() { return Math.max(0, this.bytes.length - this.position); }
        get length() { return this.bytes.length; }
    }
    as3.register("flash.display.BitmapData", BitmapData);
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
    import flash.events.Event;

    public class Ball extends Sprite {
        public static const RADIUS:int = 10;
        public var speed:Number;
        private var hits:int;

        public function Ball(speed:Number = 2) {
            this.speed = speed;
            addEventListener(Event.ENTER_FRAME, move);
        }

        // Each frame, a step to the right.
        private function move(e:Event):void {
            x += speed;
            if (x > 400) hits++;
        }

        public function get bounces():int { return hits; }
    }
}
)");
        expect(out.ok, out.errorText());
        expect(out.files.count("com/example/Ball.js") == 1, "no com/example/Ball.js");
        const std::string& text = out.files.at("com/example/Ball.js");
        expectIn(text, "as3.define(\"com.example.Ball\", () => {");
        expectIn(text, "const Sprite = as3.use(\"flash.display.Sprite\");");
        expectIn(text, "let Event;");
        expectIn(text, "class Ball extends Sprite {");
        expectIn(text, "            this.speed = NaN;\n            this.hits = 0;\n");
        expectIn(text, "$construct(speed = 2) {\n            super.$construct();");
        expectIn(text, "this.addEventListener(Event.ENTER_FRAME, as3.bind(this, \"move\"));");
        expectIn(text, "        // Each frame, a step to the right.\n        move(e) {");
        expectIn(text, "this.x += this.speed;");
        expectIn(text, "this.hits = (this.hits + 1) | 0;");
        expectIn(text, "get bounces() {");
        expectIn(text, "Ball.RADIUS = 10;");
        expectIn(text, "Event = as3.use(\"flash.events.Event\");");
        if (std::getenv("AS3_JS_DUMP")) std::cout << text << std::endl;
    });

    suite.addTest("A timeline's frame scripts", []() {
        js::Timeline timeline;
        timeline.className = js::timelineClassName("movie");
        timeline.instances = {{"playButton", "flash.display.SimpleButton"}, {"score", "flash.text.TextField"}};
        timeline.frames = {
            {0, "import flash.events.MouseEvent;\n"
                "var clicks:int = 0;\n"
                "var label:String;\n"
                "function onClick(e:MouseEvent):void { clicks++; score.text = 'clicks ' + clicks; }\n"
                "playButton.addEventListener(MouseEvent.CLICK, onClick);\n"
                "trace('frame one', clicks, label, playButton.name, getTimer() >= 0);\n"
                "stop();"},
            {4, "trace('frame five', clicks); for (var i:int = 0; i < 2; i++) clicks += i; trace(clicks);"},
        };
        js::Timeline symbol;
        symbol.className = "Star";
        const js::Output out = js::translate({}, {timeline, symbol});
        expect(out.ok, out.errorText());
        expect(out.classes == std::vector<std::string>{"movie_fla.MainTimeline", "Star"}, "classes");
        const std::string& text = out.files.at("movie_fla/MainTimeline.js");
        expectIn(text, "as3.define(\"movie_fla.MainTimeline\", () => {");
        expectIn(text, "class MainTimeline extends MovieClip {");
        expectIn(text, "this.addFrameScript(0, as3.bind(this, \"frame1\"), 4, as3.bind(this, \"frame5\"));");
        expectIn(text, "this.playButton.addEventListener(MouseEvent.CLICK, as3.bind(this, \"onClick\"));");
        expectIn(text, "this.stop();");
        expectIn(out.files.at("Star.js"), "class Star extends MovieClip {");
        if (!nodeThere()) return;
        // A MovieClip that places its instances when constructed, as the
        // runtime does, and plays the frames it is asked to.
        const std::string runtime = R"js(
class EventDispatcher {
    constructor(...args) { if (new.target === EventDispatcher) this.$construct(...args); }
    $construct() { this.listeners = {}; }
    addEventListener(type, listener) { (this.listeners[type] = this.listeners[type] || []).push(listener); }
    dispatchEvent(event) { for (const listener of this.listeners[event.type] || []) listener(event); }
}
class DisplayObject extends EventDispatcher {
    constructor(...args) { super(); if (new.target === DisplayObject) this.$construct(...args); }
    $construct() { super.$construct(); this.name = null; }
}
class SimpleButton extends DisplayObject {
    constructor(...args) { super(); if (new.target === SimpleButton) this.$construct(...args); }
}
class TextField extends DisplayObject {
    constructor(...args) { super(); if (new.target === TextField) this.$construct(...args); }
    $construct() { super.$construct(); this.text = ""; }
}
class MovieClip extends DisplayObject {
    constructor(...args) { super(); if (new.target === MovieClip) this.$construct(...args); }
    $construct() {
        super.$construct();
        this.scripts = [];
        this.playButton = new SimpleButton(); this.playButton.name = "playButton";
        this.score = new TextField(); this.score.name = "score";
    }
    addFrameScript(...pairs) { for (let i = 0; i < pairs.length; i += 2) this.scripts[pairs[i]] = pairs[i + 1]; }
    stop() { as3.trace("stopped"); }
    playFrame(n) { if (this.scripts[n]) this.scripts[n](); }
}
class MouseEvent { constructor(type) { this.type = type; } }
MouseEvent.CLICK = "click";
as3.register("flash.events.EventDispatcher", EventDispatcher);
as3.register("flash.display.DisplayObject", DisplayObject);
as3.register("flash.display.SimpleButton", SimpleButton);
as3.register("flash.text.TextField", TextField);
as3.register("flash.display.MovieClip", MovieClip);
as3.register("flash.events.MouseEvent", MouseEvent);
)js";
        const std::string play =
            "const Main = as3.getDefinitionByName(\"movie_fla.MainTimeline\");\n"
            "const clip = new Main();\n"
            "clip.playFrame(0);\n"
            "clip.playButton.dispatchEvent(new (as3.use(\"flash.events.MouseEvent\"))(\"click\"));\n"
            "clip.playButton.dispatchEvent(new (as3.use(\"flash.events.MouseEvent\"))(\"click\"));\n"
            "as3.trace(clip.score.text);\n"
            "clip.playFrame(4);\n"
            "const star = new (as3.use(\"Star\"))();\n"
            "as3.trace(as3.getQualifiedClassName(clip), as3.getQualifiedClassName(star), star instanceof as3.use(\"flash.display.MovieClip\"));\n";
        const std::vector<std::string> got = runInNode({out.bundle + play}, runtime);
        expect(got[0] == "frame one 0 null playButton true\nstopped\nclicks 2\nframe five 2\n3\n"
                         "movie_fla::MainTimeline Star true\n",
               "got:\n" + got[0]);
    });

    suite.addTest("Timers run as the VM runs them", []() {
        const std::string program =
            "import flash.utils.*;\n"
            "var log:Array = [];\n"
            "var id:uint = setInterval(function(tag:String):void { log.push(tag + getTimer()); if (log.length == 3) clearInterval(id); }, 100, 'i');\n"
            "setTimeout(function():void { log.push('t'); trace(log); }, 450);\n"
            "var never:uint = setTimeout(function():void { log.push('never'); }, 200); clearTimeout(never);\n";
        VM vm;
        std::string wanted;
        vm.defineFunction("trace", [&wanted](VM& vm, const Val&, Args args) {
            for (size_t i = 0; i < args.size(); ++i) wanted += (i ? " " : "") + vm.toString(args[i]);
            wanted += "\n";
            return Val();
        });
        double now = 0;
        vm.setClock([&now]() { return now; });
        if (!vm.load(program, "timers.as")) throw std::runtime_error(vm.getError());
        for (now = 0; now <= 1000; now += 50) vm.runTimers(now);
        const js::Output out = translated(program);
        expect(out.ok, out.errorText());
        if (!nodeThere()) return;
        const std::vector<std::string> got = runInNode(
            {out.bundle + "for (let now = 0; now <= 1000; now += 50) { clock = now; as3.runTimers(now); }\n"},
            "var clock = 0; as3.clock = () => clock;\n");
        expect(got[0] == wanted && !wanted.empty(), "VM:\n" + wanted + "JavaScript:\n" + got[0]);
    });

    suite.addTest("Source maps lead back to the lines of the source", []() {
        const js::Output out = translated("package p {\n"
                                          "    public class A {\n"
                                          "        public function f():Number {\n"
                                          "            var x:int = 1;\n"
                                          "            return x + 1;\n"
                                          "        }\n"
                                          "    }\n"
                                          "}\n"
                                          "trace(new p.A().f());\n");
        expect(out.ok, out.errorText());
        expect(out.sourceMaps.count("p/A.js.map") == 1 && out.sourceMaps.count("bundle.js.map") == 1, "no maps");
        const std::string& map = out.sourceMaps.at("p/A.js.map");
        expectIn(map, "\"version\":3");
        expectIn(map, "\"sources\":[\"main.as\"]");
        expectIn(out.files.at("p/A.js"), "//# sourceMappingURL=A.js.map");
        if (!nodeThere()) return;
        // Decoded: the generated line of "let x = 1;" maps to line 4, and
        // "return x + 1;" to line 5.
        const std::string check = std::string(R"js(
const map = JSON.parse(MAP);
const lines = FILE.split("\n");
const digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
function decode(text) {
    const out = []; let value = 0, shift = 0;
    for (const c of text) {
        let digit = digits.indexOf(c);
        value += (digit & 31) << shift;
        if (digit & 32) { shift += 5; continue; }
        out.push(value & 1 ? -(value >> 1) : value >> 1); value = 0; shift = 0;
    }
    return out;
}
let source = 0, line = 0; const found = {};
map.mappings.split(";").forEach((segment, i) => {
    if (!segment) return;
    const fields = decode(segment.split(",")[0]);
    source += fields[1]; line += fields[2];
    found[lines[i].trim()] = line + 1;
});
as3.trace(found["let x = 1;"], found["return x + 1;"], found["class A {"]);
)js");
        const std::string script = "const MAP = " + js::quote(map) + ";\nconst FILE = " + js::quote(out.files.at("p/A.js")) + ";\n" + check;
        const std::vector<std::string> got = runInNode({script});
        expect(got[0] == "4 5 2\n", "got " + got[0]);
    });

    suite.addTest("The ABC compiler's sources translate to JavaScript that parses", []() {
        std::vector<std::string> checks;
        std::string problems;
        for (const auto& program : testPrograms("abc_compiler_tests.cpp", "compiled")) {
            const js::Output out = translated(program.second);
            if (!out.ok) {
                problems += program.first + ":\n" + out.errorText();
                continue;
            }
            // Parsed, not run: they use flash.* classes.
            checks.push_back("new (require(\"vm\").Script)(" + js::quote(out.bundle) + "); as3.trace(\"parsed\");");
        }
        expect(problems.empty(), "\n" + problems);
        expect(checks.size() >= 5, "only " + std::to_string(checks.size()) + " sources");
        if (!nodeThere()) return;
        std::vector<std::string> got = runInNode(checks, "globalThis.require = require;\n");
        for (size_t i = 0; i < got.size(); ++i) {
            if (got[i] != "parsed\n") problems += got[i];
        }
        expect(problems.empty(), "\n" + problems);
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

    suite.addTest("as3corelib's JPGEncoder makes the same bytes", []() {
        const fs::path images = fs::path(kPaint) / "as3corelib-.93/src/com/adobe/images";
        if (!fs::exists(images / "JPGEncoder.as") || !nodeThere()) {
            std::cout << "    (sources or node not found; skipped)" << std::endl;
            return;
        }
        const std::string program =
            "import com.adobe.images.JPGEncoder;\n"
            "import flash.display.BitmapData;\n"
            "import flash.utils.ByteArray;\n"
            "var image:BitmapData = new BitmapData(40, 24, false, 0xFF336699);\n"
            "for (var y:int = 0; y < 24; y++) for (var x:int = 0; x < 40; x++) image.setPixel32(x, y, 0xFF000000 | (x * 6) << 16 | (y * 10) << 8 | ((x * y) & 0xFF));\n"
            "for each (var quality:int in [10, 50, 80, 100]) {\n"
            "    var jpeg:ByteArray = new JPGEncoder(quality).encode(image);\n"
            "    jpeg.position = 0; var sum:uint = 0; var hex:String = '';\n"
            "    for (var i:int = 0; i < jpeg.length; i++) { var b:int = jpeg.readUnsignedByte(); sum = (sum * 31 + b) >>> 0; if (i < 12) hex += (b < 16 ? '0' : '') + b.toString(16); }\n"
            "    trace(quality, jpeg.length, sum, hex);\n"
            "}\n";
        VM vm;
        defineImageClasses(vm);
        std::string wanted;
        vm.defineFunction("trace", [&wanted](VM& vm, const Val&, Args args) {
            for (size_t i = 0; i < args.size(); ++i) wanted += (i ? " " : "") + vm.toString(args[i]);
            wanted += "\n";
            return Val();
        });
        vm.setClassResolver([images](const std::string& name) -> std::optional<ClassSource> {
            const std::string prefix = "com.adobe.images.";
            if (name.compare(0, prefix.size(), prefix) != 0) return std::nullopt;
            const fs::path path = images / (name.substr(prefix.size()) + ".as");
            if (!fs::exists(path)) return std::nullopt;
            return ClassSource{readText(path), path.filename().string()};
        });
        if (!vm.load(program, "jpeg.as")) throw std::runtime_error(vm.getError());
        const js::Output out = js::translate({{readText(images / "JPGEncoder.as"), "JPGEncoder.as"},
                                              {readText(images / "BitString.as"), "BitString.as"},
                                              {program, "jpeg.as"}});
        expect(out.ok, out.errorText());
        const std::vector<std::string> got = runInNode({out.bundle}, kImageClasses);
        expect(got[0] == wanted, "VM:\n" + wanted + "JavaScript:\n" + got[0]);
        std::cout << "    " << wanted.substr(0, wanted.find('\n')) << std::endl;
    });

    suite.addTest("as3corelib translates, and its utilities run as in the VM", []() {
        const fs::path root = fs::path(kPaint) / "as3corelib-.93/src";
        if (!fs::exists(root / "com/adobe/crypto/MD5.as")) {
            std::cout << "    (sources not found; skipped)" << std::endl;
            return;
        }
        // Every source but AIR's.
        std::vector<js::Source> sources;
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".as") continue;
            if (entry.path().string().find("air") != std::string::npos) continue;
            sources.push_back({readText(entry.path()), entry.path().filename().string()});
        }
        std::sort(sources.begin(), sources.end(), [](const js::Source& a, const js::Source& b) { return a.fileName < b.fileName; });
        const js::Output all = js::translate(sources);
        if (const char* dump = std::getenv("AS3_JS_DUMP")) {
            for (const auto& file : all.files) writeText(fs::path(dump) / "corelib" / file.first, file.second);
        }
        std::string problems;
        for (const auto& diagnostic : all.diagnostics) {
            if (diagnostic.isError) problems += diagnostic.text() + "\n";
        }
        expect(all.ok && problems.empty(), "\n" + problems);
        std::cout << "    " << sources.size() << " files, " << all.classes.size() << " classes" << std::endl;
        if (!nodeThere()) return;

        const std::string program =
            "import com.adobe.crypto.MD5;\n"
            "import com.adobe.crypto.SHA1;\n"
            "import com.adobe.crypto.SHA224;\n"
            "import com.adobe.crypto.SHA256;\n"
            "import com.adobe.serialization.json.JSON;\n"
            "import com.adobe.utils.StringUtil;\n"
            "import com.adobe.utils.ArrayUtil;\n"
            "import com.adobe.utils.IntUtil;\n"
            "import com.adobe.utils.NumberFormatter;\n"
            "import com.adobe.net.URI;\n"
            "trace(MD5.hash('abc'), MD5.hash(''), MD5.hash('The quick brown fox jumps over the lazy dog'));\n"
            "trace(SHA1.hash('abc'), SHA224.hash('abc'), SHA256.hash('abc'));\n"
            "trace(SHA256.hash('a longer message, past one block of sixty-four bytes, to hash'), SHA1.hash('\\u00e9t\\u00e9'));\n"
            "trace(JSON.encode({a: [1, 2.5, 'x\"y'], b: true, c: null}), JSON.decode('{\"n\": [1, {\"m\": \"t\\\\u0041\"}]}').n[1].m);\n"
            "trace(JSON.decode('[1, -2.5e3, true, false, null, \"s\"]'), JSON.encode('tab\\tquote\"'));\n"
            "trace(StringUtil.trim('  x  '), StringUtil.replace('aXbXc', 'X', '-'), StringUtil.beginsWith('hello', 'he'), StringUtil.endsWith('hello', 'lo'), StringUtil.remove('banana', 'an'));\n"
            "trace(IntUtil.toHex(255, true), IntUtil.toHex(255), IntUtil.rol(1, 3), IntUtil.ror(8, 3));\n"
            "trace(ArrayUtil.arrayContainsValue([1, 2, 3], 2), ArrayUtil.arraysAreEqual([1, 2], [1, 2]), ArrayUtil.copyArray([1, [2]]).length);\n"
            "trace(NumberFormatter.addLeadingZero(5), NumberFormatter.addLeadingZero(12));\n";
        VM vm;
        defineImageClasses(vm);
        std::string wanted;
        vm.defineFunction("trace", [&wanted](VM& vm, const Val&, Args args) {
            for (size_t i = 0; i < args.size(); ++i) wanted += (i ? " " : "") + vm.toString(args[i]);
            wanted += "\n";
            return Val();
        });
        vm.defineNativeClass("flash.utils.Endian").constant("BIG_ENDIAN", Val::string("bigEndian"))
            .constant("LITTLE_ENDIAN", Val::string("littleEndian")).define();
        vm.setClassResolver([root](const std::string& name) -> std::optional<ClassSource> {
            std::string path = name;
            std::replace(path.begin(), path.end(), '.', '/');
            const fs::path file = root / (path + ".as");
            if (!fs::exists(file)) return std::nullopt;
            return ClassSource{readText(file), file.filename().string()};
        });
        if (!vm.load(program, "corelib.as")) wanted += "Error: " + vm.getError() + "\n";
        std::vector<js::Source> withProgram = sources;
        withProgram.push_back({program, "corelib.as"});
        const js::Output out = js::translate(withProgram);
        expect(out.ok, out.errorText());
        const std::vector<std::string> got = runInNode(
            {out.bundle}, std::string(kImageClasses) + "as3.register(\"flash.utils.Endian\", { BIG_ENDIAN: \"bigEndian\", LITTLE_ENDIAN: \"littleEndian\" });\n");
        expect(got[0] == wanted && wanted.find("Error") == std::string::npos, "VM:\n" + wanted + "JavaScript:\n" + got[0]);
        if (std::getenv("AS3_JS_DUMP")) std::cout << wanted;
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
