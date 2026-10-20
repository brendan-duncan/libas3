#include "test_runner.h"
#include "swf_reader.h"
#include "../src/avm1/avm1.h"
#include "../src/avm1/avm1_actions.h"
#include "../src/avm1/avm1_interpreter.h"
#include "../src/bytecode/compiler.h"
#include "../src/bytecode/vm.h"
#include "../src/parser/parser.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace as3;
using namespace as3test;

// The AVM1 interpreter: SWF action bytecode run on the VM. What the AVM1
// compiler makes of a script should do what the same script compiled for
// the VM does, and real movies' actions should run.
TEST_SUITE(AVM1Run)

namespace {

// A movie clip for the tests: a timeline with frames and labels, and what
// scripts set on it, its children by name.
class TestClip : public HostObject {
public:
    std::string name;
    TestClip* parent = nullptr;
    std::vector<ValuePtr> children;
    ValuePtr self;
    int frame = 0;
    int frames = 10;
    bool playing = true;
    double x = 0;
    bool visible = true;
    std::map<std::string, int> labels;
    std::vector<std::string>* log = nullptr;

    TestClip* childNamed(const std::string& wanted) const {
        for (const ValuePtr& child : children) {
            auto* clip = dynamic_cast<TestClip*>(child->host.get());
            if (clip && clip->name == wanted) return clip;
        }
        return nullptr;
    }

    void note(const std::string& what) {
        if (log) log->push_back(name + " " + what);
    }

    ValuePtr get(const std::string& key) override {
        if (key == "_x") return Value::makeNumber(x);
        if (key == "_visible") return Value::makeBoolean(visible);
        if (key == "_currentframe") return Value::makeNumber(frame + 1);
        if (key == "_totalframes" || key == "_framesloaded") return Value::makeNumber(frames);
        if (key == "_name") return Value::makeString(name);
        if (key == "_parent") return parent ? parent->self : nullptr;
        if (key == "gotoAndStop" || key == "gotoAndPlay") {
            const bool play = key == "gotoAndPlay";
            return Value::makeNativeFunction([this, play](const std::vector<ValuePtr>& args) {
                if (!args.empty()) {
                    frame = args[0]->isNumber() ? static_cast<int>(args[0]->toNumber()) - 1 : labels[args[0]->toString()];
                }
                playing = play;
                note(std::string(play ? "gotoAndPlay " : "gotoAndStop ") + std::to_string(frame));
                return Value::makeUndefined();
            });
        }
        if (key == "play" || key == "stop") {
            const bool play = key == "play";
            return Value::makeNativeFunction([this, play](const std::vector<ValuePtr>&) {
                playing = play;
                note(play ? "play" : "stop");
                return Value::makeUndefined();
            });
        }
        if (TestClip* child = childNamed(key)) return child->self;
        return nullptr;
    }

    bool set(const std::string& key, ValuePtr value) override {
        if (key == "_x") {
            x = value->toNumber();
            return true;
        }
        if (key == "_visible") {
            visible = value->toBoolean();
            return true;
        }
        return false;
    }
};

// A root with a clip "hat" on it, and "bow" in that.
struct TestMovie {
    std::vector<std::string> log;
    ValuePtr root, hat, bow;

    static ValuePtr make(const std::string& name, TestClip* parent, std::vector<std::string>* log) {
        auto clip = std::make_shared<TestClip>();
        clip->name = name;
        clip->parent = parent;
        clip->log = log;
        ValuePtr value = Value::makeHostObject(clip);
        clip->self = value;
        if (parent) parent->children.push_back(value);
        return value;
    }
    static TestClip* clip(const Val& value) { return value.host<TestClip>(); }

    TestMovie() {
        root = make("_level0", nullptr, &log);
        hat = make("hat", clip(root), &log);
        bow = make("bow", clip(hat), &log);
        clip(root)->labels["ending"] = 7;
        clip(hat)->labels["end"] = 4;
    }
    ~TestMovie() {
        for (const ValuePtr& value : {root, hat, bow}) {
            clip(value)->children.clear();
            clip(value)->self.reset();
        }
    }
};

class TestHost : public avm1::Host {
public:
    explicit TestHost(TestMovie& movie) : movie(movie) {}
    TestMovie& movie;
    std::string traced;
    std::vector<std::string> errors;

    static TestClip* clip(const Val& value) { return value.host<TestClip>(); }
    void note(const Val& value, const std::string& what) {
        if (TestClip* c = clip(value)) c->note(what);
    }

