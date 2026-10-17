# The Language

libas3 reads ActionScript 3 and the older ActionScript 1 and 2 with one parser (`parser/parser.h`). What happens to the program after that depends on where it goes:

| Back end | Reads | Semantics |
|----------|-------|-----------|
| The bytecode VM (`VM::load`, `Compiler` + `VM::execute`) | ActionScript 3, and ActionScript 1 and 2 as far as they are ActionScript 3 too | ActionScript 3's |
| The ABC compiler (`abc::compile`) and `abc::Domain` | ActionScript 3 | ActionScript 3's (AVM2) |
| The AS3 to JavaScript translator (`js::translate`) | ActionScript 3 | ActionScript 3's, on `as3.js` |
| The AVM1 compiler (`avm1::Compiler`) and `avm1::Interpreter` | ActionScript 1 and 2 | AVM1's, by SWF version |
| The AS2 to JavaScript translator (`js::as2::translate`) | ActionScript 1 and 2 | AVM1's, on `avm1.js` |

This page describes the language as the parser and the VM have it, then the older variants and where they differ. [LIBRARY.md](LIBRARY.md) covers the built-in classes and functions. The tests (`test/language_tests.cpp`, `test/as2_tests.cpp`, `test/xml_tests.cpp`) show each feature with the output it gives.

## ActionScript 3

### Files and packages

A source file may have packages, and code outside them (a frame script is just that code). Each package holds classes, interfaces, functions and variables:

```actionscript
package com.example.shapes {
    import flash.geom.Point;

    public class Circle {
        public var r:Number;
        public function Circle(r:Number) { this.r = r; }
    }
}

import com.example.shapes.Circle;
trace(new Circle(2).r);
```

- Classes are found by their fully qualified names (`new com.example.shapes.Circle(3)`), through single and wildcard imports (`import com.example.app.*`), and, inside a package, other classes of the same package without an import.
- A class the program does not have is asked of the host's class resolver (see [EMBEDDING.md](EMBEDDING.md)), by qualified name, when it is first needed: as a superclass, an imported name or a name of its package.
- Each definition is kept by its qualified name and, unless the name is taken, by its short name. When two classes share a short name, the first one defined has it; an import picks the one meant.

### Classes

```actionscript
package zoo {
    public class Animal {
        public static var count:int = 0;
        public var legs:int = 4;            // initialized for each instance
        protected var _name:String;

        public function Animal(name:String = "?") {
            _name = name;
            count++;                         // a static, unqualified
        }
        public function speak():String { return _name + " makes a sound"; }
        public function get name():String { return _name; }
        public function set name(value:String):void { _name = value; }
    }

    public class Dog extends Animal {
        public function Dog(name:String) {
            super(name);                     // implicit super() when left out
            legs = 3;
        }
        override public function speak():String {
            return super.speak() + ": woof";
        }
    }
}
```

- Constructors, `super(...)` (an implicit `super()` when a constructor does not call it), `super.method()` and `super.prop`.
- Fields with initializers, run for each instance (an `Array` field is a new array for each).
- Static variables, constants, functions, getters and setters, found unqualified inside the class and its subclasses and as `Class.member` outside.
- Getters and setters, inherited and overridden; overriding only one of the pair keeps the other.
- `override`, `final`, `dynamic` and the access attributes (`public`, `private`, `protected`, `internal`) are accepted. Access is not enforced: a private member can be read from outside its class, and new properties can be set on an instance of a class that is not `dynamic`. A class's private members are its own, though: a subclass's private `tag` does not hide its superclass's.
- A method read off an object is bound to it: `addEventListener(MouseEvent.CLICK, onClick)` calls `onClick` with the right `this`, and `obj.f == obj.f`.
- Script classes can extend the host's native classes, and native code calling a method reaches the script's override. See [EMBEDDING.md](EMBEDDING.md).

### Interfaces

Interfaces are declared, extend other interfaces, and are implemented; `is` and `as` check them:

```actionscript
interface IShape { function area():Number; }
interface ISolid extends IShape { function volume():Number; }
class Cube implements ISolid {
    public function area():Number { return 6; }
    public function volume():Number { return 1; }
}
var c = new Cube();
trace(c is IShape, (c as IShape).area());   // true 6
```

### Functions and closures

