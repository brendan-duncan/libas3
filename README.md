# libas3 - ActionScript 3 Compiler & Bytecode VM

An ActionScript 3 parser, compiler and bytecode virtual machine written in C++17, for running real ActionScript programs (classes in packages, a host's native classes, the built-in library) inside a host application.

## Experimental Status

This project is being developed for my own learning and experimentation. There are no guarantees of accuracy, feature completion, or support.

## Features

### The language
- Classes in packages: constructors with `super(...)` (an implicit `super()` when left out), `super.method()` and `super.prop`, fields initialized for each instance, static members (vars, consts, functions, getters and setters) found unqualified inside the class (and its subclasses) and as `Class.member`, getters and setters (overridden and inherited), `override`, `final`, `dynamic`, and the access attributes (accepted, not enforced)
- Interfaces: declared, extending others, implemented; `is` and `as` with them
- Methods read off an object are bound to it: `addEventListener(MouseEvent.CLICK, onClick)` calls `onClick` with the right `this`, and `obj.f == obj.f`
- Packages: fully qualified names (`com.example.Shape`), single and wildcard imports, classes of the same package without an import, and a class resolver that loads classes when they are first needed (see below)
- Statements: `switch` (with fall-through), `for-in` and `for each-in` (over objects, arrays and host objects), `break` and `continue` (labeled too) in every loop and switch, `try`/`catch` (several, by type)/`finally`/`throw`, labeled statements, `const` locals
- Functions: closures capturing locals and parameters (function-scoped, as ActionScript has them), nested named functions, default parameter values, `...rest`, `arguments`, `Function.call`, `apply` and `length`
- Expressions: `typeof`, `delete`, `instanceof`, `in`, `is`, `as`, `??`, the comma operator, `void`, regular expression literals, `new <int>[1, 2]` vector literals
- Types: `:*`, `:void`, qualified (`flash.display.Sprite`) and generic (`Vector.<Vector.<int>>`) annotations; `int` and `uint` wrap where declared (see Numbers)
- Metadata (`[Embed(...)]`, `[Event(...)]`) is read and kept for the host; conditional compilation constants (`CONFIG::debug`)
- ActionScript 1 and 2: statements may end at a line break, `on (release) { }` and `onClipEvent (...) { }` blocks, `tellTarget`, `ifFrameLoaded`, scripts that run on a timeline

### The library
`Object`, `Array` (with `sort`'s options and `sortOn`), `Vector.<T>`, `String` (counting in UTF-16 units, with regular expressions in `split`, `replace`, `match` and `search`), `Number` (`toFixed`, `toPrecision`, `toString(radix)`), `int`, `uint`, `Boolean`, `Math`, `Date`, `RegExp`, `JSON`, `Error` and its subclasses (`getStackTrace`), `Function`, `flash.utils.Dictionary`, `getDefinitionByName`, `getQualifiedClassName`, `getQualifiedSuperclassName`, `describeType`, `getTimer`, `setTimeout`, `setInterval`, `clearTimeout`, `clearInterval`, and the top-level functions (`trace`, `isNaN`, `isFinite`, `parseInt`, `parseFloat`, `escape`, `unescape`, `encodeURI(Component)`, `decodeURI(Component)`).

Also `with` (names in its body found on its object first) and `include "file.as"` (read through the host, `VM::setIncludeResolver` or `Parser::setIncludeHandler`).

E4X: XML literals (`<item id={n}>{text}</item>`, `<>...</>`), `XML` and `XMLList`, `x.child`, `x.@attribute`, `x..descendant`, `x.*`, `x.@*`, filters (`x.item.(@price > 4)`), assignment and `delete` on them, `for each` over lists, and their methods (`children`, `attribute`, `descendants`, `elements`, `text`, `appendChild`, `toXMLString`...). Namespaces in XML are not.

Not supported: namespaces beyond accepting their syntax (`mx_internal function f()` is a plain method).

### SWF action bytecode (AVM1)
`avm1/avm1.h` compiles ActionScript 1 and 2 to the actions SWF movies carry for Flash Player's first virtual machine, for a host writing SWF files:

```cpp
#include "avm1/avm1.h"

as3::avm1::Compiler compiler;
as3::avm1::FrameScript frame = compiler.compileFrameScript("stop();\nhat._alpha = 50;");
// frame.actions is a DoAction tag's body; frame.initActions, what was
// between #initclip and #endinitclip, a DoInitAction's.
std::vector<as3::avm1::Handler> handlers = compiler.compileHandlers("on (release) { gotoAndPlay(2); }");
as3::avm1::ClassCode hat;
compiler.compileClass(classSource, hat);   // an ActionScript 2 class, for a DoInitAction
std::string listing = as3::avm1::disassemble(frame.actions);
```

It gives a constant pool, `DefineFunction2` functions with their locals in registers (and `this`, `arguments` and `super` where used), the timeline's own actions for `gotoAndPlay`, `getURL`, `getProperty`, `duplicateMovieClip`, `tellTarget` and the rest (frame numbers counted from the scene's start on the main timeline, `Options::scenes`), `switch`, `for-in`, `with`, `try`/`catch`/`finally` (typed catches tried in turn), and ActionScript 2 classes as the prototypes and `Extends` they are made of: members found by their bare names (MovieClip's too, for a class that extends it), getters and setters through `addProperty`, imports and packages. What only ActionScript 3 has is an error.

## Project Structure

```
src/
├── ast/              # AST node definitions
├── lexer/            # Tokenization
├── parser/           # AS3 parser (AST generation)
├── runtime/          # Values, classes, the built-in library
│   ├── value.h/cpp         # Val, Value, ClassInfo, HostObject
│   └── builtins_*.cpp      # Object, Array, String, Math, Date, RegExp, JSON...
├── bytecode/         # Compiler and VM
│   ├── opcode.h/cpp        # Instruction set
│   ├── chunk.h/cpp         # Bytecode, functions and classes as compiled
│   ├── compiler.h/cpp      # AST → Bytecode
│   ├── vm.h/cpp            # The interpreter loop, calls, exceptions
│   └── vm_runtime.cpp      # Properties, names, classes, native classes
├── abc/              # ActionScript 3 → ABC (AVM2 bytecode), and running ABC
│   ├── abc.h               # compile(): sources and timelines to an abcFile
│   ├── abc_domain.h/cpp    # Domain: an abcFile's classes and code run on the VM
│   ├── abc_model.h/cpp     # The front end the compilers share: definitions, classes, timelines
│   ├── abc_file.h/cpp      # The abcFile as data; read() and write()
│   ├── abc_opcodes.h/cpp   # AVM2's instructions, decoding, stack effects
│   ├── abc_verifier.h/cpp  # verify(), disassemble()
│   ├── abc_builder.h/cpp   # Constant pools and method code as they are made
│   ├── abc_player.h/cpp    # The player's classes, by package
│   └── abc_compiler.cpp    # AST → ABC
├── avm1/             # ActionScript 1 and 2 → SWF actions
│   ├── avm1.h              # Compiler, disassemble
│   ├── avm1_actions.h      # The action codes
│   ├── avm1_compiler.cpp   # AST → actions
│   └── avm1_disassembler.cpp
└── js/               # ActionScript 3 → JavaScript, for HTML5 pages
    ├── js.h                # translate(): sources and timelines to JavaScript
    ├── js_translator.cpp   # AST → JavaScript
    ├── js_writer.h/cpp     # Lines, source maps, JavaScript literals
    ├── as3.js              # The runtime translated code needs (js::runtime())
    └── js_runtime.cpp      # as3.js, embedded

test/                 # One executable, a suite per file
```

## Building

```bash
cmake -S . -B build
cmake --build build --config Release
./build/Release/as3_tests.exe            # every suite
./build/Release/as3_tests.exe language   # one suite
```

## Usage

### Running a script
```cpp
#include "bytecode/vm.h"

as3::VM vm;
// Parses, compiles and runs. False (with the error kept) when it fails.
if (!vm.load("var x = 10; trace('Sum:', x + 20);", "frame1.as")) {
    std::cerr << vm.getError() << " at line " << vm.getErrorLine() << "\n" << vm.getErrorStack();
}
```

Parsing and compiling separately, as before, works too: `as3::Parser(source, fileName).parse()`, `as3::Compiler().compile(program)` (which throws `as3::CompileError`, a `std::runtime_error`, with the file and line of each error) and `vm.execute(code, scope)`.

### Values
The VM works with `as3::Val`: a small value that holds a number or a boolean in place and a string or an object by reference. The host's older API, `as3::ValuePtr` (`std::shared_ptr<Value>`), still works, and converts to and from `Val` implicitly:

```cpp
as3::Val n = as3::Val::number(3.5);
as3::Val s = as3::Val::string("text");
as3::Val o = vm.newObject();
o->properties["x"] = n;          // a dynamic property
as3::ValuePtr boxed = n;         // for older host code
double d = vm.getProperty(o, "x").toNumber();
```

`vm.toString(v)` gives a value's text as `String(v)` does (running an object's `toString`); `v.toString()` gives it without running script code.

### Native classes
The host implements classes such as `flash.display.Sprite` or `flash.geom.Rectangle` in C++: a constructor that attaches the host's state, methods, getters and setters, static members and a superclass. Scripts use them as ActionScript classes, and extend them:

```cpp
// The host's state for each Rectangle: a HostObject.
struct RectState : as3::HostObject {
    double x = 0, y = 0, width = 0, height = 0;
};

using namespace as3;

// 'self' is the instance: a Rectangle, or a script class extending it.
static RectState& rect(VM& vm, const Val& self) {
    RectState* state = self.host<RectState>();
    if (!state) vm.throwError("TypeError", "Error #1034: Type Coercion failed: Rectangle expected.", 1034);
    return *state;
}

vm.defineNativeClass("flash.geom.Rectangle")
    // Runs for new Rectangle(...), and for a subclass's super(...).
    .constructor([](VM& vm, const Val& self, Args args) {
        auto state = std::make_shared<RectState>();
        state->x = args.number(0);
        state->y = args.number(1);
        state->width = args.number(2);
        state->height = args.number(3);
        self->host = state;
        return Val();
    })
    .property("width",
              [](VM& vm, const Val& self, Args) { return Val::number(rect(vm, self).width); },
              [](VM& vm, const Val& self, Args args) {   // a setter's one argument is the value
                  rect(vm, self).width = vm.toNumber(args[0]);
                  return Val();
              })
    .getter("right", [](VM& vm, const Val& self, Args) {
        RectState& r = rect(vm, self);
        return Val::number(r.x + r.width);
    })
    .method("isEmpty", [](VM& vm, const Val& self, Args) {
        RectState& r = rect(vm, self);
        return Val::boolean(r.width <= 0 || r.height <= 0);
    })
    // A native method calling one a subclass may override.
    .method("describe", [](VM& vm, const Val& self, Args) {
        return Val::string("<" + vm.toString(vm.callMethod(self, "toString")) + ">");
    })
    .constant("ZERO_WIDTH", Val::number(0))           // Rectangle.ZERO_WIDTH
    .staticMethod("unit", [](VM& vm, const Val& cls, Args) {
        return vm.construct(cls, {Val::number(0), Val::number(0), Val::number(1), Val::number(1)});
    })
    .define();
```

A script class extends it as any class; its instances keep the native state, the native methods and accessors are found unqualified, and overriding works both ways:

```actionscript
package geo {
    import flash.geom.Rectangle;
    public class Box extends Rectangle {
        public function Box(w:Number, h:Number) {
            super(0, 0, w, h);                 // into the native constructor
        }
        override public function get width():Number {
            return super.width * 2;            // the native getter
        }
        override public function toString():String {
            return "Box " + width;             // describe() calls this one
        }
        public function grow():void {
            if (!isEmpty()) width += 1;        // native method and accessor, unqualified
        }
    }
}
```

What the builder takes:
- `extends("pkg.Base")`: the superclass, native or script (defined already, or found by the class resolver); `Object` when not given. A native class without a constructor runs its superclass's.
- `implements("pkg.IFace")`, `dynamic()`, `final()`, `interface()`.
- `method`, `getter`, `setter`, `property`: instance members. `field(name, initial, coerce)`: a field each instance has (as a script `var`).
- `staticMethod`, `staticGetter`, `staticSetter`, `constant`, `staticVar`: members of the class.
- `prototypeMethod`: a method found after an instance's dynamic properties (as `Object.prototype.toString` is), so an object may have its own.
- `callHandler`: what `ClassName(x)` does; a cast when not given.
- `define()` registers it under its qualified name and, unless taken, its short name (frame scripts and older host code find classes so). It returns the class.

Native functions are `NativeMethod`s: `Val (VM& vm, const Val& self, Args args)`. `Args` reads missing arguments as undefined, and has `number(i)`, `integer(i)`, `string(i)`, `boolean(i)`. A native raises an ActionScript error with `vm.throwError("TypeError", "Error #1034: ...", 1034)` (any error class by name); scripts catch it with `try`/`catch`. A C++ exception from a native becomes an `Error` too.

The host's state goes on `self->host`, a `std::shared_ptr<HostObject>`; `self.host<T>()` finds it again (nullptr when the object has none or another type). A `HostObject` may also answer for names (`get`, `set`, as older host objects do), for `obj[key]` (`getIndex`, `setIndex`, `deleteIndex`, `hasIndex`) and for `for-in` (`enumerate`).

When the host makes an instance itself (a movie clip of a symbol whose class is a script class extending `MovieClip`), it attaches its state before the constructors run, and the native constructor keeps it:

```cpp
auto cls = vm.findClass("game.Star");          // the class resolver may load it
as3::Val star = vm.newInstance(cls);           // fields at their defaults
star->host = myClipState;                      // the host's state first
vm.runConstructor(star, as3::Args());          // then Star(), super() and so on
```

### Calling scripts from the host
```cpp
Val result = vm.callFunction(listener, thisValue, {event});    // a function, as from a dispatch
Val text = vm.callMethod(object, "toString");                  // the subclass's override, if any
Val made = vm.construct(vm.findDefinition("geo.Box"), {Val::number(3), Val::number(4)});
Val w = vm.getProperty(made, "width");                         // getters run
vm.setProperty(made, "width", Val::number(5));                 // setters run
```

These throw `as3::ScriptException` (not a `std::exception`) for an ActionScript error, so that from a native function it goes on to the script that called it. The older `vm.call(function, args, this, scope)` catches it and keeps it as the VM's error instead, as `execute` and `load` do.

### Functions and values by name
```cpp
vm.defineFunction("flash.utils.getTimer", [](VM& vm, const Val&, Args) { return Val::number(now()); });
vm.defineFunction("trace", myTrace);                 // a top-level one replaces the built-in
vm.defineValue("com.example.VERSION", Val::string("1.0"));
```

`registerBuiltin`, `setGlobal`, `getGlobal`, `defineExternClass` and `registerObject` work as before.

### The class resolver
When a script needs a class (or function) it does not have, as a superclass, an imported name or a name of its package, the VM asks the host for its source, compiles and runs it, and goes on. The host answers nothing for names it does not have:

```cpp
vm.setClassResolver([](const std::string& qualifiedName) -> std::optional<as3::ClassSource> {
    // "com.example.Shape", or "Shape" for a class without a package.
    std::filesystem::path file = root / (dotsToSlashes(qualifiedName) + ".as");
    if (!std::filesystem::exists(file)) return std::nullopt;
    return as3::ClassSource{readFile(file), file.filename().string()};
});
as3::Val cls = vm.findDefinition("com.example.Main");   // loads Main, and what it extends
```

Classes are kept by their qualified names (`getDefinitionByName("com.example.Main")`, or `"com.example::Main"`) and, unless the name is taken, by their short names. A class's static initializer runs when it is defined. A source that does not compile is a `SyntaxError` naming its file and line.

### Errors
Runtime errors are ActionScript's: `TypeError: Error #1009: Cannot access a property or method of a null object reference.`, `ReferenceError: Error #1065: Variable x is not defined.`, `TypeError: Error #1006: f is not a function.`, `RangeError`, `ArgumentError`. Scripts catch them by type. One not caught stops the script and is kept on the VM:

```cpp
vm.hasError();        // the last run failed
vm.getError();        // "TypeError: Error #1009: ..."
vm.getErrorLine();    // where it was thrown
vm.getErrorStack();   // "\tat com.example::Main/draw()[Main.as:42]\n\tat ..."
vm.getErrorValue();   // what was thrown
```

Runaway recursion is `Error #1023` (also through native calls: their C++ stack is limited, `setNativeStackLimit`), and the VM goes on.

The parser accepts one known slip leniently: two expressions on one line with nothing between them (`options.bleed 0.07;`, an `=` left out) are taken as two statements, with a warning (`Parser::getWarnings`); `Parser::setStrict(true)` makes that an error.

### Numbers
Numbers are doubles, as `Number` is. A value stored in an `int` or `uint` (a variable, field, parameter or return value declared so) wraps to 32 bits as ActionScript's ToInt32 and ToUint32 do; `int(x)`, `uint(x)` and `Number(x)` convert. So `var c:uint = 0xFF000000` is 4278190080 and `var i:int = 0xFF000000` is -16777216; `&`, `|`, `^`, `<<` and `>>` give int results and `>>>` a uint one; `7 / 2` is 3.5 unless stored in an int. Typed variables start as their type does: 0, `NaN`, `false` or `null`.

### Timers
`setTimeout` and `setInterval` keep their timers on the VM; the host runs what is due with `vm.runTimers(milliseconds)`, the time since the VM was made (or by the clock it sets with `vm.setClock`). `getTimer` gives that time. The host may define its own `flash.utils.setTimeout` and the rest instead.

### Metadata and conditional compilation
A class's metadata is `cls->metadata`, a member's `cls->findTrait(name)->metadata` (or `findStatic` for a static one): each tag's name and arguments as written, for the host to act on (`[Embed(source="a.png")]`).

```cpp
as3::Parser parser(source, "Main.as");
parser.defineConstant("CONFIG::debug", "true");   // CONFIG::debug { ... } is kept
```

## Compiling to ABC

For movies that run in a player rather than in this VM, `as3::abc::compile` makes ABC (AVM2 bytecode, the abcFile a SWF's DoABC tag carries) from the same sources: classes in packages, and the frame scripts of timelines.

```cpp
#include "abc/abc.h"

std::vector<as3::abc::Source> sources = {{readFile("com/example/Ball.as"), "Ball.as"}};

as3::abc::Timeline main;                       // the main timeline's scripts
main.className = as3::abc::timelineClassName("game");   // "game_fla.MainTimeline"
main.instances = {{"ball", "com.example.Ball"}, {"score", "flash.text.TextField"}};
main.frames = {{0, "import com.example.Ball;\nball.speed = 2;\nstop();"}};

as3::abc::Output out = as3::abc::compile(sources, {main});
if (!out.ok) {
    std::cerr << out.errorText();              // "Ball.as:12: error: ..."
} else {
    // out.abc: the DoABC tag's ABC (after its flags, 1 for lazy, and its name).
    // out.classes: "com.example.Ball", ..., "game_fla.MainTimeline", for SymbolClass.
}
```

- Each source file is a script: its classes (with their superclasses pushed as scopes as they are made, as compilers do), package functions and variables, and code outside the package. A script runs when one of its definitions is first needed, so files may be given in any order; the timelines' scripts come last.
- A `Timeline` becomes a dynamic subclass of its `baseClass` (`flash.display.MovieClip` when not given) whose constructor calls `addFrameScript(0, frame1, ...)`. The variables and functions of its frames' scripts are its public members, each frame's code is a method (`frame1` for frame 0), its stage instances are public variables of their types, and the `flash.*` packages are imported without import statements, as the authoring tool does. A library symbol exported for ActionScript with no class file is a `Timeline` without frames.
- Classes the player provides need no definitions: `import flash.display.Sprite` names `flash.display::Sprite`, as `import fl.controls.Button` names that class for a movie that has the components. A type named through a wildcard import is found in a table of the player's classes (`abc_player.cpp`); one not found there is taken as `*`, with a warning.
- `Options`: `debugInfo` (debugfile and debugline, for a debugger player), conditional compilation `constants`, an `includeHandler`, and `verify` (on by default: the output is checked with the verifier below, and a problem is an error).
- Not compiled: namespaces beyond the four access ones (the parser keeps no `ns::name` or `use namespace`: such members are internal), E4X namespaces, and AVM1. A local variable inside `with` is found before the `with` object's property of the same name.

The rest of the format is a library too, for reading ABC from published movies as well as for checking what is made:

```cpp
#include "abc/abc_file.h"
#include "abc/abc_opcodes.h"
#include "abc/abc_verifier.h"

as3::abc::AbcFile file;                        // pools, methods, metadata, instances, classes, scripts, bodies
std::string error;
if (!as3::abc::read(bytes, file, error)) { /* not an abcFile: error says why */ }
std::vector<uint8_t> again = as3::abc::write(file);           // the same bytes

std::vector<as3::abc::Instruction> code;       // a body's instructions, operands decoded
as3::abc::decodeAll(file.bodies[0].code, code, error);
as3::abc::StackEffect effect = as3::abc::stackEffect(file, code[0]);

std::vector<std::string> problems = as3::abc::verify(file);   // empty when it verifies
std::string text = as3::abc::disassemble(file);
```

`read` takes every abcFile (46.16) a compiler makes: all multiname kinds, metadata, optional parameters, every opcode including the debug ones and `lookupswitch`; it reads, and writes back byte for byte, the 249 KB ABC of a movie Flash CS5 published with the fl components in it. `verify` checks what a player's verifier checks of the structure (not the types): pool indexes, one body for each method that runs and none for an interface's, instructions that decode, branches onto instructions (a backward one onto a `label`), and along every path an operand stack never below empty or above `max_stack` and a scope stack within `max_scope_depth - init_scope_depth`, the same wherever paths meet, with exception handlers starting from one value and no scopes.

## Running ABC

`as3::abc::Domain` runs ABC (a published movie's DoABC tags) on the VM, so that a movie's compiled classes work as classes compiled from source do:

```cpp
#include "abc/abc_domain.h"

as3::VM vm;                                    // the host's classes defined on it as usual
as3::abc::Domain domain(vm);                   // lives as long as code of it may run
std::string error;
if (!domain.load(abcBytes, error)) { /* not an abcFile */ }
as3::Val main = vm.findDefinition("game_fla.MainTimeline");   // its script runs now
as3::Val clip = vm.construct(main);
```

- A class defined in ABC is a `ClassInfo` like any other: the VM finds it by qualified name (`findDefinition`, `findClass`, `getDefinitionByName`, and the names scripts compiled from source use) before asking the class resolver, it extends native classes (its instance initializer runs as a native constructor does, and `super()` runs the native one), classes from source extend it, and methods are called and overridden either way: native code calling `vm.callMethod(self, "draw")` reaches an ABC subclass's `draw`.
- A script's initializer runs the first time one of its definitions is needed (a DoABC tag's lazy flag); `runEntry()` runs the last script of the last file, for code outside classes.
- Its methods are interpreted as they are: instructions decoded once per body, branches resolved to instructions, the operand stack, registers and scope stack of each call in a pool of values. Each method is a native function of the VM (so the VM's calls, bound methods, `call` and `apply` reach it), and names are found as AVM2 finds them: on the scope stack (with `with` objects), the scopes the method was made in, then the domain's scripts and the VM's definitions. Private members are kept by their class (`pkg.Class::name`), so a subclass's private member does not hide its superclass's; members in other namespaces by their names. `trace(domain.takeErrorStack())` gives the methods an error came out of, with the instruction's offset (or line, with debug info).
- Not run: the domain memory instructions (`li8`, `si32`...), `callmethod`, and XML namespaces; argument counts are not checked (a missing argument is `undefined`).

## Translating to JavaScript

For movies published to HTML5, `as3::js::translate` makes readable JavaScript (ES2020 classes, no transpiler needed) from the same sources and timelines as the ABC compiler, which it shares its front end with (`abc_model.h`: the files parsed, their packages' definitions, the classes with their members, superclasses and interfaces resolved, the timelines' classes made from their frame scripts). The JavaScript runs on `as3.js` (`as3::js::runtime()`, the source in `src/js/as3.js`) and a JavaScript implementation of the flash.* classes, the runtime below.

```cpp
#include "js/js.h"

as3::js::Timeline main;                        // as for ABC: a frame script, the stage's instances
main.className = as3::js::timelineClassName("game");
main.instances = {{"ball", "com.example.Ball"}};
main.frames = {{0, "ball.speed = 2;\nstop();"}};

as3::js::Output out = as3::js::translate({{readFile("com/example/Ball.as"), "Ball.as"}}, {main});
if (!out.ok) std::cerr << out.errorText();    // "Ball.as:12: error: cannot translate ... to JavaScript"
// out.files: "com/example/Ball.js", "game_fla/MainTimeline.js", each with a sourceMappingURL
// out.bundle: every file in order, then as3.runScripts(); out.sourceMaps: each file's and "bundle.js.map"
// out.classes: "com.example.Ball", ..., "game_fla.MainTimeline"
```

A page loads `as3.js`, the flash.* runtime, then the bundle (or the files), and makes the document's class: `new (as3.getDefinitionByName("game_fla.MainTimeline"))()`.

`Options`: conditional compilation `constants`, an `includeHandler`, `sourceMaps` (on), `comments` (on: the sources' comments on lines of their own are kept with the code they are by), and `coerceAllParameters` (typed `int` and `uint` parameters are converted on entry always, `x |= 0`; with this, `Number`, `String` and `Boolean` ones too).

### What is made

A class is a definition, made when it is first used:

```javascript
// Ball.as, translated to JavaScript by libas3.
"use strict";
as3.define("com.example.Ball", () => {
    const Sprite = as3.use("flash.display.Sprite");
    let Event;

    class Ball extends Sprite {
        constructor(...args) {
            super();
            this.speed = NaN;
            this.hits = 0;
            if (new.target === Ball) this.$construct(...args);
        }

        $construct(speed = 2) {
            super.$construct();
            this.speed = speed;
            this.addEventListener(Event.ENTER_FRAME, as3.bind(this, "move$Ball"));
        }

        // Each frame, a step to the right.
        move$Ball(e) {
            this.x += this.speed;
            if (this.x > 400) {
                this.hits = (this.hits + 1) | 0;
            }
        }

        get bounces() {
            return this.hits;
        }
    }
    Ball.RADIUS = 10;
    return {
        class: Ball,
        fields: { speed: "Number", hits: "private int" },
        init() {
            Event = as3.use("flash.events.Event");
        },
    };
});
```

- Packages are nested objects (`com.example.Ball`, `flash.display.MovieClip`). `as3.define(name, factory)` puts a getter there; the first use (`as3.use`, the getter, `getDefinitionByName`) runs the factory, which makes the class with its superclass (`const`, needed now) and gives the descriptor; the class is then registered, so that classes may use each other in any order, and `init` binds the rest of what the class uses (the `let` names: imports, its package's classes, the player's, each defined then if it is not yet) and runs its static initializers and code in the order written: a class's statics are ready before it is first used (and are made when a class using it is first used). A class's constants of literal values are set when it is made.
- Fields are the object's own properties. Each takes its constant value (or its type's default: `int` and `uint` 0, `Number` NaN, `Boolean` false, others null, untyped undefined) in the JavaScript constructor, before any ActionScript constructor runs, as a player has them when the object is made; one with a value to compute takes it in `$construct`, the ActionScript constructor, before the constructor's code, so before `super()` as ActionScript has it. `super(...)` is `super.$construct(...)`, put first when the constructor does not call it.
- Methods, getters, setters and statics are the class's; an accessor overriding only its getter (or setter) gets the other half from its superclass. A private member whose name a superclass or subclass has (or the player's class it extends, as a `MovieClip` subclass's private `name`) takes its class's name (`move$Ball`), so that two classes' privates do not clash; others keep their names. A method read as a value is bound to its object, the same function each time (`as3.bind`), so `removeEventListener` finds what `addEventListener` was given; so is an untyped object's member passed as an argument when some class has a method of that name.
- Interfaces are values (`as3.define("a.IShape", () => ({ interface: true, extends: [...] }))`) that `is`, `as` and `instanceof` check a class's `interfaces` against.
- Local variables are `let` (or `const`) where the function's scoping and JavaScript's agree (declared once, not used before it or by nested functions, at the function's top, or a loop's variable used only in the loop, or a block's, with a value, used only after it in the block), `var` otherwise, and a typed variable declared without a value in a block is declared at the function's start with its default, as ActionScript has function-scoped variables. Nested functions are arrow functions (so `this` and the class's members are the method's), or plain functions when they use `this` or `arguments` (the method's `this` is then `$this`).
- A file's code outside its packages runs with `as3.runScripts()`; its classes and functions outside the packages are the file's own (`as3.file(name)`), its variables those of the file's scope.
- A timeline is a class extending its base (`flash.display.MovieClip`) whose `$construct` calls `this.addFrameScript(0, as3.bind(this, "frame1"), ...)`; its frames' variables and stage instances are fields, its frames' functions methods, `frame1` and the rest its frames' code; names not otherwise known are the clip's (`stop()` is `this.stop()`), and the flash.* packages are imported, as for ABC.
- Names are resolved as the ABC compiler resolves them: locals, the class's members (`this.x`), statics (`Ball.RADIUS`), imported and same-package definitions, the top level; in a class extending one of the player's, a name not known is the object's (`this.stage`). Each output line maps back to its .as line in the source maps (a timeline's, to its frame's script: each frame is a source).

The language's semantics JavaScript lacks:

- `int` and `uint`: a value stored in an `int` or `uint` (variable, field, parameter, return value, element of a typed Vector) is converted, `(a / b) | 0` and `x >>> 0`, so division, overflow and `0xFF000000` are as ActionScript has them; `int(x)` is `x | 0`. Stored to `Number`, `String` or `Boolean` a value of another type is converted too (`+x`, `as3.str(x)`, which keeps null, and `!!x`). Literals are written as in the source (`0xFF336699` stays hexadecimal).
- `is`, `as`, `typeof` (`"xml"`, and `"object"` for a class), a class called as a cast (`Sprite(x)` is `as3.cast(x, Sprite)`, a TypeError when it is not one), `trace` (`as3.trace`, writing `[class Ball]`, `null` and the rest as a player does, to `as3.output`), `String(x)`, `parseInt` (a leading 0 is octal), `getQualifiedClassName` (`"com.example::Ball"`), `getQualifiedSuperclassName`, `getDefinitionByName`, `describeType` (a minimal one: the class, its bases, interfaces, public variables, constants, accessors and methods).
- `for (k in o)` and `for each (v in o)` are `for (... of as3.keys(o))` and `as3.values(o)`: over objects, arrays, a Dictionary's keys (objects too), XMLList's items, a dynamic class's own properties but not a class's fields; null gives none.
- `Vector.<T>` (`as3.Vector(as3.int)`, `new Vector.<int>(n, fixed)` is `as3.vector(as3.int, n, fixed)`): an Array of its elements' type, filled with its defaults, converting what `push`, `unshift` and `splice` add, `fixed`, its methods giving vectors.
- `Array`'s `sort` and `sortOn` with their options (`Array.NUMERIC`, `CASEINSENSITIVE`, `DESCENDING`, `UNIQUESORT`, `RETURNINDEXEDARRAY`), `insertAt`, `removeAt`: `a.sort(...)` is `as3.sort(a, ...)` where `a` may be an array.
- `flash.utils.Dictionary`: objects as keys; `d[k]` of a typed Dictionary is `as3.getItem(d, k)` (and `setItem`, `hasItem`, `deleteItem`), and of a string key on any object `d[k]`, which a Dictionary also answers.
- `arguments`, `...rest`, default values, `with` (each name looked for on the object first, as the VM has it: `as3.has(o, "x") ? o.x : x`), labels, `switch`, `try` with `catch` clauses by type: a JavaScript error caught becomes the player's (`TypeError: Error #1009: Cannot access a property or method of a null object reference.`, `#1006`, `#1065`, `#1023`...), and `Error`, its subclasses and `flash.errors.*` are as3's (`errorID`, `getStackTrace()`), to extend and throw.
- E4X: XML literals (`<item id={n}>{text}</item>`) are `as3.toXML(...)`; `x.child`, `x["@id"]`, `x.children()`, `x.attributes()`, `x.descendants("name")`, filters (`as3.xmlFilter(list, ($item) => ...)`), assignment and `delete` work on XML and XMLList objects (proxies over a tree), with their methods and `toXMLString()` as the VM writes it. A child named as a method (`name`, `length`, `text`...) of an object typed `XML` or `XMLList` is read with `child("name")`.
- Date's properties (`fullYear`, `month`, `time`...), and its strings as a player writes them where the type is known (`as3.dateText(d, "toUTCString")`); regular expressions with named groups (`(?P<word>...)`) give the groups on the match.
- `setTimeout`, `setInterval`, `clearTimeout`, `clearInterval` and `getTimer` (from `as3.clock`); with `as3.manualTimers` the host runs what is due with `as3.runTimers(now)`, as the VM's `runTimers`.

What cannot be translated is an error naming it (`cannot translate arguments.callee to JavaScript`): `arguments.callee`, `super` in a nested function that has its own `this`, a type applied to anything but `Vector`. What the parser does not keep (namespaces beyond the access ones, `ns::name`, E4X namespaces) is not translated either, nor AVM1. Known differences from the VM: a function's `length` counts the parameters before the first with a default; writing past a Vector's end extends it rather than throwing RangeError #1125; `JSON.stringify` of a class instance gives its fields in their order; `describeType` gives no metadata; `++` and `--` on an `int` inside an expression or a `for` loop's update do not wrap at 2147483647; a value of unknown type stored in an object-typed variable stays undefined (rather than null) when it is undefined.

### The runtime it targets

The flash.* runtime (Phoenix's, in its `resources/html5`) and translated code meet in a few places, all in `as3.js` and easily adjusted there:

- Definitions: `as3.define(name, factory)` (translated code), `as3.register(name, value)` (the runtime's classes and functions, defined at once; classes get their metadata), `as3.use(name)`, `as3.isDefined(name)`, `as3.getDefinitionByName(name)` (`"a.b::C"` or `"a.b.C"`). A name not defined through as3 is looked for in the package tree (a runtime that puts `flash.display.Sprite` on `globalThis.flash` itself needs no registering) and then by each of `as3.resolvers`, functions `(name) => value` the runtime adds; one not found at all, or whose definition failed (its superclass missing), is a stand-in that throws that error (ReferenceError #1065) when it is used, as a player throws, so that a class may use what a movie does not have (`mx.*`, `fl.controls.*`) and still load.
- Classes: a class the translated code extends follows its construction protocol. The JavaScript constructor only makes the object (its own state at defaults) and, for the class actually made, runs the ActionScript constructor, `$construct(...args)`; a subclass's `$construct` calls `super.$construct(...)` with the arguments ActionScript's `super(...)` gives:
  ```javascript
  class Sprite extends DisplayObjectContainer {
      constructor(...args) { super(); if (new.target === Sprite) this.$construct(...args); }
      $construct() { super.$construct(); /* what new Sprite() does */ }
  }
  as3.register("flash.display.Sprite", Sprite);
  ```
  The root class (`EventDispatcher`'s base, or `EventDispatcher` itself) has a `$construct` that does not call a super one. Translated classes extending `Object` have neither `super()` nor `super.$construct()`. A runtime class without `$construct` (a constructor that does all) gets one when as3 finds it (`as3.adopt`): with no arguments it does nothing, the object being made already; with arguments (`super(type, true)` in an `Event` subclass) it makes one with them and takes its own properties, which serves classes whose state is their properties.
- Library symbols and stage instances: the runtime makes a symbol's instance with `new Cls()` (its class from `as3.getDefinitionByName(linkageName)`, a translated class, or a timeline's); the state that ties the object to its symbol is set in the runtime's JavaScript constructor (from a symbol the runtime notes before `new`), and stage instances are set as `this[instanceName]` in the runtime's JavaScript constructor or in `MovieClip.prototype.$construct` (the subclass's `super()`): a translated class keeps what is there for its public fields of display types and a timeline's stage instances (`this.ball = this.ball ?? null`). Frame scripts come by `addFrameScript(frame, method, ...)`, the methods bound.
- `trace` writes to `as3.output(text)` (console.log until the runtime sets its own); `as3.clock()` gives the milliseconds since the movie started, which `getTimer` and the timers count; `as3.manualTimers` and `as3.runTimers(now)` let the runtime drive the timers from its frames.
- as3.js defines `flash.utils`' functions, `Dictionary`, `ByteArray` (both byte orders, `b[i]`, the read and write methods but AMF's and compression) and `Endian`, and `flash.errors`' classes; the runtime may replace any of them with `as3.register`.

## Running Tests

```bash
./build/Release/as3_tests.exe                # every suite
./build/Release/as3_tests.exe nativeclasses  # one
```

Suites: Parser, Bytecode, VM, AS2 (ActionScript 1 and 2), Language (classes, packages, statements, types), Library (the built-in classes), XML (E4X, and as3corelib's JSON encoder, which uses it), NativeClasses (the host's classes, a JPEG encoder run on a native BitmapData), BunnyPaint (a 6,000-line paint program and as3corelib's JPEG encoder compiled and loaded through the class resolver, skipped where its sources are not), Errors, Abc (reading, writing and verifying ABC, Flash CS5's own among it), AbcCompiler (ActionScript compiled to ABC and verified, bunnyPaint and its main timeline among it), AVM1 (the SWF actions compiled, by their listing and by what a small AVM1 machine in the tests makes of them), AbcDomain (programs compiled to ABC and run by a Domain, against the same source run directly; ABC classes over native ones and under source ones; a published movie's ABC making its classes), JavaScript (ActionScript translated to JavaScript; where Node is installed, the programs of the Language, Library, XML and VM suites and of its own run both in the VM and translated in Node, tracing the same, as3corelib's JPEG encoder makes the same bytes, as3corelib translates and its hashes, JSON and utilities give the VM's results, and bunnyPaint with its main timeline translates, loads and runs its classes that need no display against stand-ins for the flash.* classes), and Benchmark.

## Performance

A stack-based interpreter with values that hold numbers in place, inline caches on property reads, writes and method calls (a class's field is a slot, found without a lookup by name once seen), fused instructions for `this.x` and `local.x`, call frames used again rather than made, and loops tested at the bottom. On a recent desktop (Release build):

- 1,000,000 iterations of arithmetic, a method call and a property read and write: about 100 ms
- 320 by 320 pixels with ten native calls each (1,000,000 calls to a host's `getPixel32` and `setPixel32`): about 65 ms
- Making a VM with its built-in library: about 0.25 ms

## Bytecode Design

See [BYTECODE_DESIGN.md](src/bytecode/BYTECODE_DESIGN.md).

## Acknowledgments

Inspired by:
- ActionScript Language Specification
- [Crafting Interpreters](https://craftinginterpreters.com) by Robert Nystrom
- Lua VM design
- V8 JavaScript engine
