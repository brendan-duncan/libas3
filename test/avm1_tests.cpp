#include "test_runner.h"
#include "avm1_machine.h"
#include "../src/avm1/avm1.h"

#include <stdexcept>

using namespace as3test;

// ActionScript 1 and 2 compiled to SWF action bytecode: the instructions it
// gives, and what they do when the test machine runs them.
TEST_SUITE(AVM1)

namespace {

std::vector<uint8_t> compile(const std::string& source, const as3::avm1::Options& options = as3::avm1::Options()) {
    as3::avm1::Compiler compiler(options);
    std::vector<uint8_t> actions = compiler.compileScript(source);
    if (compiler.hasErrors() || actions.empty()) {
        std::string errors;
        for (const std::string& error : compiler.errors()) errors += error + "\n";
        throw std::runtime_error("did not compile: " + errors);
    }
    return actions;
}

std::string listing(const std::string& source, const as3::avm1::Options& options = as3::avm1::Options()) {
    return as3::avm1::disassemble(compile(source, options));
}

// What the script traces when run.
std::string traces(const std::string& source) {
    avm1test::Machine machine;
    machine.run(compile(source));
    return machine.traced;
}

void expect(const std::string& got, const std::string& wanted) {
    if (got != wanted) {
        throw std::runtime_error("got\n" + got + "\nwanted\n" + wanted);
    }
}

std::string joined(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& line : lines) out += line + "\n";
    return out;
}

// The first error a script gives.
std::string firstError(const std::string& source) {
    as3::avm1::Compiler compiler;
    compiler.compileScript(source);
    return compiler.errors().empty() ? std::string() : compiler.errors().front();
}

} // namespace

