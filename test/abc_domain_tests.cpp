#include "test_runner.h"
#include "swf_reader.h"
#include "../src/abc/abc.h"
#include "../src/abc/abc_domain.h"
#include "../src/parser/parser.h"
#include "../src/bytecode/compiler.h"
#include "../src/bytecode/vm.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace as3;
using namespace as3test;

// ABC run on the VM (abc::Domain): programs compiled to ABC and run there
// give what the same source run directly gives; classes from ABC extend
// native ones and are extended by source; and the ABC of published movies
// loads and makes its classes.
TEST_SUITE(AbcDomain)

namespace {

namespace fs = std::filesystem;

void defineTrace(VM& vm, std::string& out) {
    vm.defineFunction("trace", [&out](VM& vm, const Val&, Args args) {
        for (size_t i = 0; i < args.size(); ++i) {
            out += (i ? " " : "") + vm.toString(args[i]);
        }
        out += "\n";
        return Val();
    });
}

// The source run on the VM directly.
std::string runSource(const std::string& code) {
    VM vm;
    std::string out;
    defineTrace(vm, out);
    if (!vm.load(code, "Test.as")) {
        throw std::runtime_error("source: " + vm.getError() + "\n" + vm.getErrorStack() + "\nOutput so far:\n" + out);
    }
    return out;
}

std::vector<uint8_t> compiledAbc(const std::vector<abc::Source>& sources,
                                 const std::vector<abc::Timeline>& timelines = {}) {
    abc::Output out = abc::compile(sources, timelines);
    if (!out.ok) throw std::runtime_error("did not compile:\n" + out.errorText());
    return out.abc;
}

// The source compiled to ABC and that run.
std::string runAbc(const std::string& code) {
    const std::vector<uint8_t> bytes = compiledAbc({{code, "Test.as"}});
    VM vm;
    std::string out;
    defineTrace(vm, out);
    abc::Domain domain(vm);
    std::string error;
    if (!domain.load(bytes, error)) throw std::runtime_error("does not load: " + error);
    try {
        domain.runEntry();
    } catch (ScriptException& exception) {
        throw std::runtime_error("ABC: " + vm.toString(exception.value) + "\n" + domain.takeErrorStack() +
                                 "Output so far:\n" + out);
    }
    return out;
}

// Both ways, the same.
void same(const std::string& code) {
    const std::string direct = runSource(code);
    const std::string viaAbc = runAbc(code);
    if (direct != viaAbc) {
        throw std::runtime_error("run directly:\n" + direct + "run as ABC:\n" + viaAbc);
    }
    if (direct.empty()) throw std::runtime_error("nothing traced");
}

void expect(const std::string& got, const std::string& wanted) {
    if (got != wanted) throw std::runtime_error("got \"" + got + "\", wanted \"" + wanted + "\"");
}

std::vector<uint8_t> readBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Each DoABC block of a movie, and of the movies it holds as binary data.
void abcBlocks(const std::vector<uint8_t>& movie, std::vector<std::vector<uint8_t>>& blocks, int depth = 0) {
    for (const SwfTag& tag : swfTags(movie)) {
        if (tag.code == 82) {
            blocks.push_back(doAbcBytes(tag));
        } else if (tag.code == 72) {
            blocks.push_back(tag.data);
        } else if (tag.code == 87 && tag.data.size() > 9 && depth < 3 && tag.data[7] == 'W' && tag.data[8] == 'S') {
            abcBlocks(std::vector<uint8_t>(tag.data.begin() + 6, tag.data.end()), blocks, depth + 1);
        }
    }
}

// A VM whose player classes (flash.*) are empty ones made on demand, the
// stand-ins a movie's ABC needs to make its classes.
void standIns(VM& vm) {
    vm.setClassResolver([](const std::string& name) -> std::optional<ClassSource> {
        if (name.rfind("flash.", 0) != 0 && name.rfind("flashx.", 0) != 0) return std::nullopt;
        const size_t dot = name.find_last_of('.');
        const std::string package = name.substr(0, dot);
        const std::string shortName = name.substr(dot + 1);
        if (shortName.empty() || !std::isupper(static_cast<unsigned char>(shortName[0]))) return std::nullopt;
        std::string base;
        if (shortName == "EventDispatcher") base = "";
        else if (shortName == "DisplayObject") base = "flash.events.EventDispatcher";
        else if (shortName == "InteractiveObject") base = "flash.display.DisplayObject";
        else if (shortName == "DisplayObjectContainer") base = "flash.display.InteractiveObject";
        else if (shortName == "Sprite") base = "flash.display.DisplayObjectContainer";
        else if (shortName == "MovieClip") base = "flash.display.Sprite";
        else if (shortName == "Event") base = "";
        else if (shortName.size() > 5 && shortName.compare(shortName.size() - 5, 5, "Event") == 0) base = "flash.events.Event";
        std::string source = "package " + package + " { public dynamic class " + shortName +
                             (base.empty() ? "" : " extends " + base) + " { public function " + shortName +
                             "(...rest) {} } }";
        return ClassSource{source, shortName + ".as"};
    });
}

} // namespace