    void trace(const std::string& text) override { traced += text + "\n"; }
    void error(const std::string& message) override { errors.push_back(message); }
    bool isClip(const Val& value) override { return clip(value) != nullptr; }
    Val root(const Val&) override { return movie.root; }
    Val level(int number) override { return number == 0 ? Val(movie.root) : Val(); }
    Val parent(const Val& value) override {
        TestClip* c = clip(value);
        return c && c->parent ? Val(c->parent->self) : Val();
    }
    Val child(const Val& value, const std::string& name) override {
        TestClip* c = clip(value);
        TestClip* found = c ? c->childNamed(name) : nullptr;
        return found ? Val(found->self) : Val();
    }
    std::string targetPath(const Val& value) override {
        std::string path;
        for (TestClip* c = clip(value); c; c = c->parent) path = c->name + (path.empty() ? "" : "." + path);
        return path;
    }
    void childNames(const Val& value, std::vector<std::string>& names) override {
        if (TestClip* c = clip(value)) {
            for (const ValuePtr& child : c->children) names.push_back(clip(Val(child))->name);
        }
    }
    void gotoFrame(const Val& value, int frame, bool play) override {
        if (TestClip* c = clip(value)) {
            c->frame = frame;
            c->playing = play;
        }
        note(value, "gotoFrame " + std::to_string(frame) + (play ? " play" : ""));
    }
    bool gotoLabel(const Val& value, const std::string& label, bool play) override {
        TestClip* c = clip(value);
        if (!c || !c->labels.count(label)) return false;
        gotoFrame(value, c->labels[label], play);
        return true;
    }
    void setPlaying(const Val& value, bool playing) override {
        if (TestClip* c = clip(value)) c->playing = playing;
        note(value, playing ? "play" : "stop");
    }
    void nextFrame(const Val& value) override { note(value, "nextFrame"); }
    void prevFrame(const Val& value) override { note(value, "prevFrame"); }
    int framesLoaded(const Val& value) override {
        TestClip* c = clip(value);
        return c ? c->frames : 0;
    }
    void callFrame(const Val& value, const Val& frame) override { note(value, "call " + frame.toString()); }
    void cloneSprite(const Val& value, const std::string& name, int depth) override {
        note(value, "duplicate " + name + " " + std::to_string(depth));
    }
    void removeSprite(const Val& value) override { note(value, "remove"); }
    void startDrag(const Val& value, bool lock, bool constrain, double left, double top, double right,
                   double bottom) override {
        std::string what = std::string("drag") + (lock ? " lock" : "");
        if (constrain) {
            what += " " + std::to_string(static_cast<int>(left)) + " " + std::to_string(static_cast<int>(top)) + " " +
                    std::to_string(static_cast<int>(right)) + " " + std::to_string(static_cast<int>(bottom));
        }
        note(value, what);
    }
    void endDrag() override { movie.log.push_back("stopDrag"); }
    void getURL(const std::string& url, const std::string& target, int method, bool loadTarget,
                bool loadVariables) override {
        movie.log.push_back("getURL " + url + " " + target + " " + std::to_string(method) +
                            (loadTarget ? " target" : "") + (loadVariables ? " variables" : ""));
    }
    double time() override { return 1234.5; }
    std::string mmExecute(const std::string& code) override {
        movie.log.push_back("MMExecute " + code);
        return "shot.fla";
    }
    void targetChanged(const Val& value) override { movie.log.push_back("target " + targetPath(value)); }
};

std::vector<uint8_t> compileActions(const std::string& source, const avm1::Options& options = avm1::Options()) {
    avm1::Compiler compiler(options);
    std::vector<uint8_t> actions = compiler.compileScript(source);
    if (compiler.hasErrors() || actions.empty()) {
        std::string errors;
        for (const std::string& error : compiler.errors()) errors += error + "\n";
        throw std::runtime_error("did not compile: " + errors);
    }
    return actions;
}

avm1::Actions shared(std::vector<uint8_t> bytes) {
    return std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
}

// A script run as the movie runs a SWF's: compiled to actions, run by the
// interpreter on the root. What it traces, its errors after.
struct Run {
    VM vm;
    TestMovie movie;
    TestHost host{movie};
    avm1::Interpreter interpreter;
    explicit Run(int version = 8) : interpreter(vm, host, version) {}