static void init_avm1_tests() {
    auto& suite = suite_AVM1;

    suite.addTest("Variables and trace, with a constant pool", []() {
        expect(listing("var a = 1;\ntrace(a);"),
               "ConstantPool \"a\"\n"
               "Push \"a\" 1\n"
               "DefineLocal\n"
               "Push \"a\"\n"
               "GetVariable\n"
               "Trace\n"
               "End\n");
    });

    suite.addTest("Assignments, members and calls", []() {
        expect(listing("x = y.z;\n_root.hat._alpha = 50;\nfoo(1, 'a');\nhat.gotoAndStop(3);"),
               "ConstantPool \"x\" \"y\" \"z\" \"_root\" \"hat\" \"_alpha\" \"a\" \"foo\" \"gotoAndStop\"\n"
               "Push \"x\" \"y\"\n"
               "GetVariable\n"
               "Push \"z\"\n"
               "GetMember\n"
               "SetVariable\n"
               "Push \"_root\"\n"
               "GetVariable\n"
               "Push \"hat\"\n"
               "GetMember\n"
               "Push \"_alpha\" 50\n"
               "SetMember\n"
               "Push \"a\" 1 2 \"foo\"\n"
               "CallFunction\n"
               "Pop\n"
               "Push 3 1 \"hat\"\n"
               "GetVariable\n"
               "Push \"gotoAndStop\"\n"
               "CallMethod\n"
               "Pop\n"
               "End\n");
    });

    suite.addTest("Numbers: integers and doubles", []() {
        expect(listing("trace(2147483647); trace(2147483648); trace(0.5); trace(-3);"),
               "Push 2147483647\nTrace\nPush 2147483648.0\nTrace\nPush 0.5\nTrace\nPush -3\nTrace\nEnd\n");
    });

    suite.addTest("Timeline actions", []() {
        expect(listing("stop(); play(); gotoAndPlay(5); gotoAndStop('end'); nextFrame(); prevFrame();"),
               "Stop\nPlay\nGotoFrame 4\nPlay\nGotoLabel \"end\"\nNextFrame\nPrevFrame\nEnd\n");
        expect(listing("gotoAndStop(n);"),
               "ConstantPool \"n\"\nPush \"n\"\nGetVariable\nGotoFrame2\nEnd\n");
        expect(listing("getURL('http://a.com/', '_blank'); getURL(u, '_self', 'POST');"),
               "ConstantPool \"u\" \"_self\"\n"
               "GetURL \"http://a.com/\" \"_blank\"\n"
               "Push \"u\"\nGetVariable\nPush \"_self\"\nGetURL2 POST\nEnd\n");
        expect(listing("fscommand('quit');"), "GetURL \"FSCommand:quit\" \"\"\nEnd\n");
        expect(listing("loadMovie('a.swf', box);"),
               "ConstantPool \"a.swf\" \"box\"\nPush \"a.swf\" \"box\"\nGetVariable\nGetURL2 target\nEnd\n");
        expect(listing("loadVariablesNum('v.txt', 0);"),
               "ConstantPool \"v.txt\" \"_level0\"\nPush \"v.txt\" \"_level0\"\nGetURL2 variables\nEnd\n");
    });

    suite.addTest("Scenes: frames count from the scene's start", []() {
        as3::avm1::Options options;
        options.scenes = {{"Scene 1", 0}, {"Scene 2", 10}};
        options.sceneStart = 10;
        options.nextScene = -1;
        options.prevScene = 0;
        expect(listing("gotoAndPlay(3); gotoAndStop('Scene 1', 2); prevScene(); nextScene();", options),
               "GotoFrame 12\nPlay\nGotoFrame 1\nGotoFrame 0\nPlay\nEnd\n");
        expect(listing("gotoAndPlay(n);", options),
               "ConstantPool \"n\"\nPush \"n\"\nGetVariable\nGotoFrame2 play bias 10\nEnd\n");
    });

    suite.addTest("Properties by number, clips made and dragged", []() {
        expect(listing("setProperty('hat', _x, getProperty('hat', _y) + 1);"),
               "ConstantPool \"hat\"\n"
               "Push \"hat\" 0 \"hat\" 1\n"
               "GetProperty\n"
               "Push 1\n"
               "Add2\n"
               "SetProperty\n"
               "End\n");
        expect(listing("duplicateMovieClip(ball, 'b2', 3); removeMovieClip('b2'); startDrag('hat', true); stopDrag();"),
               "ConstantPool \"ball\" \"b2\" \"hat\"\n"
               "Push \"ball\"\nGetVariable\nPush \"b2\" 16387\nCloneSprite\n"
               "Push \"b2\"\nRemoveSprite\n"
               "Push 0 true \"hat\"\nStartDrag\n"
               "EndDrag\nEnd\n");
    });

    suite.addTest("Functions keep their locals in registers", []() {
        expect(listing("function add(a, b) {\n  var c = a + b;\n  return c;\n}"),
               "DefineFunction2 \"add\" (a=r:1, b=r:2) registers 4 {\n"
               "    Push r:1 r:2\n"
               "    Add2\n"
               "    StoreRegister r:3\n"
               "    Pop\n"
               "    Push r:3\n"
               "    Return\n"
               "}\n"
               "End\n");
        // this and arguments go in registers of their own when used.
        expect(listing("f = function () { return this._x + arguments.length; };"),
               "ConstantPool \"f\" \"_x\" \"length\"\n"
               "Push \"f\"\n"
               "DefineFunction2 \"\" () registers 3 preload this arguments {\n"
               "    Push r:1 \"_x\"\n"
               "    GetMember\n"
               "    Push r:2 \"length\"\n"
               "    GetMember\n"
               "    Add2\n"
               "    Return\n"
               "}\n"
               "SetVariable\n"
               "End\n");
        // A local a nested function uses stays a name, for it to find.
        expect(listing("function outer() {\n  var n = 1;\n  var m = 2;\n  return function () { return n; };\n}"),
               "ConstantPool \"n\"\n"
               "DefineFunction2 \"outer\" () registers 2 {\n"
               "    Push \"n\" 1\n"
               "    DefineLocal\n"
               "    Push 2\n"
               "    StoreRegister r:1\n"
               "    Pop\n"
               "    DefineFunction2 \"\" () registers 1 {\n"
               "        Push \"n\"\n"
               "        GetVariable\n"
               "        Return\n"
               "    }\n"
               "    Return\n"
               "}\n"
               "End\n");
    });

    suite.addTest("Jumps for if, loops and logic", []() {
        expect(listing("if (a) { b(); } else { c(); }"),
               "ConstantPool \"a\" \"b\" \"c\"\n"
               "Push \"a\"\n"
               "GetVariable\n"
               "Not\n"
               "If L1\n"
               "Push 0 \"b\"\n"
               "CallFunction\n"
               "Pop\n"
               "Jump L2\n"
               "L1:\n"
               "Push 0 \"c\"\n"
               "CallFunction\n"
               "Pop\n"
               "L2:\n"
               "End\n");
        expect(listing("x = a && b;"),
               "ConstantPool \"x\" \"a\" \"b\"\n"
               "Push \"x\" \"a\"\n"
               "GetVariable\n"
               "PushDuplicate\n"
               "Not\n"
               "If L1\n"
               "Pop\n"
               "Push \"b\"\n"
               "GetVariable\n"
               "L1:\n"
               "SetVariable\n"
               "End\n");
    });

    suite.addTest("Arithmetic, strings and comparison run", []() {
        expect(traces("var a = 7; var b = 2;\ntrace(a + b); trace(a - b); trace(a * b); trace(a / b); trace(a % b);\n"
                      "trace('n' + a); trace(a > b); trace(a <= b); trace(a == '7'); trace(a === '7');\n"
                      "trace(a & 3); trace(a | 8); trace(a ^ 1); trace(a << 2); trace(-a >> 1); trace(~a);\n"
                      "trace(!a); trace(typeof a); trace(typeof 'x'); trace(typeof undefined);"),
               "9\n5\n14\n3.5\n1\nn7\ntrue\nfalse\ntrue\nfalse\n3\n15\n6\n28\n-4\n-8\nfalse\nnumber\nstring\nundefined\n");
    });

    suite.addTest("Loops, break, continue and labels run", []() {
        expect(traces("var s = '';\nfor (var i = 0; i < 10; i++) {\n  if (i == 2) continue;\n  if (i == 5) break;\n"
                      "  s += i;\n}\ntrace(s);\n"
                      "var n = 0; while (n < 3) n++; trace(n);\n"
                      "var k = 5; do { k--; } while (k > 2); trace(k);\n"
                      "var t = '';\nouter: for (var a = 0; a < 3; a++) {\n  for (var b = 0; b < 3; b++) {\n"
                      "    if (b == 1) continue outer;\n    if (a == 2) break outer;\n    t += a + '' + b + ' ';\n"
                      "  }\n}\ntrace(t);"),
               "0134\n3\n2\n00 10 \n");
    });

    suite.addTest("switch runs, falling through to the next case", []() {
        expect(traces("function name(n) {\n  var out = '';\n  switch (n) {\n    case 1: out += 'one'; break;\n"
                      "    case 2: out += 'two';\n    case 3: out += 'three'; break;\n    default: out += 'many';\n"
                      "  }\n  return out;\n}\ntrace(name(1)); trace(name(2)); trace(name(3)); trace(name(9));\n"
                      "trace(name('1'));"),
               "one\ntwothree\nthree\nmany\nmany\n");
    });

    suite.addTest("for-in runs, and a break out of it leaves the stack clean", []() {
        expect(traces("var o = {a: 1, b: 2, c: 3};\nvar sum = 0;\nfor (var k in o) sum += o[k];\ntrace(sum);\n"
                      "function first(obj) {\n  for (var name in obj) {\n    for (var again in obj) {\n"
                      "      return name;\n    }\n  }\n}\ntrace(first(o) != undefined);\n"
                      "var count = 0;\nfor (var j in o) { count++; break; }\ntrace(count);\ntrace('after');"),
               "6\ntrue\n1\nafter\n");
    });

    suite.addTest("Objects, arrays, methods and closures run", []() {
        expect(traces("var list = [1, 2, 3];\nlist.push(4);\ntrace(list.length); trace(list.join('-'));\n"
                      "var hat = {color: 'red', size: 2};\nhat.size += 3;\nhat['color'] = 'blue';\n"
                      "trace(hat.color + ' ' + hat.size);\n"
                      "var counter = function () {\n  var n = 0;\n  return function () { n++; return n; };\n}();\n"
                      "counter(); trace(counter());\n"
                      "var o = {}; o.k = 1; o.k++; ++o.k; trace(o.k); trace(o.k--); trace(o.k);\n"
                      "var i = 5; var j = i++; trace(i + ' ' + j); j = --i; trace(j);\n"
                      "delete o.k; trace(o.k);\n"
                      "function fact(n) { return n <= 1 ? 1 : n * fact(n - 1); }\ntrace(fact(6));\n"
                      "var v = null; trace(v || 'none');\ntrace('abc'.length); trace('abc'.toUpperCase());"),
               "4\n1-2-3-4\nblue 5\n2\n3\n3\n2\n6 5\n5\nundefined\n720\nnone\n3\nABC\n");
    });

    suite.addTest("Compound assignment evaluates its target once", []() {
        expect(traces("var calls = 0;\nvar o = {n: 1};\nfunction get() { calls++; return o; }\n"
                      "get().n += 5;\nget()['n'] *= 2;\ntrace(o.n + ' ' + calls);"),
               "12 2\n");
    });

    suite.addTest("try, catch, finally and throw run", []() {
        expect(traces("try {\n  trace('in');\n  throw 'oops';\n  trace('not here');\n} catch (e) {\n"
                      "  trace('caught ' + e);\n} finally {\n  trace('finally');\n}\n"
                      "function risky() { throw new Error('bad'); }\n"
                      "try { risky(); } catch (err) { trace(err.message); }\n"
                      "try { trace('fine'); } catch (e2) { trace('no'); }\ntrace('end');"),
               "in\ncaught oops\nfinally\nbad\nfine\nend\n");
        // Typed catches try each type in turn.
        expect(traces("try { throw new Error('typed'); } catch (s:String) { trace('string'); }"
                      " catch (e:Error) { trace('error ' + e.message); }"),
               "error typed\n");
    });

    suite.addTest("finally runs when break, continue or return leaves the try", []() {
        // Out of a loop.
        expect(traces("for (var i = 0; i < 3; i++) {\n  try {\n    if (i == 1) continue;\n"
                      "    if (i == 2) break;\n    trace('body ' + i);\n  } finally {\n    trace('finally ' + i);\n"
                      "  }\n}\ntrace('after ' + i);"),
               "body 0\nfinally 0\nfinally 1\nfinally 2\nafter 2\n");
        // Out of a function, its value kept while the finally block runs.
        expect(traces("function f(n) {\n  try {\n    if (n > 0) return 'big';\n    trace('small');\n"
                      "  } finally {\n    trace('cleanup ' + n);\n  }\n  return 'end';\n}\n"
                      "trace(f(1));\ntrace(f(0));"),
               "cleanup 1\nbig\nsmall\ncleanup 0\nend\n");
        // From a catch block, and through two finally blocks to a label.
        expect(traces("function g() {\n  try {\n    throw 'x';\n  } catch (e) {\n    return 'caught ' + e;\n"
                      "  } finally {\n    trace('g finally');\n  }\n}\ntrace(g());\n"
                      "outer: for (var k = 0; k < 2; k++) {\n  try {\n    try {\n      break outer;\n"
                      "    } finally {\n      trace('inner');\n    }\n  } finally {\n    trace('outer');\n  }\n}\n"
                      "trace('k ' + k);"),
               "g finally\ncaught x\ninner\nouter\nk 0\n");
        // A for-in loop in the try block left by return, a return at the
        // top level, and a try with no way out runs as before.
        expect(traces("function h(o) {\n  try {\n    for (var p in o) return p;\n  } finally {\n"
                      "    trace('h finally');\n  }\n}\ntrace(h({only: 1}));\n"
                      "try { trace('plain'); } finally { trace('plain finally'); }\n"
                      "try { return; } finally { trace('top finally'); }\ntrace('not here');"),
               "h finally\nonly\nplain\nplain finally\ntop finally\n");
    });

    suite.addTest("with runs on its object", []() {
        expect(traces("var hat = {size: 1};\nwith (hat) {\n  size = 4;\n  trace(size);\n}\ntrace(hat.size);"),
               "4\n4\n");
    });

    suite.addTest("Timeline actions run", []() {
        avm1test::Machine machine;
        machine.run(compile("gotoAndPlay(3);\nif (true) { stop(); }\ngetURL('page.html', '_blank');\n"
                            "tellTarget ('hat') { gotoAndStop('end'); }"));
        expect(joined(machine.log),
               "gotoFrame 2\nplay\nstop\ngetURL page.html _blank\nsetTarget hat\ngotoLabel end\nsetTarget \n");
    });

    suite.addTest("Handlers of buttons and movie clips", []() {
        as3::avm1::Compiler compiler;
        const std::vector<as3::avm1::Handler> handlers = compiler.compileHandlers(
            "on (press, release) {\n  gotoAndPlay(2);\n}\nonClipEvent (enterFrame) {\n  _x += 1;\n}\n"
            "on (keyPress \"<Left>\") { trace('left'); }");
        if (compiler.hasErrors() || handlers.size() != 3) throw std::runtime_error("handlers not compiled");
        expect(joined(handlers[0].events), "press\nrelease\n");
        if (handlers[0].clipEvent || !handlers[1].clipEvent) throw std::runtime_error("kinds wrong");
        expect(joined(handlers[1].events), "enterFrame\n");
        expect(joined(handlers[2].events), "keyPress <Left>\n");
        expect(as3::avm1::disassemble(handlers[0].actions), "GotoFrame 1\nPlay\nEnd\n");
        expect(as3::avm1::disassemble(handlers[1].actions),
               "ConstantPool \"_x\"\nPush \"_x\" \"_x\"\nGetVariable\nPush 1\nAdd2\nSetVariable\nEnd\n");
        // Anything but handlers is an error on an instance.
        compiler.compileHandlers("trace('loose');");
        if (!compiler.hasErrors()) throw std::runtime_error("no error for a loose statement");
    });

    suite.addTest("on() on a frame is an error, with its line", []() {
        const std::string error = firstError("stop();\n\non (press) { play(); }");
        if (error.find("line 3") == std::string::npos || error.find("on()") == std::string::npos) {
            throw std::runtime_error("error was: " + error);
        }
        if (firstError("var v:Vector.<int> = new <int>[1];").empty()) {
            throw std::runtime_error("no error for a vector");
        }
    });

    suite.addTest("#initclip goes to its own actions", []() {
        as3::avm1::Compiler compiler;
        const as3::avm1::FrameScript script =
            compiler.compileFrameScript("#initclip\nObject.registerClass('Hat', HatClass);\n#endinitclip\nstop();");
        if (compiler.hasErrors()) throw std::runtime_error("did not compile");
        expect(as3::avm1::disassemble(script.actions), "Stop\nEnd\n");
        expect(as3::avm1::disassemble(script.initActions),
               "ConstantPool \"HatClass\" \"Hat\" \"Object\" \"registerClass\"\n"
               "Push \"HatClass\"\nGetVariable\nPush \"Hat\" 2 \"Object\"\nGetVariable\nPush \"registerClass\"\n"
               "CallMethod\nPop\nEnd\n");
    });

    suite.addTest("ActionScript 2 classes run", []() {
        const std::string animalSource =
            "class zoo.Animal {\n  var legs:Number = 4;\n  var name:String;\n"
            "  static var count:Number = 0;\n"
            "  function Animal(n:String) { name = n; count++; }\n"
            "  function speak():String { return name + ' makes a sound'; }\n"
            "  function get title():String { return 'the ' + name; }\n"
            "  static function made():Number { return count; }\n}";
        const std::string dogSource =
            "import zoo.Animal;\nclass zoo.Dog extends Animal {\n"
            "  function Dog(n:String) { super(n); legs = 3; }\n"
            "  function speak():String { return super.speak() + ': woof'; }\n"
            "  function describe():String { var name = 'shadowed'; return name + ' ' + speak(); }\n}";
        as3::avm1::Options options;
        options.classes = {"zoo.Animal", "zoo.Dog"};
        // What each declares, for the names a class inherits.
        {
            as3::avm1::Compiler lister;
            std::string name;
            as3::avm1::ClassMembers members;
            if (!lister.classMembers(dogSource, name, members) || name != "zoo.Dog" || members.superName != "zoo.Animal") {
                throw std::runtime_error("Dog's members not listed");
            }
            options.members[name] = members;
            if (!lister.classMembers(animalSource, name, members)) throw std::runtime_error("no members");
            expect(joined(members.instance), "legs\nname\nspeak\ntitle\n");
            expect(joined(members.statics), "count\nmade\n");
            options.members[name] = members;
        }
        as3::avm1::Compiler compiler(options);
        as3::avm1::ClassCode animal, dog;
        if (!compiler.compileClass(animalSource, animal)) {
            throw std::runtime_error("Animal: " + joined(compiler.errors()));
        }
        if (!compiler.compileClass(dogSource, dog)) {
            throw std::runtime_error("Dog: " + joined(compiler.errors()));
        }
        expect(animal.name, "zoo.Animal");
        expect(dog.superName, "zoo.Animal");
        expect(joined(dog.needs), "zoo.Animal\n");
        avm1test::Machine machine;
        machine.run(animal.actions);
        machine.run(dog.actions);
        // Defining a class again leaves it as it was.
        machine.run(animal.actions);
        machine.run(compile("var d = new zoo.Dog('Rex');\ntrace(d.speak());\ntrace(d.legs + ' ' + d.title);\n"
                            "trace(d instanceof zoo.Animal);\nvar a = new zoo.Animal('Cat');\ntrace(a.speak());\n"
                            "trace(zoo.Animal.made() + ' ' + a.legs);\ntrace(d.describe());\n"
                            "trace(name + ' ' + legs);"));
        // The members are the instances' own: nothing went on the timeline.
        expect(machine.traced, "Rex makes a sound: woof\n3 the Rex\ntrue\nCat makes a sound\n2 4\n"
                               "shadowed Rex makes a sound: woof\nundefined undefined\n");
    });

    suite.addTest("A class that extends MovieClip finds the clip's own by name", []() {
        as3::avm1::Compiler compiler;
        as3::avm1::ClassCode ball;
        if (!compiler.compileClass("class Ball extends MovieClip {\n  var speed = 2;\n"
                                   "  function onEnterFrame() { _x += speed; if (_x > 100) gotoAndStop(2); }\n}",
                                   ball)) {
            throw std::runtime_error(joined(compiler.errors()));
        }
        const std::string text = as3::avm1::disassemble(ball.actions);
        const std::string method =
            "DefineFunction2 \"\" () registers 2 preload this {\n"
            "    Push r:1 \"_x\" r:1 \"_x\"\n"
            "    GetMember\n"
            "    Push r:1 \"speed\"\n"
            "    GetMember\n"
            "    Add2\n"
            "    SetMember\n"
            "    Push r:1 \"_x\"\n"
            "    GetMember\n"
            "    Push 100\n"
            "    Greater\n"
            "    Not\n"
            "    If L1\n"
            "    Push 2 1 r:1 \"gotoAndStop\"\n"
            "    CallMethod\n"
            "    Pop\n"
            "L1:\n"
            "}\n";
        if (text.find(method) == std::string::npos) throw std::runtime_error("got\n" + text);
    });

    suite.addTest("Imports in a frame script name the classes", []() {
        as3::avm1::Options options;
        options.classes = {"zoo.Dog"};
        expect(listing("import zoo.Dog;\nvar d = new Dog();", options),
               "ConstantPool \"d\" \"zoo\" \"Dog\"\n"
               "Push \"d\" 0 \"zoo\"\n"
               "GetVariable\n"
               "Push \"Dog\"\n"
               "NewMethod\n"
               "DefineLocal\n"
               "End\n");
    });
}

static struct AVM1TestInit {
    AVM1TestInit() { init_avm1_tests(); }
} avm1_test_init;