static void init_abc_domain_tests() {
    auto& suite = suite_AbcDomain;

    // --- The same program, directly and as ABC ---

    suite.addTest("Arithmetic, strings and conversions", []() {
        same(R"(
var a:int = 7, b:Number = 2.5, c:uint = 3;
trace(a + b, a - b, a * b, a / 2, a % 3, -a, a << 2, a >> 1, -9 >>> 28, a & 3, a | 8, a ^ 5, ~a);
var i:int = 2147483647; i++; trace(i);
var u:uint = 0; u--; trace(u);
trace("x" + 1 + 2, 1 + 2 + "x", "5" * "2", int("12abc"), parseInt("ff", 16), Number("3.5") + 1);
trace(String(null), String(undefined), Boolean(""), Boolean("0"), 10 / 0, -10 / 0, isNaN(0 / 0));
var s:String = "Hello, World";
trace(s.length, s.charAt(4), s.indexOf("o"), s.toUpperCase(), s.split(", ").join("|"), s.substr(7, 3));
trace(typeof a, typeof s, typeof null, typeof undefined, typeof {}, typeof trace);
trace(3 == "3", 3 === "3", null == undefined, null === undefined, NaN == NaN);
trace(1 < 2, "a" < "b", 2 <= 2, 3 > 4, "10" > 9, 5 >= 5);
)");
    });

    suite.addTest("Control flow: loops, switch, labels", []() {
        same(R"(
var out:Array = [];
for (var i:int = 0; i < 5; i++) { if (i == 3) continue; out.push(i); }
var j:int = 10; while (j > 7) j--; out.push(j);
do { j += 5; } while (j < 20); out.push(j);
outer: for (var a:int = 0; a < 3; a++) { for (var b:int = 0; b < 3; b++) { if (b == 2) continue outer; if (a == 2) break outer; out.push(a * 10 + b); } }
trace(out.join(","));
function kind(n:*):String {
    switch (n) {
        case 1: return "one";
        case 2: case 3: return "few";
        case "x": return "ex";
        default: return "many";
    }
}
trace(kind(1), kind(3), kind("x"), kind(9));
var t:int = 0;
for each (var v:int in [1, 2, 3]) t += v;
trace(t, t > 5 ? "big" : "small", t && "and", 0 || "or");
)");
    });

    suite.addTest("Functions: closures, defaults, rest and arguments", []() {
        same(R"(
function add(a:int, b:int = 10):int { return a + b; }
function all(first:String, ...rest):String { return first + ":" + rest.length + ":" + rest.join("+"); }
function count():int { return arguments.length; }
trace(add(1), add(1, 2), all("a"), all("a", 1, 2, 3), count(), count(1, 2));
function counter():Function { var n:int = 0; return function():int { return ++n; }; }
var c1:Function = counter(), c2:Function = counter();
c1(); c1();
trace(c1(), c2());
var fs:Array = [];
for (var i:int = 0; i < 3; i++) fs.push(function():int { return i; });
trace(fs[0](), fs[2]());
function fact(n:int):Number { return n <= 1 ? 1 : n * fact(n - 1); }
trace(fact(10), [3, 1, 2].sort().join(), [3, 1, 2].sort(function(a:int, b:int):int { return b - a; }).join());
var o:Object = {f: function():String { return "m" + this.v; }, v: 4};
trace(o.f(), add.call(null, 5, 6), add.apply(null, [7]));
)");
    });

    suite.addTest("Classes: inheritance, super, statics, accessors", []() {
        same(R"(
package shapes {
    public class Shape {
        public static var made:int = 0;
        public static const KIND:String = "shape";
        protected var _name:String;
        private var secret:int = 42;
        public function Shape(name:String) { _name = name; made++; }
        public function get name():String { return _name; }
        public function set name(value:String):void { _name = value.toUpperCase(); }
        public function area():Number { return 0; }
        public function describe():String { return _name + " " + area() + " " + secret; }
        public static function count():int { return made; }
    }
}
package shapes {
    public class Square extends Shape {
        private var side:Number;
        private var secret:String = "mine";
        public function Square(side:Number) { super("square"); this.side = side; }
        override public function area():Number { return side * side; }
        override public function describe():String { return "[" + super.describe() + "] " + secret; }
    }
}
import shapes.*;
var s:Square = new Square(3);
var b:Shape = new Shape("blob");
trace(s.describe(), b.describe(), Shape.count(), Shape.KIND, s is Shape, b is Square, s as Shape != null);
s.name = "box";
trace(s.name, s.area(), Shape.made);
)");
    });

    suite.addTest("Interfaces, dynamic classes and for-in", []() {
        same(R"(
package {
    public interface Named { function label():String; }
}
package {
    public dynamic class Bag implements Named {
        public var fixed:int = 1;
        public function label():String { return "bag"; }
    }
}
var bag:Bag = new Bag();
bag.extra = 2;
bag["more"] = 3;
var keys:Array = [];
for (var k:String in bag) keys.push(k + "=" + bag[k]);
keys.sort();
var n:Named = bag;
trace(keys.join(","), n.label(), bag is Named, "extra" in bag, "fixed" in bag, "none" in bag);
delete bag.extra;
trace("extra" in bag, bag.hasOwnProperty("more"));
var o:Object = {a: 1, b: "two"};
var sum:Array = [];
for (var key:String in o) sum.push(key);
sum.sort();
for each (var value:* in o) sum.push(typeof value);
trace(sum.join(" "));
)");
    });

    suite.addTest("Exceptions: throw, catch by type, finally", []() {
        same(R"(
package {
    public class Oops extends Error {
        public function Oops(message:String) { super(message); }
    }
}
function risky(n:int):String {
    try {
        if (n == 1) throw new Oops("one");
        if (n == 2) throw new TypeError("two");
        if (n == 3) throw "three";
        return "fine";
    } catch (e:Oops) {
        return "oops " + e.message;
    } catch (e:TypeError) {
        return "type " + e.message;
    } catch (e:*) {
        return "other " + e;
    } finally {
        trace("finally", n);
    }
    return "never";
}
trace(risky(0), risky(1), risky(2), risky(3));
try { var x:Object = null; x.y = 1; } catch (e:Error) { trace(e is TypeError, e.errorID); }
try { undefinedName(); } catch (e:ReferenceError) { trace("reference", e.errorID); }
)");
    });

    suite.addTest("Arrays, vectors, objects and Math", []() {
        same(R"(
var a:Array = [5, 3, 8];
a.push(1); a.unshift(0);
trace(a.length, a.join("-"), a.indexOf(8), a.slice(1, 3), a.concat([9]).length, a.reverse());
var v:Vector.<int> = new Vector.<int>();
v.push(3); v.push(4.7);
trace(v.length, v[1], v.join(), v is Vector.<int>);
var m:Object = {};
m["k" + 1] = "v1";
trace(m.k1, Math.max(3, 9, 2), Math.floor(-2.5), Math.abs(-4), Math.round(2.5), Math.sqrt(16));
var nested:Array = [[1, 2], [3, [4, 5]]];
trace(nested[1][1][0], nested.length, String(nested));
var d:Date = new Date(2020, 0, 15);
trace(d.getFullYear(), d.getMonth(), d.getDate());
)");
    });

    suite.addTest("Static initializers and class constants", []() {
        same(R"(
package util {
    public class Table {
        public static const NAMES:Array = ["a", "b", "c"];
        public static var lookup:Object = make();
        private static function make():Object {
            var o:Object = {};
            for (var i:int = 0; i < NAMES.length; i++) o[NAMES[i]] = i;
            return o;
        }
        public static function find(name:String):int { return lookup[name]; }
    }
}
import util.Table;
trace(Table.find("b"), Table.NAMES.length, Table.lookup.c);
)");
    });

    suite.addTest("Package functions and variables", []() {
        same(R"(
package tools {
    public function double(n:Number):Number { return n * 2; }
}
package tools {
    public var level:int = 3;
}
import tools.*;
trace(double(4), level);
level = 5;
trace(level, double(level));
)");
    });

    suite.addTest("Methods: closures over this, statics of superclasses, overridden accessors", []() {
        same(R"(
package zoo {
    public class Animal {
        protected static var count:int = 0;
        public static const LEGS:int = 4;
        private var _sound:String;
        public function Animal(sound:String) { _sound = sound; count++; }
        public function get sound():String { return _sound; }
        public function speaker():Function { return function(times:int):String { var s:String = ""; for (var i:int = 0; i < times; i++) s += sound; return s; }; }
        public function later(list:Array):void { list.forEach(function(item:*, i:int, a:Array):void { a[i] = sound + item; }); }
    }
}
package zoo {
    public class Dog extends Animal {
        public function Dog() { super("woof"); }
        override public function get sound():String { return super.sound.toUpperCase(); }
        public function census():String { return count + " animals, " + LEGS + " legs"; }
    }
}
import zoo.*;
var d:Dog = new Dog();
new Animal("moo");
var list:Array = [1, 2];
d.later(list);
trace(d.speaker()(2), list.join(), d.census(), d.sound);
)");
    });

    suite.addTest("Loops with finally, strings, regular expressions and dictionaries", []() {
        same(R"(
function find(items:Array, wanted:*):int {
    for (var i:int = 0; i < items.length; i++) {
        try {
            if (items[i] == wanted) return i;
        } finally {
            trace("looked at", i);
        }
    }
    return -1;
}
trace(find(["a", "b", "c"], "b"));
var text:String = "The quick brown fox";
trace(text.replace(/quick/, "slow"), text.match(/o\w/g).join("|"), text.search(/brown/), /^the/i.test(text));
trace(text.charCodeAt(0), String.fromCharCode(72, 105), text.lastIndexOf("o"), text.slice(-3), text.split(" ").length);
var words:Object = {};
for each (var w:String in text.toLowerCase().split(" ")) words[w.charAt(0)] = (words[w.charAt(0)] || 0) + 1;
var letters:Array = [];
for (var k:String in words) letters.push(k + words[k]);
letters.sort();
trace(letters.join(","));
var n:Number = 1234.5678;
trace(n.toFixed(2), int(n), Math.round(n * 10) / 10, (255).toString(16), parseFloat("3.25e2"));
var u:uint = 0xFFFFFFFF;
trace(u, u + 1, int(u), u >> 4, u >>> 4);
var count:int = 0;
while (true) { if (++count > 4) break; }
trace(count, count is int, 2.5 is int, null is Object);
)");
    });

    // --- Mixing with the VM's own classes ---

    suite.addTest("An ABC class extends a native one and overrides it", []() {
        VM vm;
        std::string out;
        defineTrace(vm, out);
        struct State : HostObject {
            int made = 0;
        };
        vm.defineNativeClass("host.Widget")
            .constructor([](VM&, const Val& self, Args args) {
                auto state = std::make_shared<State>();
                state->made = args.integer(0, -1);
                self->host = state;
                return Val();
            })
            .method("describe", [](VM& vm, const Val& self, Args) {
                // Native code calling a method a subclass overrides.
                return Val::string("widget(" + vm.toString(vm.callMethod(self, "kind")) + ")");
            })
            .method("kind", [](VM&, const Val&, Args) { return Val::string("plain"); })
            .getter("made", [](VM&, const Val& self, Args) {
                State* state = self.host<State>();
                return Val::number(state ? state->made : -2);
            })
            .define();
        const std::vector<uint8_t> bytes = compiledAbc({{R"(
package ui {
    import host.Widget;
    public class Fancy extends Widget {
        public var tag:String;
        public function Fancy(tag:String) { super(7); this.tag = tag; }
        override public function kind():String { return "fancy " + tag; }
        public function both():String { return describe() + " " + made; }
    }
}
)", "Fancy.as"}});
        abc::Domain domain(vm);
        std::string error;
        if (!domain.load(bytes, error)) throw std::runtime_error(error);
        // Found by the VM by name, as the host and scripts look for classes.
        std::shared_ptr<ClassInfo> cls = vm.findClass("ui.Fancy");
        if (!cls) throw std::runtime_error("ui.Fancy not found");
        Val fancy = vm.construct(vm.findDefinition("ui.Fancy"), {Val::string("hat")});
        expect(vm.toString(vm.callMethod(fancy, "both")), "widget(fancy hat) 7");
        expect(vm.toString(vm.callMethod(fancy, "describe")), "widget(fancy hat)");
        // A class from source extending the ABC one, run by the VM.
        if (!vm.load(R"(
package {
    import ui.Fancy;
    public class Fancier extends Fancy {
        public function Fancier() { super("cap"); }
        override public function kind():String { return "fancier:" + super.kind(); }
    }
}
var f:Fancier = new Fancier();
trace(f.both(), f is Fancy, f.tag);
)", "Fancier.as")) {
            throw std::runtime_error(vm.getError());
        }
        expect(out, "widget(fancier:fancy cap) 7 true cap\n");
    });

    suite.addTest("Source code uses ABC classes, and ABC code source classes", []() {
        VM vm;
        std::string out;
        defineTrace(vm, out);
        vm.setClassResolver([](const std::string& name) -> std::optional<ClassSource> {
            if (name != "lib.Helper") return std::nullopt;
            return ClassSource{"package lib { public class Helper { public static function greet(s:String):String "
                               "{ return 'hi ' + s; } } }",
                               "Helper.as"};
        });
        const std::vector<uint8_t> bytes = compiledAbc({{R"(
package app {
    import lib.Helper;
    public class Main {
        public static function run():String { return Helper.greet("abc"); }
        public function toString():String { return "[Main]"; }
    }
}
)", "Main.as"}});
        abc::Domain domain(vm);
        std::string error;
        if (!domain.load(bytes, error)) throw std::runtime_error(error);
        if (!vm.load("import app.Main; trace(Main.run(), new Main(), String(new Main()));", "use.as")) {
            throw std::runtime_error(vm.getError() + "\n" + domain.takeErrorStack());
        }
        expect(out, "hi abc [Main] [Main]\n");
    });

    suite.addTest("A main timeline's frame scripts, as a published movie has them", []() {
        VM vm;
        std::string out;
        defineTrace(vm, out);
        // The player's MovieClip, enough of it: addFrameScript keeps the
        // frames' functions, and the test runs them.
        struct Clip : HostObject {
            std::vector<Val> frames;
        };
        vm.defineNativeClass("flash.events.EventDispatcher").define();
        vm.defineNativeClass("flash.display.DisplayObject").extends("flash.events.EventDispatcher").define();
        vm.defineNativeClass("flash.display.InteractiveObject").extends("flash.display.DisplayObject").define();
        vm.defineNativeClass("flash.display.DisplayObjectContainer").extends("flash.display.InteractiveObject").define();
        vm.defineNativeClass("flash.display.Sprite").extends("flash.display.DisplayObjectContainer").define();
        vm.defineNativeClass("flash.display.MovieClip")
            .extends("flash.display.Sprite")
            .constructor([](VM&, const Val& self, Args) {
                if (!self->host) self->host = std::make_shared<Clip>();
                return Val();
            })
            .method("addFrameScript", [](VM&, const Val& self, Args args) {
                Clip* clip = self.host<Clip>();
                for (size_t i = 0; clip && i + 1 < args.size(); i += 2) {
                    const size_t frame = static_cast<size_t>(args.number(i));
                    if (clip->frames.size() <= frame) clip->frames.resize(frame + 1);
                    clip->frames[frame] = args[i + 1];
                }
                return Val();
            })
            .method("stop", [&out](VM&, const Val&, Args) {
                out += "(stop)\n";
                return Val();
            })
            .define();
        abc::Timeline main;
        main.className = abc::timelineClassName("movie");
        main.instances = {{"hat", "flash.display.MovieClip"}};
        main.frames = {{0, "var count:int = 0;\nfunction bump():int { return ++count; }\ntrace('frame 1', bump());\nstop();"},
                       {1, "trace('frame 2', bump(), hat == null);"}};
        const std::vector<uint8_t> bytes = compiledAbc({}, {main});
        abc::Domain domain(vm);
        std::string error;
        if (!domain.load(bytes, error)) throw std::runtime_error(error);
        try {
            Val root = vm.construct(vm.findDefinition("movie_fla.MainTimeline"));
            Clip* clip = root.host<Clip>();
            if (!clip || clip->frames.size() != 2) throw std::runtime_error("frame scripts not added");
            vm.callFunction(clip->frames[0], root, Args());
            vm.callFunction(clip->frames[1], root, Args());
        } catch (ScriptException& exception) {
            throw std::runtime_error(vm.toString(exception.value) + "\n" + domain.takeErrorStack());
        }
        expect(out, "frame 1 1\n(stop)\nframe 2 2 true\n");
    });

    // --- Published movies ---

    suite.addTest("bunnyPaint's classes compiled to ABC run as their source does", []() {
        const fs::path folder = "D:/Backup/hulabunny/bunnyPaint/as3corelib-.93/src/com/adobe/images";
        if (!fs::exists(folder / "JPGEncoder.as")) {
            std::cout << "    (sources not found; skipped)" << std::endl;
            return;
        }
        // The JPEG encoder over a BitmapData of the test's own.
        const auto readText = [](const fs::path& path) {
            std::ifstream in(path, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        };
        const std::string encoder = readText(folder / "JPGEncoder.as");
        const std::string bits = readText(folder / "BitString.as");
        const char* use = R"(
import com.adobe.images.JPGEncoder;
import flash.display.BitmapData;
var image:BitmapData = new BitmapData(24, 16);
var bytes:* = new JPGEncoder(80).encode(image);
var sum:int = 0;
for (var i:int = 0; i < bytes.length; i++) sum = (sum * 31 + bytes[i]) & 0xFFFFFF;
trace(bytes.length, sum);
)";
        const auto natives = [](VM& vm) {
            // Pixels from their place, and bytes kept in an array.
            vm.defineNativeClass("flash.display.BitmapData")
                .constructor([](VM&, const Val& self, Args args) {
                    self->properties["width"] = Val::number(args.number(0));
                    self->properties["height"] = Val::number(args.number(1));
                    return Val();
                })
                .dynamic()
                .method("getPixel32", [](VM&, const Val&, Args args) {
                    const int x = args.integer(0), y = args.integer(1);
                    return Val::number(((x * 37) & 255) << 16 | ((y * 91) & 255) << 8 | ((x * y) & 255));
                })
                .define();
            vm.load(R"(
package flash.utils {
    public dynamic class ByteArray extends Array {
        public var position:int = 0;
        public function writeByte(b:int):void { this[position++] = b & 255; }
        public function writeShort(s:int):void { writeByte(s >> 8); writeByte(s); }
        public function writeBytes(src:ByteArray, offset:uint = 0, length:uint = 0):void {
            for (var i:int = 0; i < src.length; i++) writeByte(src[i]);
        }
    }
}
)", "ByteArray.as");
        };
        std::string direct, viaAbc;
        {
            VM vm;
            defineTrace(vm, direct);
            natives(vm);
            vm.setClassResolver([&](const std::string& name) -> std::optional<ClassSource> {
                if (name == "com.adobe.images.JPGEncoder") return ClassSource{encoder, "JPGEncoder.as"};
                if (name == "com.adobe.images.BitString") return ClassSource{bits, "BitString.as"};
                return std::nullopt;
            });
            if (!vm.load(use, "use.as")) throw std::runtime_error("direct: " + vm.getError());
        }
        {
            VM vm;
            defineTrace(vm, viaAbc);
            natives(vm);
            abc::Domain domain(vm);
            std::string error;
            if (!domain.load(compiledAbc({{encoder, "JPGEncoder.as"}, {bits, "BitString.as"}}), error)) {
                throw std::runtime_error(error);
            }
            if (!vm.load(use, "use.as")) {
                throw std::runtime_error("ABC: " + vm.getError() + "\n" + domain.takeErrorStack());
            }
            std::cout << "    " << domain.instructionCount() << " ABC instructions" << std::endl;
        }
        expect(viaAbc, direct);
    });

    suite.addTest("A published movie's ABC makes all its classes", []() {
        const fs::path movie = "D:/Backup/hulabunny/bunnyPaint/bunnyPaint2.swf";
        if (!fs::exists(movie)) {
            std::cout << "    (movie not found; skipped)" << std::endl;
            return;
        }
        std::vector<std::vector<uint8_t>> blocks;
        abcBlocks(readBytes(movie), blocks);
        VM vm;
        std::string out;
        defineTrace(vm, out);
        standIns(vm);
        abc::Domain domain(vm);
        for (const auto& block : blocks) {
            std::string error;
            if (!domain.load(block, error)) throw std::runtime_error(error);
        }
        // The text layout framework comes from a shared library, not the
        // movie: what uses it more than by name does not get made here.
        size_t classes = 0;
        std::vector<std::string> failed;
        for (const std::string& name : domain.definitionNames()) {
            try {
                Val found = domain.find(name);
                if (found.isClass()) ++classes;
            } catch (ScriptException& exception) {
                failed.push_back(name + ": " + vm.toString(exception.value));
            }
        }
        for (const std::string& name : failed) {
            if (name.rfind("fl.text.", 0) != 0) throw std::runtime_error(name + "\n" + domain.takeErrorStack());
        }
        std::shared_ptr<ClassInfo> box = vm.findClass("fl.controls.CheckBox");
        if (!box || !box->base || box->base->name != "fl.controls.LabelButton") {
            throw std::runtime_error("CheckBox not made over LabelButton");
        }
        std::shared_ptr<ClassInfo> main = vm.findClass("bunnyPaint2_fla.MainTimeline");
        if (!main || !main->findTrait("frame1")) throw std::runtime_error("MainTimeline has no frame1");
        std::cout << "    " << classes << " classes made, " << failed.size() << " of text layout not" << std::endl;
        if (classes < 50) throw std::runtime_error("only " + std::to_string(classes) + " classes");
    });
}

static struct AbcDomainTestInit {
    AbcDomainTestInit() { init_abc_domain_tests(); }
} abc_domain_test_init;
