# AVM1: SWF Action Bytecode

Flash movies before ActionScript 3 carry their scripts as action bytecode, run by Flash Player's first virtual machine (AVM1): DoAction and DoInitAction tags on frames, and the event handlers of buttons and movie clips. libas3 compiles ActionScript 1 and 2 to these actions, prints them as text, and runs them on the VM.

| Header | |
|--------|-|
| `avm1/avm1.h` | `avm1::Compiler`, `avm1::disassemble`, `actionName` |
| `avm1/avm1_actions.h` | The action codes (`avm1::Push`, `avm1::GetVariable`...) |
| `avm1/avm1_interpreter.h` | `avm1::Interpreter` and the `avm1::Host` it runs against |

The tests are `test/avm1_tests.cpp` (the compiler, by listing and on a small AVM1 machine in `test/avm1_machine.h`) and `test/avm1_run_tests.cpp` (the interpreter). [LANGUAGE.md](LANGUAGE.md#actionscript-1-and-2) describes the language, and [JAVASCRIPT.md](JAVASCRIPT.md#actionscript-1-and-2-to-javascript) translating it to JavaScript instead.

## Compiling

```cpp
#include "avm1/avm1.h"

as3::avm1::Compiler compiler;

// A frame's script: frame.actions is a DoAction tag's body; frame.initActions,
// what was between #initclip and #endinitclip, a DoInitAction's.
as3::avm1::FrameScript frame = compiler.compileFrameScript("stop();\nhat._alpha = 50;", "Scene 1, frame 1");

// An instance's on() and onClipEvent() handlers, for a button's or a clip's
// event tags.
std::vector<as3::avm1::Handler> handlers =
    compiler.compileHandlers("on (release) { gotoAndPlay(2); }\nonClipEvent (enterFrame) { _x += 1; }");
// handlers[0].events == {"release"}, handlers[0].clipEvent == false
// handlers[1].events == {"enterFrame"}, handlers[1].clipEvent == true

// An ActionScript 2 class file, for a DoInitAction.
as3::avm1::ClassCode hat;
if (!compiler.compileClass(classSource, hat, "Hat.as")) { /* compiler.errors() */ }

if (compiler.hasErrors()) {
    for (const std::string& error : compiler.errors()) std::cerr << error << "\n";   // "file, line N: what"
}
```

`compileScript(source)` compiles code that is a script of its own, as a DoAction's, without splitting out `#initclip`. Every result ends with `ActionEnd`; on an error it is empty and `errors()` says why.

### What it makes

- A constant pool of the script's strings, and `DefineFunction2` functions with their parameters and locals in registers (and `this`, `arguments` and `super` in registers where they are used). A local that a nested function uses stays a name, for the nested function to find.
- The timeline's own actions for `play`, `stop`, `gotoAndPlay`, `gotoAndStop`, `nextFrame`, `prevFrame`, `nextScene`, `prevScene`, `getURL`, `fscommand`, `loadMovie`, `loadVariables` (and their `Num` forms), `getProperty`, `setProperty`, `duplicateMovieClip`, `removeMovieClip`, `startDrag`, `stopDrag`, `tellTarget`, `ifFrameLoaded`, `trace` and the rest.
- `if`, the loops, `switch`, `for-in`, `with`, labels, `break` and `continue`, and `try`/`catch`/`finally`, with typed `catch` clauses tried in turn; `finally` runs when `break`, `continue` or `return` leaves the `try`. Compound assignment evaluates its target once.
- ActionScript 2 classes as the prototypes and `Extends` they are made of. In a class's methods, members are found by their bare names (MovieClip's own too, for a class that extends it), getters and setters are made with `addProperty`, and `super(...)` and `super.method()` are the superclass's. Imports and packages name the classes.

```
// stop();\nhat._alpha = 50;
ConstantPool "hat" "_alpha"
Stop
Push "hat"
GetVariable
Push "_alpha" 50
SetMember
End
```

What only ActionScript 3 has is an error: `for each`, rest parameters, vectors, getters and setters outside a class, a class in a frame script, `on()` in a frame script, and the like.

