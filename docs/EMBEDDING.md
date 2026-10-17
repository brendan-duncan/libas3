# Embedding libas3

This page is for a C++ host that runs ActionScript: a player, an authoring tool's test movie, a game. The host makes an `as3::VM`, gives it the classes and functions its scripts need (the display list, events: anything beyond the [built-in library](LIBRARY.md)), loads scripts, and calls into them when events come.

Everything here is in `bytecode/vm.h` and `runtime/value.h`, namespace `as3`. `test/native_class_tests.cpp`, `test/language_tests.cpp` and `test/error_tests.cpp` use all of it.

## Running a script

```cpp
#include "bytecode/vm.h"

as3::VM vm;
// Parses, compiles and runs. False (with the error kept) when it fails.
if (!vm.load("var x = 10; trace('Sum:', x + 20);", "frame1.as")) {
    std::cerr << vm.getError() << " at line " << vm.getErrorLine() << "\n" << vm.getErrorStack();
}
```

`load` and `execute` never throw: a script's error stops it and is kept on the VM (see [Errors](#errors)), and the VM can run more afterwards. A VM is not thread-safe; use one from one thread at a time.

Parsing and compiling can be done separately, to set parser options (conditional compilation constants, an include handler, strict mode) or to compile once and run many times:

```cpp
#include "parser/parser.h"
#include "bytecode/compiler.h"

as3::Parser parser(source, "Main.as");
parser.defineConstant("CONFIG::debug", "true");
std::unique_ptr<as3::Program> program = parser.parse();
if (parser.hasErrors()) { std::cerr << parser.getErrors(); return; }

as3::Compiler compiler;
std::shared_ptr<as3::BytecodeFunction> code;
try {
    code = compiler.compile(program.get());
} catch (const as3::CompileError& error) {   // a std::runtime_error, every error a line:
    // "Compilation failed: Main.as:2: break outside a loop"
    std::cerr << error.what();
    return;
}
vm.execute(code);
```

### Frame scripts on a timeline

Given a scope, a script runs on it as a frame script runs on its timeline: the scope is `this`, names that are not locals are looked for on it before the globals, and assigning a name that is not a global (or a top-level `var`) keeps it on the scope. Another script run on the same scope sees those names.

```cpp
as3::ValuePtr root = as3::Value::makeHostObject(rootClip);   // the host's movie clip
vm.load("count = 2;\nhat._x += 20;", "frame1.as", root);
vm.load("trace(count + 1);", "frame2.as", root);                // 3
```

`execute(code, scope)` and `call(function, args, this, scope)` take a scope too.

## Values

The VM works with `as3::Val`: a small value that holds a number or a boolean in place and a string or an object by reference, cheap to copy. The older host API, `as3::ValuePtr` (`std::shared_ptr<as3::Value>`), still works, and converts to and from `Val` implicitly.

```cpp
as3::Val n = as3::Val::number(3.5);
as3::Val s = as3::Val::string("text");
as3::Val b = as3::Val::boolean(true);
as3::Val nothing = as3::Val::null();          // Val() is undefined
as3::Val o = vm.newObject();
o->properties["x"] = n;                        // a dynamic property
as3::Val list = vm.newArray({n, s});
as3::ValuePtr boxed = n;                       // for older host code
double d = vm.getProperty(o, "x").toNumber();
```

- `isUndefined`, `isNull`, `isNullish`, `isBoolean`, `isNumber`, `isString`, `isObject`, `isFunction`, `isArray`, `isClass`.
- `toNumber`, `toBoolean` and `toString` convert without running script code; `vm.toString(v)` and `vm.toNumber(v)` convert as `String(v)` and `Number(v)` do, running an object's `toString` or `valueOf`.
- `v.host<T>()` gives the `HostObject` attached to an object, as a `T`, or nullptr (see below).
- `VM::looselyEqual(a, b)` is ActionScript's `==`; `vm.isType(v, cls)` is `v is cls`; `vm.classOf(v)` is a value's class.

## Native classes