- Named functions, function expressions, and functions nested in functions, methods and blocks.
- Closures capture locals and parameters. Variables are function-scoped, as in ActionScript: every closure made in a loop sees the one loop variable.
- Default parameter values, `...rest`, `arguments` (and `arguments.callee`), `Function.call`, `apply` and `length` (the number of declared parameters).

```actionscript
function counter(start:int):Function {
    var n = start;
    return function():int { return ++n; };
}
var next = counter(5);
next();
trace(next());   // 7
```

### Statements

- `var` and `const` (locals too), `if`/`else`, `while`, `do`/`while`, `for`, `for-in` and `for each-in` (over objects, arrays, Dictionaries, XMLLists and host objects), `switch` (with fall-through, any type of case value), `break` and `continue` (labeled too) in every loop and `switch`, labeled blocks (`block: { ...; break block; }`), `return`, `throw`.
- `try` with several `catch` clauses by type, an untyped `catch`, and `finally`, which runs on `return`, `break`, `continue` and a rethrow.
- `with (object) { ... }`: names in the body are looked for on the object first.
- `include "file.as";` reads a file through the host (`VM::setIncludeResolver`, `Parser::setIncludeHandler`, or the ABC and JavaScript compilers' `includeHandler`). Without a handler the parser leaves the include out with a warning; with one, a file it does not have is an error. Includes work inside a class body too.
- `import`, `use namespace` (accepted and left out) and `namespace` declarations.
- Semicolons may be left out where a line ends.

### Expressions

- The arithmetic, comparison, logical, bitwise and assignment operators, `?:`, the comma operator, `++` and `--` (on variables, properties and elements).
- `typeof`, `delete`, `instanceof`, `in`, `is`, `as`, `void`, and `??`.
- Array and object literals, regular expression literals (`/(\d+)-(\d+)/g`, with the `g`, `i`, `m`, `s` and `x` flags), and vector literals (`new <int>[1, 2]`).
- XML literals and E4X operators (below).

```actionscript
trace(typeof 1, typeof "x", typeof null);    // number string object
trace(5 is int, 5.5 is int, "x" as String);  // true false x
trace(null ?? "default");                    // default
```

### Types and numbers

Types are written as ActionScript has them: `:*`, `:void`, class names (qualified too: `flash.display.Sprite`) and `Vector.<T>` (nested: `Vector.<Vector.<int>>`).

Numbers are doubles, as `Number` is. A value stored where `int` or `uint` is declared (a variable, field, parameter, return value or a `Vector.<int>`'s element) wraps to 32 bits as ActionScript's ToInt32 and ToUint32 do:

```actionscript
var c:uint = 0xFF000000;   // 4278190080
var i:int = 0xFF000000;    // -16777216
var half:int = 7 / 2;      // 3; 7 / 2 itself is 3.5
var big:int = 2147483647;
big++;                     // -2147483648
```

`&`, `|`, `^`, `<<` and `>>` give `int` results and `>>>` a `uint` one. `int(x)`, `uint(x)`, `Number(x)`, `String(x)` and `Boolean(x)` convert. A value stored where `Number`, `String` or `Boolean` is declared is converted to it, and one stored where a class type is declared turns `undefined` into `null`.

Typed variables and fields start as their type does: `int` and `uint` 0, `Number` `NaN`, `Boolean` `false`, other types `null`, untyped and `*` `undefined`.

Numbers are written as ActionScript writes them: `0.1 + 0.2` is `0.30000000000000004`, `1e21` is `1e+21`, `(255).toString(16)` is `ff`.

### E4X

XML is part of the language:

```actionscript
var n = 3;
var order:XML = <order id="7">
    <item sku="a1" price="3">Pen</item>
    <item sku="b2" price={n * 4}>Ink</item>
</order>;

trace(order.item.length(), order.item[1], order.@id);    // 2 Ink 7
trace(order..item.(@price > 4).@sku);                    // b2
order.item[0].@price = 2;
delete order.@id;
for each (var item:XML in order.item) trace(item.@sku);
```

- Literals, with `{expression}` for names, attribute values and content; `<>...</>` for an XMLList.
- `x.child`, `x.@attribute`, `x..descendant`, `x.*`, `x.@*`, filters (`x.item.(@price > 4)`), indexes, assignment and `delete` on all of them, and `for each` over lists.
- The `XML` and `XMLList` classes: see [LIBRARY.md](LIBRARY.md).

Not supported: XML namespaces. Prefixed names (`p:b`) are kept as they are written, but namespace-qualified access (`x.ns::name`, `namespace` objects, `QName`) is not.

### Metadata

Metadata tags before a class or a member are read and kept for the host, which acts on them (`[Embed]`, `[Event]`, `[Bindable]`, `[SWF]`, any tag):

```actionscript
[Event(name="change", type="flash.events.Event")]
public class Assets {
    [Embed(source="brush.png", mimeType="image/png")]
    public static var Brush:Class;
}
```

The host reads them from the class: `cls->metadata` for the class's, `cls->findTrait(name)->metadata` for an instance member's, `cls->findStatic(name)->metadata` for a static's. Each tag has its `name` and its `arguments` as `(key, value)` pairs, as written. `describeType` lists them too.

### Conditional compilation

`CONFIG::name` constants are defined on the parser (or in the ABC and JavaScript compilers' `Options::constants`). A block or definition after a constant is kept when the constant is true and left out when it is false; an undefined constant is false. In an expression a constant is its value:

```cpp
as3::Parser parser(source, "Main.as");
parser.defineConstant("CONFIG::debug", "true");
parser.defineConstant("CONFIG::level", "2");
parser.defineConstant("CONFIG::name", "\"phoenix\"");
```

```actionscript
CONFIG::debug { trace("debug build"); }
class C {
    CONFIG::debug public function f():String { return "debug f"; }
    CONFIG::release public function f():String { return "release f"; }
}
trace(CONFIG::level + 1, CONFIG::name);   // 3 phoenix
```

`VM::load` and the class resolver's classes are parsed without constants; to use them with the VM, parse with a `Parser`, compile with `Compiler` and run with `VM::execute` (see [EMBEDDING.md](EMBEDDING.md)).

### Lenient parsing

The parser accepts one common slip: two expressions on one line with nothing between them (`options.bleed 0.07;`, where an `=` was left out) are read as two statements, with a warning (`Parser::getWarnings`). `Parser::setStrict(true)` makes that an error.

Parsing never crashes on broken input; nesting deeper than the parser allows (thousands of parentheses, brackets or blocks) is an error ("Nested too deeply"), while long flat chains (`a + b + ...` with 20,000 terms) are fine.

### Not supported

- Namespaces beyond accepting their syntax. `mx_internal function f()` is a plain method, `use namespace` is left out, and `ns::name` is not kept.
- Access control (see Classes): `private`, `protected`, `internal`, `final` and sealed classes are not enforced on the VM.
- XML namespaces (see E4X).
- `flash.*` classes beyond `flash.utils` and `flash.errors`: the display list, events, geometry and the rest are the host's to provide, as native classes.

## ActionScript 1 and 2

Old Flash movies' scripts are ActionScript 1 (prototypes, timeline variables, `tellTarget`) and ActionScript 2 (classes in class files, with type annotations). The parser reads both, and has these forms that only they have:

- Statements end at a line break: `var a = 1` on one line and `trace(a)` on the next. (The parser allows this in ActionScript 3 too.)
- `on (release) { ... }` and `on (press, keyPress "<Enter>") { ... }`: a button's handlers.
- `onClipEvent (enterFrame) { ... }`: a movie clip's handlers.
- `tellTarget ("hat") { ... }`: code whose timeline actions act on another clip.
- `ifFrameLoaded (10) { ... }`: code that runs when a frame has loaded.
- `#initclip` and `#endinitclip` around a frame script's code that runs before the frame (the AVM1 compiler and the AS2 to JavaScript translator split these out; the VM's parser does not read them).
- `getProperty("hat", _x)` and `setProperty("hat", _x, 5)`: the bare `_x` names the property.

ActionScript 2 classes are written one to a file, with their package in the class's name:

```actionscript
import zoo.Animal;

class zoo.Dog extends Animal implements IPet {
    var legs:Number = 4;
    static var count:Number = 0;

    function Dog(name:String) { super(name); count++; }
    function speak():String { return super.speak() + ": woof"; }
    function get title():String { return "the dog"; }
    function set title(t:String) { }
}
```

`intrinsic` class declarations (which declare the player's own classes) are not read.

### Where they run, and how the results differ

There are three ways to run ActionScript 1 and 2 with libas3, and they do not give the same semantics.

**On the VM.** `VM::load` (or `execute`) compiles the script as it compiles ActionScript 3, so what runs is ActionScript 3's semantics with the older syntax. This is meant for frame scripts and simple handlers in a host that runs them itself:

- A frame script runs on its timeline when given a scope (`vm.load(source, file, clip)` or `vm.execute(code, clip)`): the clip is `this`, bare names are looked for on it before the globals, and assigning a name that is not a global (or declaring one with `var` at the top level) keeps it on the clip. A host function of the same name as one of the clip's methods wins over it (a global `gotoAndStop` that knows scenes).
- The older forms become calls to functions the host defines: `on (...) { }` and `onClipEvent (...) { }` call `__on(events, handler)` and `__onClipEvent(events, handler)`, with an array of the events as written (`"release"`, `"keyPress <Enter>"`) and the block as a function; `tellTarget (t) { }` calls `__tellTarget(t, function)`; `ifFrameLoaded (f) { }` is `if (ifFrameLoaded(f)) { }`. Without them, they are ReferenceErrors.
- The timeline functions (`gotoAndPlay`, `stop`, `getProperty`...), `_root`, `_global`, `_parent` and movie clips are the host's: global functions, host objects and scopes it provides.
- Names are case sensitive, `undefined` is `"undefined"` in strings and `NaN` in numbers, as from SWF 7.
- ActionScript 1's prototype classes do not work: a script function has no `prototype` to add methods to. `new` on a constructor function that sets `this.x` does work. ActionScript 2 class syntax (`class zoo.Animal { var legs:Number = 4; ... }`) compiles as a class.
- `eval`, `set` and slash paths (`"/hat:size"`) are not built in.

**As SWF actions.** `avm1::Compiler` compiles to the action bytecode a SWF's DoAction, DoInitAction and button and clip event tags carry, which Flash Player runs, or `avm1::Interpreter` runs on the VM with a host for the movie's clips. The semantics are AVM1's, by SWF version: before SWF 7 names are found whatever their case and `undefined` is `""` and `0`; prototypes, `__proto__`, `addProperty`, `ASSetPropFlags`, `Object.registerClass`, `eval`, `set`, slash and dot paths, `tellTarget`, `_global`, `_root` and `_levelN` all work. See [AVM1.md](AVM1.md).

**As JavaScript.** `js::as2::translate` makes a JavaScript module that runs on `avm1.js`, with AVM1's semantics by SWF version as above. See [JAVASCRIPT.md](JAVASCRIPT.md).

### What ActionScript 2 does not have

The AVM1 compiler and the AS2 translator reject what only ActionScript 3 has, with an error naming the line ("for each is not in ActionScript 2"): among it `for each`, rest parameters, vectors and `Vector.<T>`, a class outside a class file of its own, statements, expressions and operators AVM1 has no form for, and `on()` and `onClipEvent()` in a frame script (they belong to an instance's script). The AVM1 compiler also rejects getters and setters outside a class. The VM accepts all of these, since it runs everything as ActionScript 3.

### Differences at a glance

| | VM | AVM1 actions, interpreter | JavaScript on avm1.js |
|-|----|---------------------------|-----------------------|
| Case of names | Sensitive | Insensitive before SWF 7 | Insensitive before SWF 7 |
| `undefined` in strings, numbers | `"undefined"`, `NaN` | `""`, `0` before SWF 7 | `""`, `0` before SWF 7 |
| Numbers in strings | As ActionScript 3 (`0.30000000000000004`) | 15 significant digits (`0.3`) | 15 significant digits (`0.3`) |
| Prototype classes | No | Yes | Yes |
| `on()`, `onClipEvent()` | Calls `__on`, `__onClipEvent` | Handler action blocks | `movie.handlers(...)` |
| `tellTarget` | Calls `__tellTarget` | `SetTarget` | A scope with another target |
| Timeline functions, `_root`, paths | The host's globals | Actions, `avm1::Host` | `avm1.js` and `avm1.host` |
| A member of `undefined` | TypeError #1010 | `undefined` | `undefined` |
| Calling an undefined function | ReferenceError #1065 | Nothing happens | Nothing happens |