    std::string actions(const std::vector<uint8_t>& bytes) {
        host.traced.clear();
        interpreter.run(shared(bytes), movie.root, "test");
        std::string out = host.traced;
        for (const std::string& error : host.errors) out += "error: " + error + "\n";
        host.errors.clear();
        return out;
    }
    std::string script(const std::string& source) { return actions(compileActions(source)); }
};

std::string viaInterpreter(const std::string& source, int version = 8) {
    Run run(version);
    return run.script(source);
}

// The same script compiled for the VM and run on the root as a frame
// script.
std::string viaSource(const std::string& source) {
    VM vm;
    TestMovie movie;
    std::string out;
    vm.registerBuiltin("trace", [&out](const std::vector<ValuePtr>& args) {
        for (size_t i = 0; i < args.size(); ++i) out += (i ? " " : "") + args[i]->toString();
        out += "\n";
        return Value::makeUndefined();
    });
    Parser parser(source);
    auto program = parser.parse();
    if (parser.hasErrors()) throw std::runtime_error("Parse error: " + parser.getErrors());
    Compiler compiler;
    vm.execute(compiler.compile(program.get()), movie.root);
    if (vm.hasError()) out += "error: " + vm.getError() + "\n";
    return out;
}

void expect(const std::string& got, const std::string& wanted) {
    if (got != wanted) throw std::runtime_error("got\n" + got + "\nwanted\n" + wanted);
}

// The interpreter does what the VM does with the script's source.
void same(const std::string& source) {
    const std::string wanted = viaSource(source);
    if (wanted.find("error:") != std::string::npos) throw std::runtime_error("the source fails: " + wanted);
    expect(viaInterpreter(source), wanted);
}

std::string joined(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& line : lines) out += line + "\n";
    return out;
}

// Actions written out by hand, for those the compiler does not make (SWF 4's).
struct Assembler {
    std::vector<uint8_t> out;
    Assembler& op(uint8_t code) {
        out.push_back(code);
        return *this;
    }
    Assembler& record(uint8_t code, const std::vector<uint8_t>& payload) {
        out.push_back(code);
        out.push_back(static_cast<uint8_t>(payload.size() & 0xFF));
        out.push_back(static_cast<uint8_t>(payload.size() >> 8));
        out.insert(out.end(), payload.begin(), payload.end());
        return *this;
    }
    Assembler& str(const std::string& text) {
        std::vector<uint8_t> payload{avm1::PushString};
        payload.insert(payload.end(), text.begin(), text.end());
        payload.push_back(0);
        return record(avm1::Push, payload);
    }
    Assembler& num(int value) {
        std::vector<uint8_t> payload{avm1::PushInteger};
        for (int i = 0; i < 4; ++i) payload.push_back(static_cast<uint8_t>((static_cast<uint32_t>(value) >> (8 * i)) & 0xFF));
        return record(avm1::Push, payload);
    }
    std::vector<uint8_t> done() {
        out.push_back(avm1::End);
        return out;
    }
};

// -- Real movies --------------------------------------------------------------

std::vector<SwfTag> nestedTags(const std::vector<uint8_t>& body, size_t at) {
    std::vector<SwfTag> tags;
    while (at + 2 <= body.size()) {
        const int header = body[at] | (body[at + 1] << 8);
        at += 2;
        SwfTag tag;
        tag.code = header >> 6;
        size_t length = header & 0x3F;
        if (length == 0x3F) {
            if (at + 4 > body.size()) break;
            length = body[at] | (body[at + 1] << 8) | (body[at + 2] << 16) | (static_cast<size_t>(body[at + 3]) << 24);
            at += 4;
        }
        if (at + length > body.size()) break;
        tag.data.assign(body.begin() + static_cast<std::ptrdiff_t>(at), body.begin() + static_cast<std::ptrdiff_t>(at + length));
        at += length;
        if (tag.code == 0) break;
        tags.push_back(std::move(tag));
    }
    return tags;
}