The host implements classes such as `flash.display.Sprite` or `flash.geom.Rectangle` in C++: a constructor that attaches the host's state, methods, getters and setters, static members and a superclass. Scripts use them as ActionScript classes, and extend them.

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

(`runtime/builtins.h` has the `rect` helper above as a template: `as3::hostOf<RectState>(vm, self, "Rectangle")`.)

A script class extends it as any class. Its instances keep the native state, the native methods and accessors are found unqualified, and overriding works both ways:

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
- `constructor(fn)`: runs for `new` and for a subclass's `super(...)`. It returns `Val()` to keep `self` (it may return another object to make instead, rarely wanted).
- `method`, `getter`, `setter`, `property` (a getter and a setter): instance members. `method` and `staticMethod` take an optional `length` (what `f.length` gives). `field(name, initial, coerce)`: a field each instance has, as a script `var`; `coerce` (`Coerce::Int`, `Uint`, `Number`, `String`, `Boolean`, `Nullable`) converts what is stored.
- `staticMethod`, `staticGetter`, `staticSetter`, `constant`, `staticVar`: members of the class.
- `prototypeMethod`: a method found after an instance's dynamic properties (as `Object.prototype.toString` is), so an object may have its own of that name.
- `callHandler`: what `ClassName(x)` does; a cast when not given.
- `define()` registers it under its qualified name and, unless taken, its short name (frame scripts and older host code find classes so). It returns the class value. `info()` gives the `ClassInfo` being built.

### Native functions

Native functions are `NativeMethod`s: `Val (VM& vm, const Val& self, Args args)`. `self` is `this` (the instance, or the class for a static). `Args` reads missing arguments as undefined, and has `size()`, `args[i]`, and `number(i)`, `integer(i)`, `string(i)`, `boolean(i)` (each with an optional default for a missing one).

A native raises an ActionScript error with `vm.throwError("TypeError", "Error #1034: ...", 1034)` (any error class by name, `flash.errors.IOError` too); scripts catch it with `try`/`catch`. A C++ exception thrown from a native (a `std::exception`) becomes an `Error` with its `what()` as the message.

### Host state: HostObject

The host's state goes on `self->host`, a `std::shared_ptr<HostObject>`; `self.host<T>()` finds it again (nullptr when the object has none or one of another type). A `HostObject` may also answer for properties itself, as a movie clip answers for `_x` or `gotoAndStop` (what it does not answer for is kept as an ordinary property):

```cpp
class Clip : public as3::HostObject {
public:
    double x = 0;
    as3::ValuePtr get(const std::string& name) override {
        if (name == "_x") return as3::Value::makeNumber(x);
        return nullptr;                       // not the host's: an ordinary property
    }
    bool set(const std::string& name, as3::ValuePtr value) override {
        if (name == "_x") { x = value->toNumber(); return true; }
        return false;
    }
};
as3::ValuePtr clip = as3::Value::makeHostObject(std::make_shared<Clip>());
```

`getIndex`, `setIndex`, `deleteIndex` and `hasIndex` answer for `obj[key]` with keys that are not names (a Dictionary's object keys, a ByteArray's bytes), and `enumerate` gives the keys `for-in` visits.

### Instances the host makes

When the host makes an instance itself (a movie clip of a symbol whose class is a script class extending `MovieClip`), it attaches its state before the constructors run, and the native constructor keeps it:

```cpp
auto cls = vm.findClass("game.Star");          // the class resolver may load it
as3::Val star = vm.newInstance(cls);           // fields at their defaults
star->host = myClipState;                      // the host's state first
vm.runConstructor(star, as3::Args());          // then Star(), super() and so on
```

## Calling scripts from the host

```cpp
Val result = vm.callFunction(listener, thisValue, {event});    // a function, as from a dispatch
Val text = vm.callMethod(object, "toString");                  // the subclass's override, if any
Val made = vm.construct(vm.findDefinition("geo.Box"), {Val::number(3), Val::number(4)});
Val w = vm.getProperty(made, "width");                         // getters run
vm.setProperty(made, "width", Val::number(5));                 // setters run
bool has = vm.hasProperty(made, "height");
Val first = vm.getIndex(list, Val::number(0));
```

