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
`Object`, `Array` (with `sort`'s options and `sortOn`), `Vector.<T>`, `String` (counting in UTF-16 units, with regular expressions in `split`, `replace`, `match` and `search`), `Number` (`toFixed`, `toPrecision`, `toString(radix)`), `int`, `uint`, `Boolean`, `Math`, `Date`, `RegExp`, `JSON`, `Error` and its subclasses (`getStackTrace`), `Function`, `flash.utils.Dictionary`, `getDefinitionByName`, `getQualifiedClassName`, `getQualifiedSuperclassName`, `getTimer`, `setTimeout`, `setInterval`, `clearTimeout`, `clearInterval`, and the top-level functions (`trace`, `isNaN`, `isFinite`, `parseInt`, `parseFloat`, `escape`, `unescape`, `encodeURI(Component)`, `decodeURI(Component)`).

Also `with` (names in its body found on its object first) and `include "file.as"` (read through the host, `VM::setIncludeResolver` or `Parser::setIncludeHandler`).

Not supported: E4X (XML literals and expressions), and namespaces beyond accepting their syntax (`mx_internal function f()` is a plain method).

## Project Structure

```
src/
├── ast/              # AST node definitions
├── lexer/            # Tokenization
├── parser/           # AS3 parser (AST generation)
├── runtime/          # Values, classes, the built-in library
│   ├── value.h/cpp         # Val, Value, ClassInfo, HostObject
│   └── builtins_*.cpp      # Object, Array, String, Math, Date, RegExp, JSON...
└── bytecode/         # Compiler and VM
    ├── opcode.h/cpp        # Instruction set
    ├── chunk.h/cpp         # Bytecode, functions and classes as compiled
    ├── compiler.h/cpp      # AST → Bytecode
    ├── vm.h/cpp            # The interpreter loop, calls, exceptions
    └── vm_runtime.cpp      # Properties, names, classes, native classes

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

## Running Tests

```bash
./build/Release/as3_tests.exe                # every suite
./build/Release/as3_tests.exe nativeclasses  # one
```

Suites: Parser, Bytecode, VM, AS2 (ActionScript 1 and 2), Language (classes, packages, statements, types), Library (the built-in classes), NativeClasses (the host's classes, a JPEG encoder run on a native BitmapData), BunnyPaint (a 6,000-line paint program and as3corelib's JPEG encoder compiled and loaded through the class resolver, skipped where its sources are not), Errors, and Benchmark.

## Performance

A stack-based interpreter with values that hold numbers in place, inline caches on property reads, writes and method calls (a class's field is a slot, found without a lookup by name once seen), fused instructions for `this.x` and `local.x`, call frames used again rather than made, and loops tested at the bottom. On a recent desktop (Release build):

- 1,000,000 iterations of arithmetic, a method call and a property read and write: about 100 ms
- 320 by 320 pixels with ten native calls each (1,000,000 calls to a host's `getPixel32` and `setPixel32`): about 65 ms
- Making a VM with its built-in library: about 0.2 ms

## Bytecode Design

See [BYTECODE_DESIGN.md](src/bytecode/BYTECODE_DESIGN.md).

## Acknowledgments

Inspired by:
- ActionScript Language Specification
- [Crafting Interpreters](https://craftinginterpreters.com) by Robert Nystrom
- Lua VM design
- V8 JavaScript engine