### Options

```cpp
as3::avm1::Options options;
// Scenes of the main timeline: the frame (from 0) each starts on.
options.scenes = {{"Scene 1", 0}, {"Scene 2", 10}};
// Where this script's scene starts, and the next and previous scenes (-1 for none).
options.sceneStart = 10;
options.nextScene = -1;
options.prevScene = 0;
as3::avm1::Compiler compiler(options);
// gotoAndPlay(3) in Scene 2 is GotoFrame 12; gotoAndStop("Scene 1", 2) is GotoFrame 1.
```

On the main timeline frame numbers count from the scene's start, as Flash has them; `sceneStart` is -1 for code that is not on the main timeline. A `gotoAndPlay` to a frame number computed at run time uses `GotoFrame2` with the scene's bias.

For ActionScript 2 classes:

- `options.classes`: the qualified names of the classes the movie has (`"com.studio.Hat"`), so that a short name in a class of the same package, or one imported with `com.studio.*`, finds its class.
- `options.members`: what each class declares, so that a subclass's methods find inherited members by their bare names. `compiler.classMembers(source, name, members)` reads them from a class file without compiling it:

```cpp
as3::avm1::Options options;
options.classes = {"zoo.Animal", "zoo.Dog"};
for (const std::string& source : classFiles) {
    std::string name;
    as3::avm1::ClassMembers members;     // superName, instance, statics
    if (as3::avm1::Compiler().classMembers(source, name, members)) options.members[name] = members;
}
as3::avm1::Compiler compiler(options);
```

`ClassCode` gives the class's `name`, its `superName`, and `needs`: the classes (the superclass and interfaces) to define first, for ordering the DoInitAction tags. A class's actions leave a class already defined as it was.

`implements` is not compiled into an `ImplementsOp`: an instance is not `instanceof` an interface its ActionScript 2 class implements.

## Disassembling

```cpp
std::string listing = as3::avm1::disassemble(frame.actions);
const char* name = as3::avm1::actionName(0x96);    // "Push"
```

One action a line, with jump targets as labels and the bodies of functions, `with` blocks and `try` blocks indented:

```
DefineFunction2 "add" (a=r:1, b=r:2) registers 4 {
    Push r:1 r:2
    Add2
    StoreRegister r:3
    Pop
    Push r:3
    Return
}
End
```

Strings are quoted (a constant pool entry by its text), integers plain, doubles with a decimal point, registers as `r:1`. It reads any DoAction tag's body, not only what the compiler makes.

## The interpreter

`avm1::Interpreter` runs action bytecode (SWF 4 to 8) on the VM, so that an old movie's scripts run in a host built on libas3. Functions the actions define are VM functions, which scripts and the host call as any other, and the objects they make are VM objects, with ActionScript 1's prototypes kept on them. The built-in classes are the VM's (`Object`, `Array`, `String`, `Math`, `Date`...), with AVM1's conversions; the player's ActionScript 2 classes beyond `MovieClip`, `Button` and `TextField` (`Key`, `Mouse`, `Sound`, `Color`, `Stage`, AVM1's `XML`...) are the host's to define.

The host implements `avm1::Host`: its movie clips (values the host made, usually host objects), their timelines, and the player around them. Every method has a default that does nothing:

```cpp
#include "avm1/avm1_interpreter.h"

class MyHost : public as3::avm1::Host {
public:
    void trace(const std::string& text) override { std::cout << text << "\n"; }
    void error(const std::string& message) override { std::cerr << message << "\n"; }
    bool isClip(const as3::Val& value) override { return value.host<Clip>() != nullptr; }
    as3::Val root(const as3::Val& clip) override { return rootClip; }
    as3::Val level(int number) override { return number == 0 ? as3::Val(rootClip) : as3::Val(); }
    as3::Val parent(const as3::Val& clip) override { ... }
    as3::Val child(const as3::Val& clip, const std::string& name) override { ... }
    std::string targetPath(const as3::Val& clip) override { ... }   // "_level0.hat.bow"
    void gotoFrame(const as3::Val& clip, int frame, bool play) override { ... }
    bool gotoLabel(const as3::Val& clip, const std::string& label, bool play) override { ... }
    void setPlaying(const as3::Val& clip, bool playing) override { ... }
    double time() override { return millisecondsSinceStart(); }
    // ...
};

as3::VM vm;
MyHost host;
as3::avm1::Interpreter interpreter(vm, host, swfVersion);   // 8 when not given

auto actions = std::make_shared<const std::vector<uint8_t>>(doActionBody);
interpreter.run(actions, rootClip, "Scene 1, frame 1");     // false when an error stopped it
```

