#include "test_runner.h"
#include "../src/abc/abc.h"
#include "../src/abc/abc_verifier.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

using namespace as3;
using namespace as3test;

// ActionScript 3 compiled to ABC: each program's output read back, checked
// by the verifier and looked at in its disassembly. (Run in a player, it
// is not: there is none here.)
TEST_SUITE(AbcCompiler)

namespace {

namespace fs = std::filesystem;

std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

// Compiles, reads the output back and verifies it; the disassembly.
std::string compiled(const std::vector<abc::Source>& sources, const std::vector<abc::Timeline>& timelines = {},
                     abc::Output* result = nullptr) {
    abc::Output out = abc::compile(sources, timelines);
    if (!out.ok) throw std::runtime_error("did not compile:\n" + out.errorText());
    abc::AbcFile file;
    std::string error;
    if (!abc::read(out.abc, file, error)) throw std::runtime_error("output does not read: " + error);
    if (abc::write(file) != out.abc) throw std::runtime_error("output written again differs");
    const auto problems = abc::verify(file);
    if (!problems.empty()) {
        std::string all;
        for (const auto& problem : problems) all += problem + "\n";
        throw std::runtime_error("does not verify:\n" + all + abc::disassemble(file));
    }
    if (result) *result = out;
    return abc::disassemble(file);
}

std::string compiled(const std::string& text, const std::string& fileName = "Test.as") {
    return compiled(std::vector<abc::Source>{{text, fileName}});
}

void expectIn(const std::string& text, const std::string& part) {
    if (text.find(part) == std::string::npos) {
        throw std::runtime_error("expected \"" + part + "\" in:\n" + text);
    }
}

void expectNotIn(const std::string& text, const std::string& part) {
    if (text.find(part) != std::string::npos) {
        throw std::runtime_error("did not expect \"" + part + "\" in:\n" + text);
    }
}

// The disassembly of one method, by the name methodNames gives it.
std::string methodText(const std::string& all, const std::string& name) {
    const size_t at = all.find(" " + name + " (");
    if (at == std::string::npos) throw std::runtime_error("no method " + name + " in:\n" + all);
    const size_t start = all.rfind("\nmethod ", at);
    const size_t end = all.find("\nmethod ", at);
    return all.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// bunnyPaint: its classes, as3corelib's JPEG encoder, and the frame script
// and stage instances of its document's main timeline.
const char* kPaint = "D:/Backup/hulabunny/bunnyPaint/";

std::vector<abc::Source> paintSources() {
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(fs::path(kPaint) / "com/hulabunny/bunnyPaint")) {
        if (entry.is_regular_file() && entry.path().extension() == ".as") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    files.push_back(fs::path(kPaint) / "as3corelib-.93/src/com/adobe/images/JPGEncoder.as");
    files.push_back(fs::path(kPaint) / "as3corelib-.93/src/com/adobe/images/BitString.as");
    std::vector<abc::Source> sources;
    for (const fs::path& file : files) sources.push_back({readText(file), file.filename().string()});
    return sources;
}

std::string attribute(const std::string& tag, const std::string& name) {
    const size_t at = tag.find(" " + name + "=\"");
    if (at == std::string::npos) return std::string();
    const size_t start = at + name.size() + 3;
    return tag.substr(start, tag.find('"', start) - start);
}

// The main timeline as the document has it: its frame script, and its named
// instances typed as a published movie types them.
abc::Timeline paintTimeline() {
    const std::string document = readText(fs::path(kPaint) / "test/DOMDocument.xml");
    abc::Timeline timeline;
    timeline.className = abc::timelineClassName("bunnyPaint");
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

} // namespace

static void init_abc_compiler_tests() {
    auto& suite = suite_AbcCompiler;

    suite.addTest("A class with fields, methods and accessors", []() {
        const std::string out = compiled(R"(
package geo {
    import flash.display.Sprite;
    public class Box extends Sprite {
        public static const SIDES:int = 4;
        private static var count:int;
        public var label:String = "box";
        protected var w:Number;
        private var items:Array = [];
        public function Box(width:Number = 10) {
            super();
            w = width;
            count++;
        }
        public function get width2():Number { return w * 2; }
        public function set width2(value:Number):void { w = value / 2; }
        override public function toString():String { return "Box " + w + " " + label; }
        public static function make():Box { return new Box(SIDES); }
        internal function area():Number { return w * w; }
    }
}
)");
        expectIn(out, "class 0 geo:Box extends flash.display:Sprite");
        expectIn(out, "const SIDES slot 0 : int = (3)");
        expectIn(out, "slot private(geo:Box):count");
        expectIn(out, "slot protected(geo:Box):w");
        expectIn(out, "getter width2");
        expectIn(out, "setter width2");
        expectIn(out, "method toString override");
        expectIn(out, "method internal(geo):area");
        // Superclasses as scopes, as the class is made.
        expectIn(out, "getlex flash.display:DisplayObjectContainer");
        expectIn(out, "newclass 0");
        const std::string ctor = methodText(out, "geo:Box iinit");
        expectIn(ctor, "initproperty private(geo:Box):items");
        expectIn(ctor, "constructsuper 0");
        expectIn(ctor, "setproperty protected(geo:Box):w");
        expectIn(ctor, "scope 9-10");
        expectIn(methodText(out, "geo:Box$/make"), "constructprop geo:Box 1");
    });

    suite.addTest("Statements and operators", []() {
        compiled(R"(
package {
    public class Statements {
        public function run(list:Array, o:Object):int {
            var total:int = 0;
            var text:String = "";
            for (var i:int = 0; i < list.length; i++) {
                if (i % 2 == 0 && list[i] != null) continue;
                if (i > 10 || !o) break;
                total += list[i];
            }
            var j:uint = 0;
            while (j < 3) { j++; }
            do { total--; } while (total > 100);
            for (var key:String in o) text += key;
            for each (var value:* in o) text += value;
            outer: for (var a:int = 0; a < 3; ++a) {
                for (var b:int = 0; b < 3; b++) {
                    if (b == a) continue outer;
                    if (b > a) break outer;
                }
            }
            switch (total) {
                case 0: text = "zero"; break;
                case 1:
                case 2: text = "small";
                default: text += "!";
            }
            total = total > 0 ? total : -total;
            total <<= 1; total >>= 1; total >>>= 0; total |= 1; total &= 0xFF; total ^= 3;
            var n:Number = 7 / 2 + Math.floor(3.5) - (2 * 3) % 4;
            var bits:int = ~total & 0xF0 | 1 << 2;
            var s:Boolean = typeof o == "object" && o is Object && !(o instanceof Array);
            var arr:Array = o as Array;
            delete o.x;
            delete o["y"];
            var v:* = undefined;
            var m:Number = NaN;
            text = text || "none";
            var obj:Object = {a: 1, "b": 2, 3: [1, 2, , 3]};
            var re:RegExp = /a+b/gi;
            var k:int = (total, bits);
            return total + (s ? 1 : 0) + int(n) + uint(m) + k;
        }
    }
}
)");
    });

    suite.addTest("Closures capture variables in an activation", []() {
        const std::string out = compiled(R"(
package {
    import flash.events.Event;
    import flash.display.Sprite;
    public class Clicks extends Sprite {
        private var hits:int;
        public function Clicks() {
            var count:int = 0;
            var self:Clicks = this;
            addEventListener(Event.ENTER_FRAME, function(e:Event):void {
                count++;
                hits = count;
                self.dispatch(e);
                helper();
            });
            function helper():void { count += 2; }
        }
        public function dispatch(e:Event):void {}
        public function counter(start:int):Function {
            return function():int { return start++; };
        }
    }
}
)");
        const std::string ctor = methodText(out, "Clicks iinit");
        expectIn(ctor, "newactivation");
        expectIn(ctor, "newfunction");
        expectIn(out, "activation\n      slot internal():count slot 1 : int");
        const std::string counter = methodText(out, "Clicks/counter");
        expectIn(counter, "setslot 1");
        // In the closure the variable is found on the scope chain.
        expectIn(out, "findpropstrict internal():start");
        expectIn(out, "getlex internal():count");
    });

    suite.addTest("try, catch, finally and leaving them", []() {
        const std::string out = compiled(R"(
package {
    public class Tries {
        public function run(list:Array):String {
            var log:String = "";
            for (var i:int = 0; i < list.length; i++) {
                try {
                    if (i == 1) continue;
                    if (i == 5) break;
                    if (i == 7) return log;
                    log += list[i];
                    throw new Error("x");
                } catch (e:TypeError) {
                    log += "type";
                } catch (e:Error) {
                    log += e.message;
                    with (e) { log += name; if (log.length > 3) break; }
                } finally {
                    log += ";";
                }
            }
            try { log += "a"; } finally { log += "b"; }
            try { log += "c"; } catch (any) { log += any; }
            return log;
        }
    }
}
)");
        const std::string run = methodText(out, "Tries/run");
        expectIn(run, "type TypeError name e");
        expectIn(run, "type Error name e");
        expectIn(run, "type * name *");
        expectIn(run, "pushwith");
        expectIn(run, "throw");
    });

    suite.addTest("Interfaces, implemented and extended", []() {
        const std::string out = compiled(std::vector<abc::Source>{
            {"package shapes { public interface IShape { function area():Number; function get name():String; } }", "IShape.as"},
            {"package shapes { public interface ISolid extends IShape { function volume():Number; } }", "ISolid.as"},
            {"package shapes { public class Cube implements ISolid {\n"
             "  public function area():Number { return 6; }\n"
             "  public function get name():String { return 'cube'; }\n"
             "  public function volume():Number { return 1; }\n"
             "  public static function test(s:IShape):Boolean { return s is ISolid; } } }",
             "Cube.as"},
        });
        expectIn(out, "shapes:IShape interface");
        expectIn(out, "method namespace(shapes:IShape):area");
        expectIn(out, "getter namespace(shapes:IShape):name");
        expectIn(out, "shapes:ISolid implements shapes:IShape interface");
        expectIn(out, "shapes:Cube extends Object implements shapes:ISolid");
        expectIn(methodText(out, "shapes:Cube$/test"), "istypelate");
    });

    suite.addTest("Vectors, XML and E4X", []() {
        const std::string out = compiled(R"(
package {
    public class Data {
        public function run(n:int):Object {
            var v:Vector.<int> = new Vector.<int>();
            v.push(n);
            var w:Vector.<String> = new <String>["a", "b"];
            var vv:Vector.<Vector.<int>> = new Vector.<Vector.<int>>(2);
            var x:XML = <item id={n}>{"text"}<child name="a"/></item>;
            var names:XMLList = x.child.@name;
            var all:XMLList = x..child;
            var some:XMLList = x.child.(@name == "a");
            var stars:XMLList = x.*;
            x.@id = 5;
            return v.length + w.length + names.length() + all.length() + some.length() + stars.length();
        }
    }
}
)");
        const std::string run = methodText(out, "Data/run");
        expectIn(run, "applytype 1");
        expectIn(run, "__AS3__.vec:Vector");
        expectIn(run, "esc_xelem");
        expectIn(run, "esc_xattr");
        expectIn(run, "checkfilter");
        expectIn(run, "pushwith");
    });

    suite.addTest("Package functions, variables and code outside", []() {
        const std::string out = compiled(std::vector<abc::Source>{
            {"package util { public function twice(x:Number):Number { return x * 2; } }", "twice.as"},
            {"package util { public var counter:int = 3; public const NAME:String = 'u'; }", "vars.as"},
            {"package app { import util.twice; import util.*; public class Main {\n"
             "  public function run():Number { counter++; return twice(counter) + Helper.value(); } } }\n"
             "class Helper { public static function value():int { return 1; } }",
             "Main.as"},
        });
        expectIn(out, "method util:twice");
        expectIn(out, "slot util:counter");
        expectIn(methodText(out, "app:Main/run"), "callproperty util:twice 1");
        expectIn(methodText(out, "app:Main/run"), "getlex private(Main.as$):Helper");
    });

    suite.addTest("A timeline's frame scripts", []() {
        abc::Timeline timeline;
        timeline.className = abc::timelineClassName("movie");
        timeline.instances = {{"playButton", "flash.display.SimpleButton"}, {"label", "flash.text.TextField"}, {"clip", ""}};
        timeline.frames = {
            {0, "import flash.events.MouseEvent;\n"
                "stop();\n"
                "var clicks:int = 0;\n"
                "for (var i:int = 0; i < 3; i++) clicks += i;\n"
                "function onClick(e:MouseEvent):void { clicks++; label.text = 'Clicks: ' + clicks; gotoAndStop(2); }\n"
                "playButton.addEventListener(MouseEvent.CLICK, onClick);\n"
                "clip.addEventListener('enterFrame', function(e:*):void { clip.rotation += clicks; });\n"},
            {4, "trace('frame five', clicks); stop();"},
        };
        abc::Output result;
        const std::string out = compiled({}, {timeline}, &result);
        if (timeline.className != "movie_fla.MainTimeline") throw std::runtime_error(timeline.className);
        if (result.classes != std::vector<std::string>{"movie_fla.MainTimeline"}) throw std::runtime_error("classes");
        expectIn(out, "class 0 movie_fla:MainTimeline extends flash.display:MovieClip dynamic");
        expectIn(out, "slot playButton slot 0 : flash.display:SimpleButton");
        expectIn(out, "slot clip slot 0 : *");
        expectIn(out, "slot clicks slot 0 : int");
        expectIn(out, "slot i slot 0 : int");
        expectIn(out, "method onClick disp");
        expectIn(out, "method internal(movie_fla):frame1");
        expectIn(out, "method internal(movie_fla):frame5");
        const std::string ctor = methodText(out, "movie_fla:MainTimeline iinit");
        expectIn(ctor, "findpropstrict addFrameScript");
        expectIn(ctor, "pushbyte 4");
        expectIn(ctor, "getproperty internal(movie_fla):frame5");
        expectIn(ctor, "callpropvoid addFrameScript 4");
        const std::string frame1 = methodText(out, "movie_fla:MainTimeline/internal(movie_fla):frame1");
        expectIn(frame1, "setproperty clicks");
        expectIn(frame1, "getproperty playButton");
        expectIn(frame1, "getproperty onClick");
        expectNotIn(frame1, "newactivation");
        expectIn(abc::timelineClassName("my movie", "Button 1", 3), "my_movie_fla.Button1_3");
    });

    suite.addTest("Errors name their file and line", []() {
        abc::Output out = abc::compile({{"package {\n public class A {\n  public function f() { var x = ; }\n }\n}", "A.as"}});
        if (out.ok || out.diagnostics.empty()) throw std::runtime_error("compiled");
        expectIn(out.errorText(), "A.as:3");
        out = abc::compile({{"package { public class B extends Missing {} }", "B.as"}});
        if (out.ok) throw std::runtime_error("compiled with an unknown superclass");
        expectIn(out.errorText(), "superclass Missing");
        out = abc::compile({{"package { public class C { public function f():void { break; } } }", "C.as"}});
        if (out.ok) throw std::runtime_error("compiled a break outside a loop");
        expectIn(out.errorText(), "break outside a loop");
        out = abc::compile({{"package { public class D { public function f(a:int = 1, b:int):void {} } }", "D.as"}});
        if (out.ok) throw std::runtime_error("compiled a parameter without a default after one with");
    });

    suite.addTest("Debug information", []() {
        abc::Options options;
        options.debugInfo = true;
        abc::Output out = abc::compile({{"package { public class E {\n public function f():int {\n  var a:int = 1;\n  return a;\n }\n} }", "E.as"}},
                                       {}, options);
        if (!out.ok) throw std::runtime_error(out.errorText());
        abc::AbcFile file;
        std::string error;
        abc::read(out.abc, file, error);
        const std::string text = abc::disassemble(file);
        expectIn(text, "debugfile \"E.as\"");
        expectIn(text, "debugline 3");
    });

    suite.addTest("bunnyPaint and its main timeline compile and verify", []() {
        if (!fs::exists(fs::path(kPaint) / "com/hulabunny/bunnyPaint/BP_Canvas.as")) {
            std::cout << "    (sources not found; skipped)" << std::endl;
            return;
        }
        const std::vector<abc::Source> sources = paintSources();
        const abc::Timeline timeline = paintTimeline();
        // A library symbol exported for ActionScript without a class file
        // of its own: a class made for it, as for a timeline without
        // scripts.
        abc::Timeline closeButton;
        closeButton.className = "CloseButton";
        abc::Output result;
        const std::string out = compiled(sources, {closeButton, timeline}, &result);
        if (result.classes.size() != 46 || result.classes.back() != "bunnyPaint_fla.MainTimeline") {
            std::string names;
            for (const auto& name : result.classes) names += name + " ";
            throw std::runtime_error(std::to_string(result.classes.size()) + " classes: " + names);
        }
        for (const auto& diagnostic : result.diagnostics) {
            // The one known slip, MultiSmudgeTool.as line 50.
            if (diagnostic.fileName == "MultiSmudgeTool.as" && diagnostic.line == 50) continue;
            throw std::runtime_error("unexpected: " + diagnostic.text());
        }
        expectIn(out, "com.hulabunny.bunnyPaint:BP_Canvas extends flash.display:Sprite");
        expectIn(out, "slot lockPainting slot 0 : fl.controls:CheckBox");
        expectIn(out, "slot canvas slot 0 : com.hulabunny.bunnyPaint:BP_Canvas");
        expectIn(out, "method setFillTool disp");
        expectIn(out, "slot private(com.hulabunny.bunnyPaint:BP_EffectsWindow):mCloseButton slot 0 : CloseButton");
        expectIn(methodText(out, "bunnyPaint_fla:MainTimeline iinit"), "callpropvoid addFrameScript 2");
        std::cout << "    " << result.classes.size() << " classes, " << result.abc.size() << " bytes" << std::endl;
    });

    suite.addTest("Parameters, metadata, super and qualified names", []() {
        const std::string out = compiled(std::vector<abc::Source>{
            {"package base { public class Shape { protected var size:Number = 1;\n"
             "  public function Shape(s:Number) { size = s; }\n"
             "  public function get area():Number { return 0; }\n"
             "  protected function grow(by:Number):void { size += by; } } }",
             "Shape.as"},
            {R"(package app {
    import base.Shape;
    [Event(name="change", type="flash.events.Event")]
    public class Square extends Shape {
        [Inspectable(defaultValue="2")]
        public var sides:int = 4;
        public static const ORIGIN:flash.geom.Point = new flash.geom.Point(0, 0);
        public function Square(s:Number = -1.5, name:String = "sq", flag:Boolean = true, o:Object = null, ...more) {
            super(s * more.length);
        }
        override public function get area():Number { return super.area + size * size; }
        override protected function grow(by:Number):void { super.grow(by * 2); super.size = by; }
        public function stamp():Number { return flash.utils.getTimer() + Square.ORIGIN.x; }
        public function count():int {
            var f:Function = function fact(n:int):int { return n <= 1 ? 1 : n * fact(n - 1); };
            var v:Vector.<Number> = new Vector.<Number>(3);
            var i:int = 0;
            v[i] += 2; v[i]++; ++v[1];
            sides++; this.sides--; Square.ORIGIN.y += 1;
            var a:Array = [1, 2];
            if (1 in a) delete a[1];
            return f(5) + arguments2(1, 2);
        }
        public function arguments2(a:int, b:int):int { return arguments.length; }
    }
})", "Square.as"},
        });
        expectIn(out, "app:Square iinit (Number, String, Boolean, Object):* rest defaults 4");
        expectIn(out, "Square/arguments2 (int, int):int arguments");
        const std::string ctor = methodText(out, "app:Square iinit");
        expectIn(ctor, "constructsuper 1");
        expectNotIn(ctor, "constructsuper 0");
        expectIn(methodText(out, "app:Square/get area"), "getsuper area");
        expectIn(methodText(out, "app:Square/protected(app:Square):grow"), "callsupervoid protected(base:Shape):grow 1");
        expectIn(methodText(out, "app:Square/protected(app:Square):grow"), "setsuper protected(base:Shape):size");
        expectIn(methodText(out, "app:Square/stamp"), "callproperty flash.utils:getTimer 0");
        expectIn(methodText(out, "app:Square/stamp"), "getlex app:Square");
        expectIn(methodText(out, "app:Square cinit"), "constructprop flash.geom:Point 2");
        expectIn(out, "const ORIGIN slot 0 : flash.geom:Point");
        expectIn(out, "getproperty callee");
        const std::string count = methodText(out, "app:Square/count");
        expectIn(count, "in");
        expectIn(count, "deleteproperty");
        expectIn(count, "increment");
        // Metadata, on the class and on a field.
        abc::Output result = abc::compile({{"package { [Embed(source='a.png')] public class M { [Bindable] public var x:int; } }", "M.as"}});
        if (!result.ok) throw std::runtime_error(result.errorText());
        abc::AbcFile file;
        std::string error;
        abc::read(result.abc, file, error);
        if (file.metadata.size() != 2) throw std::runtime_error(std::to_string(file.metadata.size()) + " metadata");
        bool embed = false;
        for (const abc::MetadataInfo& tag : file.metadata) {
            embed = embed || (file.pool.strings[tag.name] == "Embed" && tag.items.size() == 1 &&
                              file.pool.strings[tag.items[0].first] == "source" &&
                              file.pool.strings[tag.items[0].second] == "a.png");
        }
        if (!embed) throw std::runtime_error("no Embed metadata");
        expectIn(abc::disassemble(file), "class M slot 0 class 0");
    });

    suite.addTest("Leaving nested tries, switches and labeled blocks", []() {
        const std::string out = compiled(R"(
package {
    public class Flow {
        public var log:Array = [];
        public function run(n:int):int {
            outer: for (var i:int = 0; i < n; i++) {
                try {
                    try {
                        switch (i) {
                            case 0: continue outer;
                            case 1: break;
                            case 2: break outer;
                            default: throw i;
                        }
                    } finally {
                        log.push("inner");
                    }
                } catch (e:*) {
                    try { return e; } finally { log.push("return"); }
                } finally {
                    log.push("outer");
                }
            }
            block: {
                if (n > 3) break block;
                log.push("block");
            }
            with (log) { push(n); }
            var k:int = 0;
            do { if (k == 2) continue; k++; } while (k < 5);
            return i;
        }
    }
}
)");
        const std::string run = methodText(out, "Flow/run");
        // Each way out runs the finally blocks it leaves: "inner" on the
        // way out of the switch (three ways and the throw's handler), and
        // "outer" too.
        size_t inner = 0;
        for (size_t at = run.find("\"inner\""); at != std::string::npos; at = run.find("\"inner\"", at + 1)) ++inner;
        if (inner < 4) throw std::runtime_error("inner finally inlined " + std::to_string(inner) + " times:\n" + run);
    });
}

static struct AbcCompilerTestInit {
    AbcCompilerTestInit() { init_abc_compiler_tests(); }
} abc_compiler_test_init;
