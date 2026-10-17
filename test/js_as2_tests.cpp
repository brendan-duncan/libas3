#include "test_runner.h"
#include "avm1_machine.h"
#include "../src/avm1/avm1.h"
#include "../src/js/as2.h"
#include "../src/js/js_writer.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace as3;
using namespace as3test;

// ActionScript 1 and 2 translated to JavaScript: what it makes, and
// programs run both by the AVM1 test machine (compiled to SWF actions) and,
// translated, in Node (where it is installed) on avm1.js, tracing the same;
// and programs whose output is as Flash Player gives it, run translated.
TEST_SUITE(JavaScriptAS2)

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

// A stand-in for a player: the root timeline with a clip "hat" on it and
// "ball" in that, whose timeline actions trace what they are asked.
const char* kHost = R"(
function Clip(name, parent) {
    Object.defineProperty(this, "$clip", { value: true });
    Object.defineProperty(this, "_name", { value: name, writable: true });
    Object.defineProperty(this, "_parent", { value: parent, writable: true });
    if (parent) parent[name] = this;
}
for (const name of ["play", "stop", "nextFrame", "prevFrame", "gotoAndPlay", "gotoAndStop", "removeMovieClip"]) {
    Object.defineProperty(Clip.prototype, name, { value: function () {
        avm1.output("[" + avm1.targetPath(this) + " " + name + (arguments.length ? " " + Array.from(arguments).join(" ") : "") + "]");
    } });
}
Clip.prototype._visible = true;
Clip.prototype._x = 0;
const root = new Clip("", undefined);
const hat = new Clip("hat", root);
new Clip("ball", hat);
avm1.host.level = (n) => (n === 0 ? root : undefined);
avm1.host.targetPath = function (clip) {
    let path = "";
    for (let at = clip; at && at !== root; at = at._parent) path = "." + at._name + path;
    return "_level0" + path;
};
avm1.host.gotoScene = (clip, scene, frame, play) => avm1.output("[scene " + scene + " " + frame + (play ? " play" : " stop") + "]");
let now = 0;
avm1.clock = () => now;
avm1.manualTimers = true;
const handlers = [];
const frames = [];
const inits = [];
avm1.runMovies({
    classes(defines) { avm1.defineClasses(defines, avm1.scope({})); },
    register(linkage, name) { Object.registerClass(linkage, avm1.qualified(name)); },
    initclip(timeline, order, run) { inits.push([order, run]); },
    frame(timeline, frame, run) { frames.push([frame, run]); },
    handlers(id, list) { handlers.push(list); },
});
const runScript = (run, target) => {
    try {
        run.call(target, avm1.scope(target));
    } catch (e) {
        print("uncaught " + avm1.s(e));
    }
};
inits.sort((a, b) => a[0] - b[0]).forEach((i) => runScript(i[1], root));
frames.sort((a, b) => a[0] - b[0]).forEach((f) => runScript(f[1], root));
for (let t = 10; t <= 200; t += 10) {
    now = t;
    avm1.runTimers(t);
}
for (const list of handlers) {
    for (const h of list) {
        print("on " + h.events.join(","));
        runScript(h.run, h.clip ? hat : root);
    }
}
)";