// The action blocks of a movie: its frames' and sprites' DoActions, its
// DoInitActions, and its buttons' condition actions.
void gatherActions(const std::vector<SwfTag>& tags, std::vector<std::vector<uint8_t>>& init,
                   std::vector<std::vector<uint8_t>>& frames, std::vector<std::vector<uint8_t>>& buttons) {
    for (const SwfTag& tag : tags) {
        if (tag.code == 12) {
            frames.push_back(tag.data);
        } else if (tag.code == 59 && tag.data.size() > 2) {
            init.emplace_back(tag.data.begin() + 2, tag.data.end());
        } else if (tag.code == 39 && tag.data.size() > 4) {
            gatherActions(nestedTags(tag.data, 4), init, frames, buttons);
        } else if (tag.code == 34 && tag.data.size() > 5) {
            // DefineButton2: id, flags, the offset to the first condition.
            size_t offset = tag.data[3] | (tag.data[4] << 8);
            if (offset == 0) continue;
            size_t at = 3 + offset;
            while (at + 4 <= tag.data.size()) {
                const size_t size = tag.data[at] | (tag.data[at + 1] << 8);
                const size_t end = size == 0 ? tag.data.size() : std::min(tag.data.size(), at + size);
                buttons.emplace_back(tag.data.begin() + static_cast<std::ptrdiff_t>(at + 4),
                                     tag.data.begin() + static_cast<std::ptrdiff_t>(end));
                if (size == 0) break;
                at += size;
            }
        } else if (tag.code == 7 && tag.data.size() > 2) {
            // DefineButton: records to a zero, then the actions.
            size_t at = 2;
            while (at < tag.data.size() && tag.data[at] != 0) {
                at += 1 + 2 + 2;
                // A matrix: bits, rounded up to bytes.
                if (at >= tag.data.size()) break;
                size_t bit = at * 8;
                const auto read = [&](int n) {
                    uint32_t v = 0;
                    for (int i = 0; i < n; ++i, ++bit) {
                        if (bit / 8 >= tag.data.size()) return v;
                        v = (v << 1) | ((tag.data[bit / 8] >> (7 - bit % 8)) & 1);
                    }
                    return v;
                };
                if (read(1)) read(static_cast<int>(read(5)) * 2);
                if (read(1)) read(static_cast<int>(read(5)) * 2);
                read(static_cast<int>(read(5)) * 2);
                at = (bit + 7) / 8;
            }
            if (at + 1 < tag.data.size()) buttons.emplace_back(tag.data.begin() + static_cast<std::ptrdiff_t>(at + 1), tag.data.end());
        }
    }
}

} // namespace