These throw `as3::ScriptException` for an ActionScript error. It is not a `std::exception`, so that from inside a native function it passes through host code catching those and goes on to the script that called the native. Called from outside any script, the host catches it:

```cpp
try {
    vm.callMethod(clip, "onEnterFrame");
} catch (as3::ScriptException& e) {
    std::cerr << vm.toString(e.value) << "\n";   // "TypeError: Error #1009: ..."
}
```

The older `vm.call(function, args, this, scope)` catches it instead and keeps it as the VM's error, as `execute` and `load` do. It can be called while a script runs (from a native) as well as between scripts.

## Functions and values by name

```cpp
vm.defineFunction("flash.utils.getTimer", [](VM& vm, const Val&, Args) { return Val::number(now()); });
vm.defineFunction("trace", myTrace);                 // a top-level one replaces the built-in
vm.defineValue("com.example.VERSION", Val::string("1.0"));
Val f = vm.makeFunction("helper", helperMethod);     // a native function as a value
```

A definition is found by scripts through its package (`import flash.utils.getTimer`, or `flash.utils.getTimer()`), and by the host with `vm.findDefinition("a.b.C")` (or `"a.b::C"`), which asks the class resolver when it must. `vm.findClass(name)` gives a class's `ClassInfo`.

## The class resolver

When a script needs a class (or function) the VM does not have, as a superclass, an imported name or a name of its package, the VM asks the host for its source, compiles and runs it, and goes on. The host answers `std::nullopt` for names it does not have:

```cpp
vm.setClassResolver([](const std::string& qualifiedName) -> std::optional<as3::ClassSource> {
    // "com.example.Shape", or "Shape" for a class without a package.
    std::filesystem::path file = root / (dotsToSlashes(qualifiedName) + ".as");
    if (!std::filesystem::exists(file)) return std::nullopt;
    return as3::ClassSource{readFile(file), file.filename().string()};
});
as3::Val cls = vm.findDefinition("com.example.Main");   // loads Main, and what it extends
```

- Classes are kept by their qualified names (`getDefinitionByName("com.example.Main")`, or `"com.example::Main"`) and, unless the name is taken, by their short names.
- A class's static initializer runs when it is defined.
- A source that does not compile is a `SyntaxError` naming its file and line; its warnings are in `vm.getWarnings()`.
- `addDefinitionProvider` adds a source of definitions that are already values rather than source; `abc::Domain` uses it to give the VM the classes of ABC it has loaded. Providers are asked before the class resolver.

## Includes

`include "file.as"` in what `load` and the class resolver compile is read through the include resolver, which is given the path and the file that includes it:

```cpp
vm.setIncludeResolver([](const std::string& path, const std::string& fromFile) -> std::optional<std::string> {
    return readFileRelativeTo(fromFile, path);   // std::nullopt: an error naming the file
});
```

With a `Parser` of your own, `parser.setIncludeHandler([](const std::string& path) -> std::optional<std::string> { ... })`.

## Metadata

A class's metadata is `cls->metadata`, a member's `cls->findTrait(name)->metadata` (or `findStatic` for a static one): each tag's name and arguments as written, for the host to act on.

```cpp
auto cls = vm.findClass("art.Assets");
const as3::Trait* brush = cls->findStatic("Brush");
// brush->metadata->at(0).name == "Embed"
// brush->metadata->at(0).arguments.at(0) == {"source", "brush.png"}
```

## Timers