// Each module's output in Node, in a context of its own.
std::vector<std::string> runInNode(const std::vector<std::string>& modules) {
    const fs::path dir = fs::temp_directory_path() / "libas3-js-as2-tests";
    fs::create_directories(dir);
    writeText(dir / "avm1.js", js::as2::runtime());
    writeText(dir / "host.js", kHost);
    std::string list = "[";
    for (size_t i = 0; i < modules.size(); ++i) list += (i ? ",\n" : "") + js::quote(modules[i]);
    list += "]";
    writeText(dir / "cases.json", list);
    writeText(dir / "run.js", R"(const vm = require("vm");
const fs = require("fs");
const runtime = fs.readFileSync(process.argv[2], "utf8");
const host = fs.readFileSync(process.argv[3], "utf8");
const cases = JSON.parse(fs.readFileSync(process.argv[4], "utf8"));
let all = "";
for (let i = 0; i < cases.length; i++) {
    let out = "";
    const context = vm.createContext({ setTimeout, clearTimeout, setInterval, clearInterval, TextEncoder, TextDecoder, console });
    context.print = (text) => { out += text + "\n"; };
    try {
        vm.runInContext(runtime, context, { filename: "avm1.js" });
        vm.runInContext("avm1.output = print;", context);
        vm.runInContext(cases[i], context, { filename: "case" + i + ".js" });
        vm.runInContext(host, context, { filename: "host.js" });
    } catch (e) {
        out += "Error: " + (e && e.stack || e) + "\n";
    }
    all += "\u0001CASE " + i + "\n" + out;
}
process.stdout.write(all);
)");
    const std::string printed = capture("node \"" + (dir / "run.js").string() + "\" \"" + (dir / "avm1.js").string() + "\" \"" +
                                        (dir / "host.js").string() + "\" \"" + (dir / "cases.json").string() + "\"");
    std::vector<std::string> outputs(modules.size());
    size_t at = 0;
    for (size_t i = 0; i < modules.size(); ++i) {
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

js::as2::Output translateScript(const std::string& code, int version = 8, const std::vector<std::string>& classes = {}) {
    js::as2::Movie movie;
    movie.frames.push_back({"", 0, code, "Scene 1", "Scene 1, frame 1"});
    for (size_t i = 0; i < classes.size(); ++i) movie.classes.push_back({classes[i], "Class" + std::to_string(i) + ".as"});
    js::as2::Options options;
    options.swfVersion = version;
    return js::as2::translate(movie, options);
}

std::string module(const std::string& code, int version = 8, const std::vector<std::string>& classes = {}) {
    const js::as2::Output out = translateScript(code, version, classes);
    if (!out.ok) throw std::runtime_error("did not translate:\n" + out.errorText() + "\n" + code);
    return out.code;
}

// What the AVM1 test machine traces for a script.
std::string machineTraces(const std::string& code) {
    avm1::Compiler compiler;
    const std::vector<uint8_t> actions = compiler.compileScript(code);
    if (compiler.hasErrors()) return "(did not compile)";
    avm1test::Machine machine;
    machine.run(actions);
    return machine.traced;
}

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void expectIn(const std::string& text, const std::string& wanted) {
    if (text.find(wanted) == std::string::npos) throw std::runtime_error("no \"" + wanted + "\" in:\n" + text);
}

// The AVM1 suite's programs: each traces("...") of a string, read from its
// source (beside this file).
std::vector<std::pair<std::string, std::string>> avm1Programs() {
    std::vector<std::pair<std::string, std::string>> programs;
    const std::string text = readText(fs::path(__FILE__).parent_path() / "avm1_tests.cpp");
    std::string testName;
    for (size_t at = 0; at < text.size(); ++at) {
        if (text.compare(at, 15, "suite.addTest(\"") == 0) {
            const size_t end = text.find('"', at + 15);
            testName = text.substr(at + 15, end - at - 15);
        }
        if (text.compare(at, 7, "traces(") != 0 || (at > 0 && std::isalnum(static_cast<unsigned char>(text[at - 1])))) continue;
        size_t i = at + 7;
        std::string code;
        bool any = false;
        for (;;) {
            while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
            if (i >= text.size() || text[i] != '"') break;
            ++i;
            while (i < text.size() && text[i] != '"') {
                char c = text[i++];
                if (c == '\\') {
                    const char e = text[i++];
                    c = e == 'n' ? '\n' : e == 't' ? '\t' : e;
                }
                code += c;
            }
            ++i;
            any = true;
        }
        if (any && i < text.size() && text[i] == ')') programs.emplace_back(testName, code);
    }
    return programs;
}

// Programs and what Flash Player traces for them (SWF 8 unless given).
struct Case {
    const char* name;
    std::string source;
    std::string wanted;
    int version = 8;
    std::vector<std::string> classes;
};

std::vector<Case> programs() {
    return {
        {"Numbers in strings, 15 digits",
         "trace(1/3); trace(0.1 + 0.2); trace(1e21); trace(123456789012345680000); trace(0.00001);\n"
         "trace(-0.5); trace(1/0); trace(-1/0); trace(0/0); trace(100000000000000); trace(2.5e-7 * 2);",
         "0.333333333333333\n0.3\n1e+21\n1.23456789012346e+20\n1e-05\n-0.5\nInfinity\n-Infinity\nNaN\n100000000000000\n5e-07\n"},
        {"Conversions, SWF 7 and later",
         "trace('x' + undefined); trace(undefined + 1); trace(null + 1); trace('5' * '2'); trace('0x1A' - 0);\n"
         "trace('' - 0); trace(' 12' * 1); trace(true + 1); trace([1, 2, undefined] + ''); trace('3' + 4);\n"
         "trace(Number('abc')); trace(Number(true)); trace(String(null)); trace(Boolean('0')); trace(Boolean(''));\n"
         "trace(1 == '1'); trace(0 == ''); trace(null == undefined); trace(null == 0); trace('a' < 'b'); trace(2 < '10');\n"
         "trace('2' < '10'); trace(1 < undefined); trace(int(-3.7)); trace(int('8.9'));",
         "xundefined\nNaN\nNaN\n10\n26\nNaN\n12\n2\n1,2,undefined\n34\nNaN\n1\nnull\ntrue\nfalse\n"
         "true\nfalse\ntrue\nfalse\ntrue\ntrue\nfalse\nundefined\n-3\n8\n"},
        {"Conversions, SWF 6",
         "trace('x' + undefined); trace(undefined + 1); trace(null + 1); trace(Number(undefined)); trace(int(undefined));\n"
         "if ('0') trace('true'); else trace('false');\nif ('abc') trace('true'); else trace('false');\n"
         "trace(!'1'); trace([undefined, 2].join('-'));",
         "x\n1\n1\n0\n0\nfalse\nfalse\nfalse\n-2\n", 6},
        {"Names whatever their case, SWF 6",
         "var Score = 5;\ntrace(score);\nmyVar = 1;\ntrace(MYVAR);\nfunction Twice(N) { return n * 2; }\ntrace(twice(4));\n"
         "var o = {Size: 3};\ntrace(o.size + o['SIZE']);\nTRACE('case');\n_ROOT.hat.ball._Name = 'b2';\ntrace(_root.HAT.ball._name);",
         "5\n1\n8\n6\ncase\nb2\n", 6},
        {"Names count their case, SWF 7",
         "var Score = 5;\ntrace(score);\ntrace(Score);",
         "undefined\n5\n"},
        {"Timeline variables, _global and the scope chain",
         "x = 1;\nvar y = 2;\nfunction f() {\n  var x = 10;\n  z = x + y;\n  return x;\n}\ntrace(f());\ntrace(z);\ntrace(this.z);\n"
         "trace(_root.z);\n_global.g = 5;\ntrace(g);\ng = 6;\ntrace(g + ' ' + _global.g);\ntrace(this.x + this.y);\n"
         "function outer() {\n  var n = 1;\n  function inner() { return n + y; }\n  return inner();\n}\ntrace(outer());\n"
         "delete z;\ntrace(z);\ntrace(_level0 == _root);",
         "10\n12\n12\n12\n5\n6 5\n3\n3\nundefined\ntrue\n"},
        {"Target paths, eval, set and tellTarget",
         "hat.ball.size = 7;\ntrace(eval('/hat/ball:size'));\ntrace(eval('_root.hat.ball')._name);\nset('/hat:score', 5);\n"
         "trace(hat.score);\nset('n', 3);\ntrace(n);\ntrace(eval('hat.ball.size'));\ntellTarget ('hat') {\n  trace(_name);\n  gotoAndStop(3);\n"
         "  trace(score);\n}\ntellTarget ('/hat/ball') {\n  play();\n}\ntrace(targetPath(hat.ball));\n"
         "with (hat) {\n  trace(_name);\n  score = 9;\n}\ntrace(hat.score);\nsetProperty('hat', _visible, '0');\n"
         "trace(getProperty('hat', _visible));\ntrace(hat.ball._parent._name);\ntrace(eval('hat/ball:size'));",
         "7\nball\n5\n3\n7\nhat\n[_level0.hat gotoAndStop 3]\n5\n[_level0.hat.ball play]\n_level0.hat.ball\nhat\n9\n0\nhat\n7\n"},
        {"Timeline actions and scenes",
         "gotoAndPlay(5);\ngotoAndStop('end');\ngotoAndPlay('Scene 2', 1);\nstop();\nplay();\nnextFrame();\nhat.gotoAndStop(2);\n"
         "ifFrameLoaded (10) {\n  trace('loaded');\n}",
         "[scene Scene 1 5 play]\n[scene Scene 1 end stop]\n[scene Scene 2 1 play]\n[_level0 stop]\n[_level0 play]\n"
         "[_level0 nextFrame]\n[_level0.hat gotoAndStop 2]\nloaded\n"},
        {"Prototype classes, ActionScript 1",
         "function Point(x, y) {\n  this.x = x;\n  this.y = y;\n}\nPoint.prototype.len = function () {\n"
         "  return Math.sqrt(this.x * this.x + this.y * this.y);\n};\nvar p = new Point(3, 4);\ntrace(p.len());\n"
         "trace(p instanceof Point);\ntrace(typeof p);\nfunction Point3(x, y, z) {\n  Point.call(this, x, y);\n  this.z = z;\n}\n"
         "Point3.prototype = new Point();\nPoint3.prototype.sum = function () { return this.x + this.y + this.z; };\n"
         "var q = new Point3(1, 2, 3);\ntrace(q.sum() + ' ' + q.len() + ' ' + (q instanceof Point));\n"
         "trace(q.__proto__ == Point3.prototype);\nvar o = new Object();\no.__proto__ = Point.prototype;\no.x = 6; o.y = 8;\ntrace(o.len());",
         "5\ntrue\nobject\n6 2.23606797749979 true\ntrue\n10\n"},
        {"Functions as objects: apply, call, arguments",
         "function sum() {\n  var t = 0;\n  for (var i = 0; i < arguments.length; i++) t += arguments[i];\n  return t;\n}\n"
         "trace(sum.apply(null, [1, 2, 3]));\ntrace(sum.call(null, 4, 5));\nfunction who() { return this.name; }\n"
         "var a = {name: 'a'};\ntrace(who.call(a));\nwho.label = 'fn';\ntrace(who.label);\ntrace(typeof who);\n"
         "var f = function (n) { return n <= 1 ? 1 : n * arguments.callee(n - 1); };\ntrace(f(5));\ntrace(sum());",
         "6\n9\na\nfn\nfunction\n120\n0\n"},
        {"for-in: the latest first, and delete",
         "var o = {a: 1};\no.b = 2;\no.c = 3;\nvar s = '';\nfor (var k in o) s += k;\ntrace(s);\ndelete o.b;\ns = '';\n"
         "for (k in o) s += k;\ntrace(s);\nvar list = [5, 6];\ns = '';\nfor (var i in list) s += i;\ntrace(s);\n"
         "function keys(obj) {\n  var out = '';\n  for (var name in obj) out += name;\n  return out;\n}\ntrace(keys({x: 1, y: 2}));",
         "cba\nca\n10\nyx\n"},
        {"typeof and instanceof",
         "trace(typeof this);\ntrace(typeof null);\ntrace(typeof undefined);\ntrace(typeof trace);\ntrace(typeof 'a');\n"
         "trace(typeof 1);\ntrace(typeof {});\ntrace(typeof []);\ntrace([] instanceof Array);\ntrace(1 instanceof Number);\n"
         "trace(new Date() instanceof Date);\ntrace(typeof hat);",
         "movieclip\nnull\nundefined\nfunction\nstring\nnumber\nobject\nobject\ntrue\nfalse\ntrue\nmovieclip\n"},
        {"Global functions",
         "trace(chr(65) + ord('B'));\ntrace(length('abc'));\ntrace(substring('hello', 2, 3));\ntrace(escape('a b&c/é'));\n"
         "trace(unescape('a%20b%C3%A9'));\ntrace(parseInt('077'));\ntrace(parseInt('0x1F'));\ntrace(parseInt('12px'));\n"
         "trace(parseFloat('3.5px'));\ntrace(isNaN('abc'));\ntrace(isNaN('12'));\ntrace(isFinite(1/0));\n"
         "var r = random(5);\ntrace(r >= 0 && r < 5 && r == int(r));\ntrace(Math.max(2, 7));\ntrace(getTimer() >= 0);\n"
         "trace(String(12) + Number('3'));\ntrace(Boolean(1));",
         "A66\n3\nell\na%20b%26c/%C3%A9\na b\xc3\xa9\n63\n31\n12\n3.5\ntrue\nfalse\nfalse\ntrue\n7\ntrue\n123\ntrue\n"},
        {"Strings' methods",
         "var s = 'Hello, World';\ntrace(s.charAt(1));\ntrace(s.indexOf('o'));\ntrace(s.lastIndexOf('o'));\ntrace(s.substr(7, 3));\n"
         "trace(s.substring(7));\ntrace(s.slice(-5));\ntrace(s.split(', ').length);\ntrace(s.toUpperCase());\n"
         "trace(s.charCodeAt(0));\ntrace(String.fromCharCode(72, 105));\ntrace(s.length);\ntrace(s.concat('!', 1));",
         "e\n4\n8\nWor\nWorld\nWorld\n2\nHELLO, WORLD\n72\nHi\n12\nHello, World!1\n"},
        {"Arrays: sort and sortOn with their options",
         "var a = [3, 20, 100];\na.sort();\ntrace(a);\na.sort(Array.NUMERIC);\ntrace(a);\na.sort(Array.NUMERIC | Array.DESCENDING);\n"
         "trace(a);\nvar people = [{n: 'b', age: 30}, {n: 'a', age: 25}, {n: 'c', age: 4}];\npeople.sortOn('age', Array.NUMERIC);\n"
         "trace(people[0].n + people[1].n + people[2].n);\npeople.sortOn('n');\ntrace(people[0].n);\n"
         "var w = ['b', 'C', 'a'];\nw.sort(Array.CASEINSENSITIVE);\ntrace(w);\nw.sort(function (x, y) { return x < y ? 1 : -1; });\n"
         "trace(w);\ntrace(a.sort(Array.RETURNINDEXEDARRAY | Array.NUMERIC));\ntrace([1, 2, 3].reverse().join());\n"
         "var list = [1, 2, 3, 4];\ntrace(list.splice(1, 2));\ntrace(list);\ntrace(list.concat([5], 6).length);",
         "100,20,3\n3,20,100\n100,20,3\ncab\na\na,b,C\nb,a,C\n2,1,0\n3,2,1\n2,3\n1,4\n4\n"},
        {"Getters and setters by addProperty",
         "var o = {};\no.addProperty('v', function () { return 42; }, function (x) { trace('set ' + x); });\ntrace(o.v);\n"
         "o.v = 3;\nvar s = '';\nfor (var k in o) s += k;\ntrace(s);",
         "42\nset 3\n\n"},
        {"Members of what is not there are undefined",
         "trace(nothing.at.all);\nnothing.x = 5;\nnothing.f();\nundefinedFunction();\nvar o = {};\ntrace(o.missing.deeper);\n"
         "o.n++;\ntrace(o.n);\ntrace('ok');",
         "undefined\nundefined\nNaN\nok\n"},
        {"Timers: setInterval and clearInterval",
         "var n = 0;\nvar id = setInterval(function () {\n  n++;\n  trace('tick ' + n);\n  if (n == 3) clearInterval(id);\n}, 25);\n"
         "var o = {count: 0, step: function (by) { this.count += by; trace('step ' + this.count); }};\n"
         "var once = setTimeout(o, 'step', 50, 2);",
         "tick 1\ntick 2\nstep 2\ntick 3\n"},
        {"switch, labels, try and throw",
         "function kind(v) {\n  switch (v) {\n    case 1: return 'one';\n    case '1': return 'text';\n    default: return 'other';\n  }\n}\n"
         "trace(kind(1) + kind('1') + kind(true));\ntry {\n  throw {code: 7};\n} catch (e) {\n  trace(e.code);\n} finally {\n  trace('done');\n}\n"
         "try {\n  throw new Error('bad');\n} catch (e) {\n  trace(e.message + ' ' + e);\n}\nthrow 'up';",
         "onetextother\n7\ndone\nbad bad\nuncaught up\n"},
        {"ActionScript 2 classes: extends, super, statics, getters and setters",
         "import shapes.*;\nvar c = new Circle(2);\ntrace(c.describe());\ntrace(c.area);\nc.radius = 3;\ntrace(c.radius + ' ' + c.area);\n"
         "trace(Shape.count);\ntrace(c instanceof Shape);\ntrace(c instanceof Circle);\ntrace(c instanceof shapes.IDrawable);\n"
         "var s = new Shape('plain');\ntrace(s.describe());\ntrace(Shape.count);\ntrace(c.sides);\ntrace(Circle.unit().radius);",
         "circle of 2 (shape circle)\n12.56\n3 28.26\n1\ntrue\ntrue\ntrue\nshape plain\n2\n0\n1\n",
         8,
         {"class shapes.Shape {\n  static var count:Number = 0;\n  var name:String;\n  var sides:Number = 0;\n"
          "  function Shape(name:String) {\n    this.name = name;\n    count++;\n  }\n"
          "  function describe():String {\n    return 'shape ' + name;\n  }\n}\n",
          "import shapes.Shape;\nclass shapes.Circle extends Shape implements shapes.IDrawable {\n  private var r:Number;\n"
          "  static var PI:Number = 3.14;\n  function Circle(r:Number) {\n    super('circle');\n    this.r = r;\n  }\n"
          "  function get radius():Number { return r; }\n  function set radius(value:Number):Void { r = value; }\n"
          "  function get area():Number { return Math.round(PI * r * r * 100) / 100; }\n"
          "  function describe():String {\n    return 'circle of ' + r + ' (' + super.describe() + ')';\n  }\n"
          "  static function unit():Circle { return new Circle(1); }\n  function draw():Void {}\n}\n",
          "interface shapes.IDrawable {\n  function draw():Void;\n}\n"}},
        {"A class's bare names: its members, the clip's, the timeline's",
         "var b = new Box();\nb.grow(2);\ntrace(b.size);\ntrace(b.total());\nhelper = 10;\ntrace(b.useGlobal());",
         "3\n103\n10\n",
         8,
         {"class Box {\n  var size:Number = 1;\n  static var base:Number = 100;\n  function grow(by:Number) { size += by; }\n"
          "  function total():Number { var extra = base; return size + extra; }\n"
          "  function useGlobal():Number { return _root.helper; }\n}\n"}},
        {"#initclip runs first, in its order",
         "#initclip 2\ntrace('init two');\n_global.ready = true;\n#endinitclip\ntrace('frame ' + ready);",
         "init two\nframe true\n"},
    };
}

} // namespace

static void init_js_as2_tests() {
    auto& suite = suite_JavaScriptAS2;

    suite.addTest("Translates frame scripts readably", []() {
        const std::string code = module("var count = 0;\nfunction add(n) {\n  var total = 0;\n  for (var i = 0; i < n; i++) total += i;\n"
                                        "  return total;\n}\nstop();\nhat._x = add(3) + count;\n");
        expectIn(code, "avm1.movie(function ($, movie) {");
        expectIn(code, "movie.frame(\"\", 0, function ($s) {");
        expectIn(code, "$s.def(\"add\", function add(n) {");
        expectIn(code, "for (var i = 0; $.lt(i, n); i++) {");
        expectIn(code, "$s.def(\"count\", 0);");
        expectIn(code, "$s.stop();");
        expectIn(code, "$.put($s.get(\"hat\"), \"_x\", $.add($s.call(\"add\", [3]), $s.get(\"count\")));");
    });

    suite.addTest("Handlers, classes and registrations", []() {
        js::as2::Movie movie;
        movie.instances.push_back({3, "on (release, keyPress \"<Enter>\") {\n  gotoAndPlay(2);\n}\nonClipEvent (enterFrame) {\n  _x += 1;\n}\n", "button"});
        movie.classes.push_back({"class Hat extends MovieClip {\n  function Hat() {}\n  function onPress() { _alpha = 50; }\n}\n", "Hat.as"});
        movie.registrations.push_back({"hat symbol", "Hat"});
        const js::as2::Output out = js::as2::translate(movie);
        expect(out.ok, out.errorText());
        expectIn(out.code, "movie.handlers(3, [");
        expectIn(out.code, "{events: [\"release\", \"keyPress <Enter>\"], clip: false, run: function ($s) {");
        expectIn(out.code, "{events: [\"enterFrame\"], clip: true, run: function ($s) {");
        expectIn(out.code, "$s.set(\"_x\", $.add($s.get(\"_x\"), 1));");
        expectIn(out.code, "\"Hat\": [[\"MovieClip\"], function ($s) {");
        expectIn(out.code, "this._alpha = 50;");
        expectIn(out.code, "movie.register(\"hat symbol\", \"Hat\");");
        expect(out.classes.size() == 1 && out.classes[0] == "Hat", "classes");
    });

    suite.addTest("Errors name where they are", []() {
        const js::as2::Output out = translateScript("var a = ;\n");
        expect(!out.ok && !out.errors.empty(), "no error");
        expectIn(out.errorText(), "Scene 1, frame 1");
        const js::as2::Output handler = translateScript("on (release) { trace(1); }");
        expectIn(handler.errorText(), "goes on a button or a movie clip instance");
    });

    suite.addTest("Programs trace as Flash Player does", []() {
        if (!nodeThere()) return;
        const std::vector<Case> list = programs();
        std::vector<std::string> modules;
        for (const Case& p : list) modules.push_back(module(p.source, p.version, p.classes));
        const std::vector<std::string> outputs = runInNode(modules);
        std::string failures;
        for (size_t i = 0; i < list.size(); ++i) {
            if (outputs[i] != list[i].wanted) {
                failures += std::string("\n") + list[i].name + ":\n" + outputs[i] + "-- wanted:\n" + list[i].wanted + "-- code:\n" + modules[i];
            }
        }
        expect(failures.empty(), failures);
    });

    suite.addTest("The AVM1 suite's programs trace the same translated", []() {
        if (!nodeThere()) return;
        const auto list = avm1Programs();
        expect(list.size() >= 8, "found " + std::to_string(list.size()) + " programs");
        std::vector<std::string> modules;
        for (const auto& p : list) modules.push_back(module(p.second));
        const std::vector<std::string> outputs = runInNode(modules);
        std::string failures;
        for (size_t i = 0; i < list.size(); ++i) {
            const std::string machine = machineTraces(list[i].second);
            if (outputs[i] != machine) {
                failures += "\n" + list[i].first + ":\n" + outputs[i] + "-- the machine:\n" + machine + "-- code:\n" + modules[i];
            }
        }
        expect(failures.empty(), failures);
    });

    suite.addTest("Programs trace as the AVM1 machine does", []() {
        if (!nodeThere()) return;
        // Those of the programs above the machine runs (it has few of the
        // built-in classes, and its own string conversions).
        static const std::set<std::string> machineRuns = {"Numbers in strings, 15 digits",
                                                          "Timeline variables, _global and the scope chain"};
        std::vector<std::pair<std::string, std::string>> list;
        for (const Case& p : programs()) {
            if (machineRuns.count(p.name)) list.emplace_back(p.name, p.source);
        }
        std::vector<std::string> modules;
        for (const auto& p : list) modules.push_back(module(p.second));
        const std::vector<std::string> outputs = runInNode(modules);
        std::string failures;
        for (size_t i = 0; i < list.size(); ++i) {
            const std::string machine = machineTraces(list[i].second);
            if (outputs[i] != machine) failures += "\n" + list[i].first + ":\n" + outputs[i] + "-- the machine:\n" + machine;
        }
        expect(failures.empty(), failures);
    });
}

static struct JsAs2TestInit {
    JsAs2TestInit() { init_js_as2_tests(); }
} js_as2_test_init;