static void init_avm1_run_tests() {
    auto& suite = suite_AVM1Run;

    suite.addTest("Arithmetic, strings and comparisons as the VM has them", []() {
        same("var a = 7; var b = 2;\ntrace(a + b); trace(a - b); trace(a * b); trace(a / b); trace(a % b);\n"
             "trace('n' + a); trace(a > b); trace(a <= b); trace(a == '7'); trace(a === '7'); trace(a != 3);\n"
             "trace(a & 3); trace(a | 8); trace(a ^ 1); trace(a << 2); trace(-a >> 1); trace(-a >>> 28); trace(~a);\n"
             "trace(!a); trace(typeof a); trace(typeof 'x'); trace(typeof undefined); trace(typeof {});\n"
             "trace(a > b ? 'more' : 'less'); trace(null || 'none'); trace(0 && 'never'); trace('a' < 'b');\n"
             "trace(1 + '2' + 3); trace('5' * '2'); trace(null == undefined); trace(null === undefined);");
    });

    suite.addTest("Loops, switch and labels as the VM has them", []() {
        same("var s = '';\nfor (var i = 0; i < 10; i++) {\n  if (i == 2) continue;\n  if (i == 5) break;\n"
             "  s += i;\n}\ntrace(s);\nvar n = 0; while (n < 3) n++; trace(n);\n"
             "var k = 5; do { k--; } while (k > 2); trace(k);\n"
             "var t = '';\nouter: for (var a = 0; a < 3; a++) {\n  for (var b = 0; b < 3; b++) {\n"
             "    if (b == 1) continue outer;\n    if (a == 2) break outer;\n    t += a + '' + b + ' ';\n  }\n}\n"
             "trace(t);\nfunction name(n) {\n  var out = '';\n  switch (n) {\n    case 1: out += 'one'; break;\n"
             "    case 2: out += 'two';\n    case 3: out += 'three'; break;\n    default: out += 'many';\n  }\n"
             "  return out;\n}\ntrace(name(1)); trace(name(2)); trace(name(3)); trace(name(9));");
    });

    suite.addTest("Functions, closures and arguments as the VM has them", []() {
        same("function add(a, b) { return a + b; }\ntrace(add(2, 3));\n"
             "function count() { return arguments.length; }\ntrace(count(1, 2, 3));\n"
             "function missing(a, b) { return b; }\ntrace(missing(1));\n"
             "var counter = function () {\n  var n = 0;\n  return function () { n++; return n; };\n}();\n"
             "counter(); trace(counter());\n"
             "function fact(n) { return n <= 1 ? 1 : n * fact(n - 1); }\ntrace(fact(6));\n"
             "var f = function (x) { return x * 2; };\ntrace(f.call(null, 4)); trace(f.apply(null, [5]));\n"
             "var i = 5; var j = i++; trace(i + ' ' + j); j = --i; trace(j);");
    });

    suite.addTest("Objects, arrays and the VM's classes", []() {
        same("var list = [1, 2, 3];\nlist.push(4);\ntrace(list.length); trace(list.join('-'));\n"
             "var cap = {color: 'red', size: 2};\ncap.size += 3;\ncap['color'] = 'blue';\n"
             "trace(cap.color + ' ' + cap.size);\nvar o = {}; o.k = 1; o.k++; ++o.k; trace(o.k); delete o.k; trace(o.k);\n"
             "var sorted = [3, 1, 2]; sorted.sort(function (a, b) { return a - b; }); trace(sorted);\n"
             "var nested = {inner: {list: [10, 20]}}; trace(nested.inner.list[1]);\n"
             "list[6] = 7; trace(list.length);\nvar words = 'a,b,c'.split(','); trace(words.length + words[2]);\n"
             "trace('Hello'.toUpperCase()); trace('Hello'.substr(1, 3)); trace('Hello'.indexOf('l'));\n"
             "trace('Hello'.charAt(1)); trace('Hello'.length);\n"
             "trace(Math.floor(3.7)); trace(Math.max(2, 5)); trace(Math.abs(-3)); trace(Math.round(2.5));\n"
             "trace(parseInt('42px')); trace(isNaN('x')); trace(String(12)); trace(Number('7') + 1);\n"
             "var made = new Array(1, 2); trace(made.length); var plain = new Object(); plain.a = 1; trace(plain.a);");
    });

    suite.addTest("try, catch, finally and throw as the VM has them", []() {
        same("try {\n  trace('in');\n  throw 'oops';\n  trace('not here');\n} catch (e) {\n"
             "  trace('caught ' + e);\n} finally {\n  trace('finally');\n}\n"
             "function risky() { throw new Error('bad'); }\n"
             "try { risky(); } catch (err) { trace(err.message); }\n"
             "try { trace('fine'); } catch (e2) { trace('no'); }\n"
             "function inner() { try { return 'from try'; } finally { trace('cleanup'); } }\ntrace(inner());\n"
             "try { try { throw 1; } finally { trace('inner finally'); } } catch (outer) { trace('outer ' + outer); }\n"
             "trace('end');");
    });

    suite.addTest("with, and clips as the host has them", []() {
        same("var box = {size: 1};\nwith (box) {\n  size = 4;\n  trace(size);\n}\ntrace(box.size);\n"
             "hat._visible = false;\nhat._x = hat._x + 20;\nhat.gotoAndStop(3);\ntrace(hat._x + ' ' + hat._visible);\n"
             "trace(hat._currentframe); trace(hat.bow._name); trace(hat._parent == this);\n"
             "with (hat) { _x = 4; }\ntrace(hat._x); score = 5; trace(score); trace(this.score);");
    });

    suite.addTest("Timeline actions go to the host", []() {
        Run run;
        expect(run.script("gotoAndPlay(3);\nif (true) { stop(); }\ngetURL('page.html', '_blank');\n"
                          "tellTarget ('hat') { gotoAndStop('end'); nextFrame(); }\nprevFrame();\n"
                          "_root.hat.bow.play();\ngotoAndStop('ending');\nstartDrag('hat', true, 1, 2, 3, 4);\n"
                          "stopDrag();\nduplicateMovieClip('hat', 'hat2', 5);\nremoveMovieClip('hat2');\n"
                          "fscommand('quit', '');\nloadMovie('other.swf', 'hat');"),
               "");
        expect(joined(run.movie.log),
               "_level0 gotoFrame 2\n_level0 play\n_level0 stop\ngetURL page.html _blank 0\ntarget _level0.hat\n"
               "hat gotoFrame 4\nhat nextFrame\ntarget _level0\n_level0 prevFrame\nbow play\n_level0 gotoFrame 7\n"
               "hat drag lock 1 2 3 4\nstopDrag\nhat duplicate hat2 5\n"
               "getURL FSCommand:quit  0\ngetURL other.swf hat 0 target\n");
    });

    suite.addTest("MMExecute reaches the tool holding the movie", []() {
        Run run;
        expect(run.script("trace(MMExecute('fl.getDocumentDOM().name'));\nMMEndCommand(true, '');"), "shot.fla\n");
        expect(joined(run.movie.log), "MMExecute fl.getDocumentDOM().name\n");
    });

    suite.addTest("Properties by number, paths and targets", []() {
        Run run;
        expect(run.script("setProperty('hat', _x, 12);\ntrace(getProperty('hat', _x));\n"
                          "setProperty('/hat/bow', _visible, false);\ntrace(hat.bow._visible);\n"
                          "trace(getProperty('hat', _target)); trace(getProperty('', _name)); trace(_root.hat._target);\n"
                          "hat.size = 3;\ntrace(eval('/hat:size')); trace(eval('_root.hat.size')); trace(eval('hat.size'));\n"
                          "set('/hat:size', 9); trace(hat.size); set('hat.bow.depth', 2); trace(hat.bow.depth);\n"
                          "trace(targetPath(hat.bow)); trace(hat.bow); trace(typeof hat);\n"
                          "trace(_root == this); trace(_level0.hat._x); trace(eval('../hat'));\n"
                          "tellTarget ('hat/bow') { trace(_name); }"),
               "12\nfalse\n/hat\n_level0\n/hat\n3\n3\n3\n9\n2\n_level0.hat.bow\n_level0.hat.bow\nmovieclip\n"
               "true\n12\nundefined\nbow\n");
    });

    suite.addTest("Prototypes, constructors and instanceof", []() {
        expect(viaInterpreter("function Point(x, y) { this.x = x; this.y = y; }\n"
                              "Point.prototype.sum = function () { return this.x + this.y; };\n"
                              "var p = new Point(2, 3);\ntrace(p.sum()); trace(p instanceof Point);\n"
                              "trace(p.__proto__ == Point.prototype); trace(p.constructor == Point);\n"
                              "function Point3(x, y, z) { super(x, y); this.z = z; }\n"
                              "Point3.prototype = new Point();\nObject.prototype.hello = function () { return 'hi'; };\n"
                              "trace(p.hello()); trace([].hello()); trace(Point3.prototype.sum != undefined);\n"
                              "String.prototype.twice = function () { return this + this; };\ntrace('ab'.twice());\n"
                              "Array.prototype.first = function () { return this[0]; };\ntrace([5, 6].first());\n"
                              "MovieClip.prototype.where = function () { return this._name; };\ntrace(hat.where());\n"
                              "var o = {}; o.__proto__ = p; trace(o.x);\nfor (var k in p) trace('own ' + k);"),
               "5\ntrue\ntrue\ntrue\nhi\nhi\ntrue\nabab\n5\nhat\n2\nown x\nown y\nown sum\nown hello\n");
    });

    suite.addTest("ActionScript 2 classes, interfaces, getters and setters", []() {
        const std::string animalSource =
            "class zoo.Animal {\n  var legs:Number = 4;\n  var name:String;\n"
            "  static var count:Number = 0;\n"
            "  function Animal(n:String) { name = n; count++; }\n"
            "  function speak():String { return name + ' makes a sound'; }\n"
            "  function get title():String { return 'the ' + name; }\n"
            "  function set title(t:String) { name = t; }\n"
            "  static function made():Number { return count; }\n}";
        const std::string dogSource =
            "import zoo.Animal;\nclass zoo.Dog extends Animal {\n"
            "  function Dog(n:String) { super(n); legs = 3; }\n"
            "  function speak():String { return super.speak() + ': woof'; }\n"
            "  function describe():String { var name = 'shadowed'; return name + ' ' + speak(); }\n}";
        avm1::Options options;
        options.classes = {"zoo.Animal", "zoo.Dog"};
        for (const std::string* source : {&animalSource, &dogSource}) {
            avm1::Compiler lister;
            std::string name;
            avm1::ClassMembers members;
            if (!lister.classMembers(*source, name, members)) throw std::runtime_error("members not listed");
            options.members[name] = members;
        }
        Run run;
        for (const std::string* source : {&animalSource, &dogSource}) {
            avm1::Compiler compiler(options);
            avm1::ClassCode code;
            if (!compiler.compileClass(*source, code)) throw std::runtime_error(joined(compiler.errors()));
            expect(run.actions(code.actions), "");
        }
        expect(run.script("var d = new zoo.Dog('Rex');\ntrace(d.speak());\ntrace(d.legs + ' ' + d.title);\n"
                          "trace(d instanceof zoo.Animal); trace(d instanceof zoo.Dog);\n"
                          "var a = new zoo.Animal('Cat');\ntrace(a.speak()); trace(a instanceof zoo.Dog);\n"
                          "trace(zoo.Animal.made() + ' ' + a.legs);\ntrace(d.describe());\n"
                          "a.title = 'Tom'; trace(a.speak());\ntrace(name + ' ' + legs);\n"
                          "var methods = 0; for (var m in d) methods++; trace(methods);"),
               "Rex makes a sound: woof\n3 the Rex\ntrue\ntrue\nCat makes a sound\nfalse\n2 4\n"
               "shadowed Rex makes a sound: woof\nTom makes a sound\nundefined undefined\n2\n");
        // implements: ImplementsOp, by hand, as the compiler leaves it out.
        run.script("function Pet() {}\nfunction Cat() {}\nvar c = new Cat();");
        Assembler implement;
        implement.str("Pet").op(avm1::GetVariable).num(1).str("Cat").op(avm1::GetVariable).op(avm1::ImplementsOp);
        expect(run.actions(implement.done()), "");
        expect(run.script("trace(c instanceof Pet); trace(c instanceof Cat); trace(d instanceof Pet);"
                          "trace(c instanceof Object);"),
               "true\ntrue\nfalse\ntrue\n");
    });

    suite.addTest("Object.registerClass gives clips their class", []() {
        Run run;
        expect(run.script("function Hat() { this.made = true; }\nHat.prototype = new MovieClip();\n"
                          "Hat.prototype.tip = function () { return 'tipped ' + this._name; };\n"
                          "Object.registerClass('HatSymbol', Hat);"),
               "");
        const Val hat = run.interpreter.registeredClass("HatSymbol");
        if (!hat.isFunction()) throw std::runtime_error("not registered");
        run.interpreter.applyClass(run.movie.bow, hat);
        expect(run.script("trace(hat.bow.tip()); trace(hat.bow.made); trace(hat.bow instanceof Hat);"),
               "tipped bow\ntrue\ntrue\n");
    });

    suite.addTest("addProperty, ASSetPropFlags and _global", []() {
        expect(viaInterpreter("var o = {};\nvar kept = 0;\n"
                              "o.addProperty('v', function () { return kept * 2; }, function (n) { kept = n; });\n"
                              "o.v = 4; trace(o.v); trace(kept);\n"
                              "var h = {a: 1, b: 2, c: 3};\nASSetPropFlags(h, 'a,c', 1);\n"
                              "for (var k in h) trace(k);\nASSetPropFlags(h, null, 0, 1);\nvar n = 0; for (k in h) n++; trace(n);\n"
                              "_global.shared = 'everywhere'; trace(shared);\nfunction f() { return shared; } trace(f());\n"
                              "shared = 'timeline'; trace(_global.shared + ' ' + shared);"),
               "8\n4\nb\n3\neverywhere\neverywhere\neverywhere timeline\n");
    });

    suite.addTest("SWF versions: undefined, booleans and the case of names", []() {
        const std::string source = "var u;\ntrace(u + 'x'); trace(u + 1); trace(Boolean('abc')); trace(!'abc');\n"
                                   "var Score = 3; trace(score); trace(typeof u); trace(u);";
        expect(viaInterpreter(source, 6), "x\n1\nfalse\ntrue\n3\nundefined\nundefined\n");
        expect(viaInterpreter(source, 7), "undefinedx\nNaN\ntrue\nfalse\nundefined\nundefined\nundefined\n");
    });

    suite.addTest("Numbers as ActionScript 1 writes them", []() {
        expect(viaInterpreter("trace(0.1 + 0.2); trace(1 / 3); trace(1e21); trace(123456789012345678);\n"
                              "trace(0.00001); trace(0.000001); trace(-2.5); trace(1 / 0); trace(0 / 0); trace(2e15);"),
               "0.3\n0.333333333333333\n1e+21\n1.23456789012346e+17\n0.00001\n1e-6\n-2.5\nInfinity\nNaN\n2e+15\n");
    });

    suite.addTest("Strings by character, SWF 4's actions", []() {
        expect(viaInterpreter("trace(chr(65) + ord('a')); trace(substring('Phoenix', 2, 3)); trace(length('abc'));\n"
                              "trace(mbchr(233) + mblength('\\u00e9t\\u00e9') + mbord('\\u00e9'));\n"
                              "trace(mbsubstring('h\\u00e9llo', 2, 2)); trace(int(3.7)); trace(int(-3.7));"),
               "A97\nhoe\n3\n\xC3\xA9" "3233\n\xC3\xA9l\n3\n-3\n");
        // Push "a" 5, SetVariable; a + 3 by Add; 1 == 1 by Equals, in SWF 4
        // a number; StringExtract and StringLength.
        Assembler code;
        code.str("a").num(5).op(avm1::SetVariable);
        code.str("a").op(avm1::GetVariable).num(3).op(avm1::Add).op(avm1::Trace);
        code.num(1).num(1).op(avm1::Equals).op(avm1::Trace);
        code.num(2).num(1).op(avm1::Less).op(avm1::Trace);
        code.str("flash").num(2).num(3).op(avm1::StringExtract).op(avm1::Trace);
        code.str("abc").op(avm1::StringLength).op(avm1::Trace);
        code.num(1).num(0).op(avm1::Divide).op(avm1::Trace);
        code.num(1).num(0).op(avm1::And).op(avm1::Trace);
        code.str("b").str("a").op(avm1::StringGreater).op(avm1::Trace);
        code.str("b").str("a").op(avm1::StringLess).op(avm1::Trace);
        code.str("a").str("a").op(avm1::StringEquals).op(avm1::Trace);
        code.str("a").str("b").op(avm1::StringAdd).op(avm1::Trace);
        Run swf4(4);
        expect(swf4.actions(code.done()), "8\n1\n0\nlas\n3\n#ERROR#\n0\n1\n0\n1\nab\n");
    });

    suite.addTest("Errors are told, and runaway scripts stopped", []() {
        Run run;
        expect(run.script("function loop(n) { return loop(n + 1); }\nloop(0);\ntrace('not here');"),
               "error: test: 256 levels of recursion were exceeded\n");
        expect(run.script("throw 'away';"), "error: test: uncaught exception: away\n");
        expect(run.script("undefinedFunction(); nothing.here.at.all = 1; trace(nothing.here); trace('on');"),
               "undefined\non\n");
        // Damaged actions stop without harm.
        expect(run.actions({0x96, 0xFF, 0x00, 0x00}), "");
        expect(run.actions({0x8E, 0x02, 0x00}), "");
        expect(run.script("while (true) {}"), "error: test: a script ran too long and was stopped\n");
    });

    suite.addTest("Handlers run on the timeline they are called on", []() {
        Run run;
        avm1::Compiler compiler;
        const std::vector<avm1::Handler> handlers =
            compiler.compileHandlers("on (release) { trace('released on ' + _name); gotoAndStop(2); }");
        if (handlers.size() != 1) throw std::runtime_error("no handler");
        const Val handler = run.interpreter.handler(shared(handlers[0].actions), "button");
        run.vm.call(handler, {}, run.movie.hat, run.movie.hat);
        expect(run.host.traced, "released on hat\n");
        expect(joined(run.movie.log), "hat gotoFrame 1\n");
        // onEnterFrame and listeners, called by the host.
        run.script("hat.onEnterFrame = function () { trace('entered ' + this._name); };");
        run.host.traced.clear();
        run.interpreter.callMethod(run.movie.hat, "onEnterFrame", {}, "hat");
        expect(run.host.traced, "entered hat\n");
        // setInterval on a method, by name, and on a function.
        expect(run.script("var o = {tick: function () {}};\nvar a = setInterval(o, 'tick', 10);\n"
                          "var b = setInterval(function () {}, 10);\ntrace(typeof a + ' ' + (b > a));\n"
                          "clearInterval(a); clearInterval(b); trace(setInterval(undefined, 10));"),
               "number true\nundefined\n");
    });

    suite.addTest("Real movies' actions run without errors", []() {
        // Folders of SWF movies to try, separated by semicolons, in
        // AS3_AVM1_SWFS; nothing to do without.
        const char* folders = std::getenv("AS3_AVM1_SWFS");
        if (!folders) return;
        std::string list = folders;
        size_t movies = 0, blocks = 0, failed = 0;
        for (size_t from = 0; from <= list.size();) {
            const size_t semicolon = list.find(';', from);
            const std::string folder = list.substr(from, semicolon == std::string::npos ? std::string::npos : semicolon - from);
            from = semicolon == std::string::npos ? list.size() + 1 : semicolon + 1;
            if (folder.empty() || !std::filesystem::exists(folder)) continue;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".swf") continue;
                std::ifstream in(entry.path(), std::ios::binary);
                std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                std::vector<SwfTag> tags;
                try {
                    tags = swfTags(file);
                } catch (const std::exception&) {
                    continue;
                }
                std::vector<std::vector<uint8_t>> init, frames, buttons;
                gatherActions(tags, init, frames, buttons);
                if (init.empty() && frames.empty() && buttons.empty()) continue;
                ++movies;
                Run run(file.size() > 3 ? file[3] : 8);
                size_t errors = 0;
                std::string first;
                for (const auto* group : {&init, &frames, &buttons}) {
                    for (const std::vector<uint8_t>& actions : *group) {
                        ++blocks;
                        const std::string out = run.actions(actions);
                        const size_t at = out.find("error: ");
                        if (at != std::string::npos) {
                            ++errors;
                            if (first.empty()) first = out.substr(at, out.find('\n', at) - at);
                        }
                    }
                }
                failed += errors;
                std::cout << "  " << entry.path().filename().string() << ": " << (init.size() + frames.size() + buttons.size())
                          << " blocks, " << errors << " errors" << (first.empty() ? "" : " (" + first + ")") << std::endl;
            }
        }
        std::cout << "  " << movies << " movies, " << blocks << " action blocks, " << failed << " with errors" << std::endl;
    });
}

static struct AVM1RunTestInit {
    AVM1RunTestInit() { init_avm1_run_tests(); }
} avm1_run_test_init;