`setTimeout` and `setInterval` keep their timers on the VM; the host runs what is due with `vm.runTimers(milliseconds)`, the time since the VM was made (or by the clock it sets with `vm.setClock`). `getTimer` gives that time. The host may define its own `flash.utils.setTimeout` and the rest instead. See [LIBRARY.md](LIBRARY.md#timers).

## Errors

Runtime errors are ActionScript's: `TypeError: Error #1009: Cannot access a property or method of a null object reference.`, `ReferenceError: Error #1065: Variable x is not defined.`, `TypeError: Error #1006: f is not a function.`, `RangeError`, `ArgumentError`. Scripts catch them by type. One not caught stops the script and is kept on the VM:

```cpp
vm.hasError();        // the last run failed
vm.getError();        // "TypeError: Error #1009: ..."
vm.getErrorLine();    // where it was thrown (-1 when not known)
vm.getErrorStack();   // "\tat app.Deep/inner()[Deep.as:8]\n\tat app.Deep/outer()[Deep.as:4]\n..."
vm.getErrorValue();   // what was thrown
vm.clearError();
```

Errors are not printed; the host shows them as it wants.

- Compile errors from `load` are kept the same way, with the file and line: `Parse error in Bad.as at line 1, column 9: ...`, `Frame.as:2: break outside a loop`.
- Runaway recursion is `Error #1023` (stack overflow), through native calls and getters too, and the VM goes on. Scripts calling natives calling scripts may use 512 KB of the C++ stack from where the host called in; `vm.setNativeStackLimit(bytes)` changes that.
- `vm.stackTrace()` gives the functions running now, as `getStackTrace` does.
- `vm.makeError("RangeError", "message", id)` makes an error object without throwing it.

## Memory

Values are reference counted. Objects that refer to each other in a cycle (a closure stored on the object it closes over, a parent and child) are not freed by counting alone; `vm.releaseObjects()` empties every object the VM made, breaking the cycles. The VM's destructor does it, so values the host keeps after the VM is gone are empty.

## Child VMs

A VM made with a parent shares the parent's built-in classes and class resolver, and finds the parent's globals and definitions when it has none of its own of that name. What the child defines is its own: the parent does not see it.

```cpp
as3::VM parent;
parent.defineFunction("shared", sharedFunction);
as3::VM child(&parent);
child.defineFunction("local", localFunction);   // only the child's scripts see this
child.load("shared(); local();", "child.as");
```

The parent must outlive its children.

## The older API

Earlier host code used boxed values and functions without the VM or `this`. It still works:

```cpp
vm.registerBuiltin("double", [](const std::vector<as3::ValuePtr>& args) {
    return as3::Value::makeNumber(args.empty() ? 0 : args[0]->toNumber() * 2);
});
vm.defineExternClass("Shape", {{"getType", [](const std::vector<as3::ValuePtr>&) {
    return as3::Value::makeString("shape");
}}});
vm.registerObject("hair01", "Shape");          // a global instance of that class
vm.setGlobal("score", as3::Value::makeNumber(0));
as3::ValuePtr score = vm.getGlobal("score");
```

New code should prefer `defineFunction`, `defineNativeClass` and `defineValue`, which give natives the VM and `this` and make classes scripts can extend.

## ActionScript 1 and 2 hosts

The VM runs ActionScript 1 and 2 frame scripts and handlers with ActionScript 3's semantics (see [LANGUAGE.md](LANGUAGE.md#actionscript-1-and-2)). A host running them defines what the older forms call:

```cpp
// on (release) { ... } and onClipEvent (enterFrame) { ... }: the events as
// written, and the block as a function to call when one comes.
vm.registerBuiltin("__on", [&](const std::vector<as3::ValuePtr>& args) {
    for (const as3::ValuePtr& event : args[0]->elements) listen(event->toString(), args[1]);
    return as3::Value::makeUndefined();
});
// __onClipEvent(events, function), __tellTarget(target, function) and
// ifFrameLoaded(frame) likewise; the timeline functions (gotoAndPlay, stop,
// getProperty, setProperty...) as globals or as the clips' own methods.
```

`getProperty("hat", _x)` and `setProperty("hat", _x, v)` are given the property's name as a string (`"_x"`).

For movies compiled to SWF actions, run them with `avm1::Interpreter` instead ([AVM1.md](AVM1.md)): it has ActionScript 1 and 2's own semantics and asks the host only for what its clips do.