What `Host` answers for:

| | |
|-|-|
| `trace`, `error` | What `trace` gives, and an error that stopped a script |
| `isClip`, `className` | Whether a value is one of the movie's clips, and its class (`"MovieClip"`, `"Button"`, `"TextField"`), whose prototype scripts may add to |
| `root`, `level`, `parent`, `child`, `childNames`, `targetPath` | The clips: `_root`, `_level0`, `_parent`, a child by instance name, the names `for-in` gives, `"_level0.hat.bow"` |
| `gotoFrame`, `gotoLabel`, `setPlaying`, `nextFrame`, `prevFrame`, `framesLoaded`, `callFrame` | Timelines |
| `getProperty`, `setProperty` | The numbered properties (`_x`, `_alpha`...): undefined (or false from `setProperty`) leaves them to the clip's own members |
| `cloneSprite`, `removeSprite`, `startDrag`, `endDrag` | `duplicateMovieClip`, `removeMovieClip`, dragging |
| `getURL`, `toggleQuality`, `stopSounds` | `getURL`, `loadMovie`, `loadVariables`, `fscommand` (as `"FSCommand:..."` URLs) |
| `time` | `getTimer()` |
| `targetChanged` | `tellTarget` sent bare timeline calls to another clip, or back |

The interpreter's own API:

- `run(actions, clip, where)`: runs a DoAction's or DoInitAction's actions on the timeline `clip`, which is their `this` and their target, where their variables go. `where` names it in error messages.
- `handler(actions, where)`: a function that runs a button's or a clip's handler on the timeline it is called with as `this`; the host calls it with `vm.call(handler, {}, clip, clip)` when the event comes.
- `call(function, self, args, where)` and `callMethod(object, name, args, where)`: calls from the host (`onEnterFrame`, a listener's method), an error told to the host rather than thrown.
- `getMember` and `setMember`: members as ActionScript 1 finds them, along prototypes.
- `global()` (`_global`), `prototypeOf(constructor)`.
- `registeredClass(linkage)` and `applyClass(clip, constructor)`: what `Object.registerClass` linked to a library symbol, and making a clip an instance of it (its prototype the class's, the constructor run on it).
- `toString`, `toNumber`, `toBoolean`: conversions as the movie's SWF version has them. `version()` and `setVersion()`.

What it has of ActionScript 1 and 2: prototypes and `__proto__`, constructors and `instanceof`, ActionScript 2 classes, `addProperty`, `ASSetPropFlags`, `_global`, `Object.registerClass`, `setInterval(object, "method", ms)` and `setInterval(function, ms)` (on the VM's timers, which the host runs with `vm.runTimers`), slash and dot paths (`eval("/hat:size")`, `set("hat.bow.depth", 2)`, `"../hat"`), `tellTarget`, SWF 4's string actions, `targetPath`, `getVersion`. Numbers are written with 15 significant digits (`0.1 + 0.2` is `0.3`). Before SWF 7, names are found whatever their case and `undefined` is `""` in strings and 0 in numbers.

As in Flash Player, calling a function that is not there, or reading a member of `undefined`, does nothing and gives `undefined`. A script stops (and the host is told) when it throws an exception nothing catches, recurses more than 256 levels deep, or runs 40 million actions without returning ("a script ran too long and was stopped"). Damaged actions stop without harm. `watch` and `unwatch` are there but do nothing.
