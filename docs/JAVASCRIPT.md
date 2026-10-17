# Translating to JavaScript

For movies published to HTML5, libas3 translates ActionScript to readable JavaScript that runs in a browser (or Node) without a virtual machine:

- `as3::js::translate` (`js/js.h`): ActionScript 3 classes and timelines to ES2020 classes, running on `as3.js`.
- `as3::js::as2::translate` (`js/as2.h`): a movie's ActionScript 1 and 2 to one JavaScript module, running on `avm1.js`.

Both runtimes are embedded in the library (`as3::js::runtime()` and `as3::js::as2::runtime()` return their source) and are in the repository as `src/js/as3.js` and `src/js/avm1.js`. Neither includes the Flash Player API: the display list, events and the other `flash.*` classes come from a JavaScript runtime the host provides (Phoenix's, in its `resources/html5`), which the translated code meets at the points described below.

The tests are `test/js_tests.cpp` and `test/js_as2_tests.cpp`. Where Node is installed they run each program both in the VM (or on an AVM1 machine) and translated in Node, and check that the two trace the same.

## ActionScript 3 to JavaScript

`js::translate` takes the same sources and timelines as the ABC compiler ([ABC.md](ABC.md)), and shares its front end (`abc_model.h`: the files parsed, their packages' definitions, the classes with their members, superclasses and interfaces resolved, the timelines' classes made from their frame scripts). What compiles to ABC for a player translates to JavaScript for a page.

```cpp
#include "js/js.h"

as3::js::Timeline main;                        // as for ABC: frame scripts, the stage's instances
main.className = as3::js::timelineClassName("game");   // "game_fla.MainTimeline"
main.instances = {{"ball", "com.example.Ball"}};
main.frames = {{0, "ball.speed = 2;\nstop();"}};

as3::js::Output out = as3::js::translate({{readFile("com/example/Ball.as"), "Ball.as"}}, {main});
if (!out.ok) std::cerr << out.errorText();    // "Ball.as:12: error: cannot translate ... to JavaScript"
// out.files: "com/example/Ball.js", "game_fla/MainTimeline.js", each with a sourceMappingURL
// out.bundle: every file in order, then as3.runScripts()
// out.sourceMaps: each file's ("com/example/Ball.js.map") and "bundle.js.map"
// out.classes: "com.example.Ball", ..., "game_fla.MainTimeline"
```

A package's class is written at its package's path (`com/example/Ball.js`); other sources by their own names (`main.js`). Each output line maps back to its `.as` line in the source maps (version 3); a timeline's map has each frame's script as a source of its own.

### Options

- `constants`: conditional compilation constants (`{"CONFIG::debug", "true"}`).
- `includeHandler`: what `include "file.as"` reads.
- `sourceMaps` (on): a source map for each file and for the bundle.
- `comments` (on): the sources' comments on lines of their own are kept, with the code they are by.
- `coerceAllParameters` (off): typed `int` and `uint` parameters are always converted on entry (`x |= 0`); with this, `Number`, `String` and `Boolean` ones too.

### Loading it

A page loads `as3.js`, the flash.* runtime, then the bundle (or the files, followed by `as3.runScripts()`), and makes the document's class:

```cpp
std::ofstream("as3.js") << as3::js::runtime();
std::ofstream("bundle.js") << out.bundle;
std::ofstream("bundle.js.map") << out.sourceMaps.at("bundle.js.map");
```

```html
<script src="as3.js"></script>
<script src="flash-runtime.js"></script>   <!-- the host's flash.* classes -->
<script src="bundle.js"></script>
<script>
    const Main = as3.getDefinitionByName("game_fla.MainTimeline");
    const root = new Main();
</script>
```

In Node, `as3.js` runs as a script (it defines the global `as3`); the tests run it with `vm.runInContext`, set `as3.output` to collect what is traced, then run the bundle.

### What is made

This source:

```actionscript
package geom {
    public class Shape {
        public static const SIDES:int = 0;
        public var name:String;
        private var count:int;

        public function Shape(name:String = "shape") {
            this.name = name;
        }

        // How big it is.
        public function area():Number {
            return 0;
        }

        public function grow(by:int):int {
            count += by;
            return count / 2;
        }

        public function get label():String {
            return name + " " + area();
        }
    }
}
import geom.Shape;
var s:Shape = new Shape("box");
trace(s.label, s is Shape, s.grow(3));
```

translates to this `geom/Shape.js`:

```javascript
// main.as, translated to JavaScript by libas3.
"use strict";
(() => {
    as3.define("geom.Shape", () => {
        class Shape {
            constructor(...args) {
                this.name = null;
                this.count = 0;
                if (new.target === Shape) this.$construct(...args);
            }

            $construct(name = "shape") {
                this.name = name;
            }

            // How big it is.
            area() {
                return 0;
            }

            grow(by) {
                by |= 0;
                this.count = (this.count + by) | 0;
                return (this.count / 2) | 0;
            }

            get label() {
                return this.name + " " + this.area();
            }
        }
        Shape.SIDES = 0;
        return {
            class: Shape,
            fields: { name: "String", count: "private int" },
        };
    });

    let Shape;
    var s = null;

    as3.script("main.as", () => {
        Shape = as3.use("geom.Shape");
        s = new Shape("box");
        as3.trace(s.label, as3.is(s, Shape), s.grow(3));
    });
})();
//# sourceMappingURL=Shape.js.map
```

A class extending one of the player's gets its superclass when it is made, and binds the rest of what it uses in `init`:

```actionscript
package com.example {
    import flash.display.Sprite;
    import flash.events.Event;

    public class Ball extends Sprite {
        public static const RADIUS:int = 10;
        public var speed:Number;
        private var hits:int;

        public function Ball(speed:Number = 2) {
            this.speed = speed;
            addEventListener(Event.ENTER_FRAME, move);
        }

        // Each frame, a step to the right.
        private function move(e:Event):void {
            x += speed;
            if (x > 400) hits++;
        }

        public function get bounces():int { return hits; }
    }
}
```

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
            this.addEventListener(Event.ENTER_FRAME, as3.bind(this, "move"));
        }

        // Each frame, a step to the right.
        move(e) {
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
//# sourceMappingURL=Ball.js.map
```

- **Packages** are nested objects (`com.example.Ball`, `flash.display.MovieClip`). `as3.define(name, factory)` puts a getter there; the first use (`as3.use`, the getter, `getDefinitionByName`) runs the factory, which makes the class with its superclass (a `const`, needed now) and gives the descriptor. The class is then registered, so classes may use each other in any order, and `init` binds the rest of what the class uses (the `let` names: imports, its package's classes, the player's, each defined then if it is not yet) and runs its static initializers and code in the order written. A class's statics are ready before it is first used. Its constants of literal values are set when it is made.
- **Fields** are the object's own properties. Each takes its constant value, or its type's default (`int` and `uint` 0, `Number` `NaN`, `Boolean` `false`, others `null`, untyped `undefined`), in the JavaScript constructor, before any ActionScript constructor runs, as a player has them when the object is made. One with a value to compute takes it in `$construct`, the ActionScript constructor, before the constructor's code (so before `super()`, as ActionScript has it). `super(...)` is `super.$construct(...)`, put first when the constructor does not call it.
- **Members**: methods, getters, setters and statics are the class's; an accessor overriding only its getter (or setter) gets the other half from its superclass. A private member whose name a superclass or subclass has (or the player's class it extends: a `MovieClip` subclass's private `name`) takes its class's name (`move$Ball`), so two classes' privates do not clash; others keep their names. A method read as a value is bound to its object, the same function each time (`as3.bind`), so `removeEventListener` finds what `addEventListener` was given.
- **Interfaces** are values (`as3.define("a.IShape", () => ({ interface: true, extends: [...] }))`) that `is`, `as` and `instanceof` check a class's interfaces against.
- **Locals** are `let` (or `const`) where the function's scoping and JavaScript's agree, `var` otherwise. A typed variable declared without a value in a block is declared at the function's start with its default, as ActionScript has function-scoped variables. Nested functions are arrow functions (so `this` and the class's members are the method's), or plain functions when they use `this` or `arguments` (the method's `this` is then `$this`).
- **Code outside packages** runs with `as3.runScripts()` (the bundle ends with it). A file's classes and functions outside its packages are the file's own (`as3.file(name)`), its variables those of the file's scope.
- **Timelines** are classes extending their base (`flash.display.MovieClip`) whose `$construct` calls `this.addFrameScript(0, as3.bind(this, "frame1"), ...)`. The frames' variables and stage instances are fields, their functions methods, `frame1` and the rest their code. Names not otherwise known are the clip's (`stop()` is `this.stop()`), and the flash.* packages are imported, as for ABC.
- **Names** are resolved as the ABC compiler resolves them: locals, the class's members (`this.x`), statics (`Ball.RADIUS`), imported and same-package definitions, the top level. In a class extending one of the player's, a name not otherwise known is the object's (`this.stage`).

### The language's semantics JavaScript lacks

- **`int` and `uint`**: a value stored in an `int` or `uint` (variable, field, parameter, return value, element of a typed Vector) is converted, `(a / b) | 0` and `x >>> 0`, so division, overflow and `0xFF000000` are as ActionScript has them; `int(x)` is `x | 0`. A value of another type stored to `Number`, `String` or `Boolean` is converted too (`+x`, `as3.str(x)`, which keeps null, and `!!x`). Literals are written as in the source (`0xFF336699` stays hexadecimal).
- **Operators and functions**: `is`, `as`, `typeof` (`"xml"`, and `"object"` for a class), a class called as a cast (`Sprite(x)` is `as3.cast(x, Sprite)`, a TypeError when it is not one), `trace` (`as3.trace`, writing `[class Ball]`, `null` and the rest as a player does, to `as3.output`), `String(x)`, `parseInt` (a leading 0 is octal), `getQualifiedClassName` (`"com.example::Ball"`), `getQualifiedSuperclassName`, `getDefinitionByName`, `describeType` (a minimal one: the class, its bases, interfaces, public variables, constants, accessors and methods).
- **`for-in` and `for each`** are `for (... of as3.keys(o))` and `as3.values(o)`: over objects, arrays, a Dictionary's keys (objects too), an XMLList's items, a dynamic class's own properties but not a class's fields; null gives none.
- **`Vector.<T>`** (`as3.Vector(as3.int)`; `new Vector.<int>(n, fixed)` is `as3.vector(as3.int, n, fixed)`): an Array of its element type, filled with its defaults, converting what `push`, `unshift` and `splice` add, with `fixed`, its methods giving vectors.
- **`Array`**: `sort` and `sortOn` with their options (`Array.NUMERIC`, `CASEINSENSITIVE`, `DESCENDING`, `UNIQUESORT`, `RETURNINDEXEDARRAY`), `insertAt`, `removeAt`. `a.sort(...)` is `as3.sort(a, ...)` where `a` may be an array.
- **`flash.utils.Dictionary`**: objects as keys. `d[k]` of a typed Dictionary is `as3.getItem(d, k)` (and `setItem`, `hasItem`, `deleteItem`); of a string key on any object it stays `d[k]`, which a Dictionary also answers.
- **Statements**: `arguments`, `...rest`, default values, labels, `switch`, `with` (each name looked for on the object first, as the VM has it: `as3.has(o, "x") ? o.x : x`), `try` with `catch` clauses by type. A JavaScript error caught becomes the player's (`TypeError: Error #1009: Cannot access a property or method of a null object reference.`, `#1006`, `#1065`, `#1023`...), and `Error`, its subclasses and `flash.errors.*` are as3.js's (`errorID`, `getStackTrace()`), to extend and throw.
- **E4X**: XML literals (`<item id={n}>{text}</item>`) are `as3.toXML(...)`; `x.child`, `x["@id"]`, `x.children()`, `x.attributes()`, `x.descendants("name")`, filters (`as3.xmlFilter(list, ($item) => ...)`), assignment and `delete` work on XML and XMLList objects (proxies over a tree), with their methods and `toXMLString()` as the VM writes it. A child named as a method (`name`, `length`, `text`...) of an object typed `XML` or `XMLList` is read with `child("name")`.
- **Date**'s properties (`fullYear`, `month`, `time`...), and its strings as a player writes them where the type is known (`as3.dateText(d, "toUTCString")`). Regular expressions with named groups (`(?P<word>...)`) give the groups on the match.
- **Timers**: `setTimeout`, `setInterval`, `clearTimeout`, `clearInterval` and `getTimer` count from `as3.clock()`. With `as3.manualTimers` set, the host runs what is due with `as3.runTimers(now)`, as the VM's `runTimers`.

### What is not translated

What cannot be translated is an error naming it (`cannot translate arguments.callee to JavaScript`): `arguments.callee`, `super` in a nested function that has its own `this`, and a type applied to anything but `Vector`. What the parser does not keep (namespaces beyond the access ones, `ns::name`, XML namespaces) is not translated either. ActionScript 1 and 2 have a translator of their own (below).

Known differences from the VM:

- A function's `length` counts the parameters before the first with a default.
- Writing past a Vector's end extends it rather than throwing RangeError #1125.
- `JSON.stringify` of a class instance gives its fields in their order.
- `describeType` gives no metadata.
- `++` and `--` on an `int` inside an expression or a `for` loop's update do not wrap at 2147483647.
- A value of unknown type stored in an object-typed variable stays `undefined` (rather than `null`) when it is `undefined`.

### The runtime interface

The flash.* runtime and translated code meet in a few places, all in `as3.js` and easily adjusted there:

- **Definitions**: `as3.define(name, factory)` (translated code), `as3.register(name, value)` (the runtime's classes and functions, defined at once), `as3.use(name)`, `as3.isDefined(name)`, `as3.getDefinitionByName(name)` (`"a.b::C"` or `"a.b.C"`). A name not defined through as3 is looked for in the package tree (a runtime that puts `flash.display.Sprite` on `globalThis.flash` itself needs no registering) and then by each of `as3.resolvers`, functions `(name) => value` the runtime adds. One not found at all, or whose definition failed (its superclass missing), is a stand-in that throws that error (ReferenceError #1065) when it is used, as a player throws, so a class may use what a movie does not have (`mx.*`, `fl.controls.*`) and still load.
- **Classes**: a runtime class that translated code extends follows its construction protocol. The JavaScript constructor only makes the object (its own state at defaults) and, for the class actually made, runs the ActionScript constructor, `$construct(...args)`; a subclass's `$construct` calls `super.$construct(...)` with the arguments ActionScript's `super(...)` gives:

  ```javascript
  class Sprite extends DisplayObjectContainer {
      constructor(...args) { super(); if (new.target === Sprite) this.$construct(...args); }
      $construct() { super.$construct(); /* what new Sprite() does */ }
  }
  as3.register("flash.display.Sprite", Sprite);
  ```

  The root class (`EventDispatcher`'s base, or `EventDispatcher` itself) has a `$construct` that does not call a super one. Translated classes extending `Object` have neither `super()` nor `super.$construct()`. A runtime class without `$construct` (a constructor that does everything) gets one when as3 finds it (`as3.adopt`): with no arguments it does nothing, the object being made already; with arguments (`super(type, true)` in an `Event` subclass) it makes one with them and takes its own properties, which serves classes whose state is their properties.
- **Library symbols and stage instances**: the runtime makes a symbol's instance with `new Cls()` (its class from `as3.getDefinitionByName(linkageName)`: a translated class, or a timeline's). The state that ties the object to its symbol is set in the runtime's JavaScript constructor (from a symbol the runtime notes before `new`), and stage instances are set as `this[instanceName]` in the runtime's JavaScript constructor or in `MovieClip.prototype.$construct` (the subclass's `super()`). A translated class keeps what is there for its public fields of display types and a timeline's stage instances (`this.ball = this.ball ?? null`). Frame scripts come by `addFrameScript(frame, method, ...)`, the methods bound.
- **Output and time**: `trace` writes to `as3.output(text)` (`console.log` until the runtime sets its own); `as3.clock()` gives the milliseconds since the movie started, which `getTimer` and the timers count; `as3.manualTimers` and `as3.runTimers(now)` let the runtime drive the timers from its frames. `as3.error(e)` gives a thrown value as ActionScript has it (a JavaScript `TypeError` as the player's `#1009`, and so on).
- **What as3.js defines**: `flash.utils`' functions, `Dictionary`, `ByteArray` (both byte orders, `b[i]`, the read and write methods except AMF's and compression) and `Endian`, and `flash.errors`' classes. The runtime may replace any of them with `as3.register`.

## ActionScript 1 and 2 to JavaScript

`as3::js::as2::translate` makes one JavaScript module of a movie's ActionScript 1 and 2: its frame scripts (and their `#initclip` parts), the `on()` and `onClipEvent()` handlers of its instances, its ActionScript 2 classes, and the symbols linked to classes. It runs on `avm1.js`, which gives the language as AVM1 has it, and on a host that gives movie clips their ActionScript 2 face (Phoenix's `resources/html5/as2.js`).

```cpp
#include "js/as2.h"

as3::js::as2::Movie movie;
// timeline ("" for the main one, else the symbol's library name), frame from 0,
// source, scene, and where it is for messages.
movie.frames.push_back({"", 0, "stop();\nhat._visible = false;", "Scene 1", "Scene 1, frame 1"});
// an instance's handlers: the host's id for the instance, source, where.
movie.instances.push_back({7, "on (release) { gotoAndPlay(2); }", "button play_btn"});
movie.classes.push_back({readFile("com/studio/Hat.as"), "Hat.as"});
movie.registrations.push_back({"hat", "com.studio.Hat"});   // Object.registerClass("hat", com.studio.Hat)

as3::js::as2::Options options;
options.swfVersion = 8;                         // from 7 names are case sensitive
as3::js::as2::Output out = as3::js::as2::translate(movie, options);
// out.code: avm1.movie(function ($, movie) { ... });
// out.errors: "Scene 1, frame 1, line 3: ..."; out.classes: the classes defined
```

A script that does not translate is left out with its error; the rest of the movie is in `out.code` all the same (`out.ok` is false). `Options::includeHandler` reads `include "file.as"`.

### What is made

This frame script:

```actionscript
var count = 0;
function add(n) {
    var total = 0;
    for (var i = 0; i < n; i++) {
        total += i;
    }
    return total;
}
stop();
hat._x = add(3) + count;
```

translates to:

```javascript
// ActionScript 1 and 2, translated to JavaScript by libas3.
avm1.movie(function ($, movie) {
    const $g = $.global;
    $.version = 8;
    // Scene 1, frame 1
    movie.frame("", 0, function ($s) {
        $s.def("add", function add(n) {
            var total = 0;
            for (var i = 0; $.lt(i, n); i++) {
                total = total + i;
            }
            return total;
        });
        $s.def("count", 0);
        $s.stop();
        $.put($s.get("hat"), "_x", $.add($s.call("add", [3]), $s.get("count")));
    });
});
```

Handlers, classes and registrations add:

```javascript
    movie.classes({
        "com.studio.Hat": [["MovieClip"], function ($s) { /* defines _global.com.studio.Hat */ }],
    });
    movie.register("hat", "com.studio.Hat");
    movie.handlers(7, [{events: ["release"], clip: false, run: function ($s) { /* gotoAndPlay(2); */ }}]);
```

- **Scopes**: each script is a function of its scope, `$s` (an `avm1.Scope`), called with the timeline's clip as `this`. A function's parameters and variables are JavaScript's own locals; every other name goes along the scope chain as AVM1 has it: `with` blocks' objects, the timeline the code is on (its target), then `_global` (`$s.get`; `$s.call` calls a function with the object it was found on as `this`). An assignment to a name not found sets it on the timeline, never on `_global`; `var` on a timeline is the timeline's (`$s.def`). `_root`, `_parent`, `_global`, `_levelN` and `this` are what they are where the code runs. A function keeps the scope it was defined in, so a timeline's function finds that timeline's names wherever it is called from.
- **ActionScript 2 classes**: in a class's methods a bare name that is one of the class's members (or those of the classes it extends, MovieClip's among them) is `this.name`, a static one `Class.name`. A class is a prototype function on `_global` (`com.studio.Hat`), its instance variables' values on its prototype, getters and setters properties (as `addProperty` makes them), `super(...)` and `super.method(...)` the superclass's, `implements` checked by `instanceof`. Classes are defined before those that extend them.
- **Timeline actions**: the global functions that are actions of their own act on the scope's target: `play`, `stop`, `gotoAndPlay` (a main timeline's script names its scene, so a frame number counts from that scene's start), `nextScene`, `getProperty`, `setProperty`, `duplicateMovieClip`, `removeMovieClip`, `startDrag`, `tellTarget` (a scope with another target), `eval` and `set` (paths: `"/a/b:score"`, `"_root.a.b"`, `"../a"`), `loadMovie`, `loadVariables`, `ifFrameLoaded`, `call`. `trace`, `getTimer`, `random`, `int`, `chr`, `ord`, `length`, `substring`, `getURL` and `fscommand` are `avm1.js`'s. The others (`setInterval`, `escape`, `parseInt`...) are on `_global`, as a player has them.
- **Conversions**: operators convert as AVM1 does: `$.add` (strings when either side is one), `$.n` and `$.s` (ToNumber and ToString: numbers with 15 significant digits, `0.1 + 0.2` is `0.3`; `"0x1A"` is 26; `null` and `undefined` are `NaN`, and `undefined` is `"undefined"` in strings, from SWF 7; 0 and `""` before), `$.eq` (`==`), `$.lt` and the other comparisons (`undefined` when a side is `NaN`), `$.typeOf` (`"movieclip"`, `"null"`), `$.instanceOf`. Where both sides are known to be numbers, the JavaScript operator is used as it is. Before SWF 7 a string is true when it is a number other than 0.
- **Members of non-objects**: a member of what is not an object is `undefined`, as AVM1 has it, not an error: `a?.b`, `$.put(o, "k", v)`, `o?.f?.(...)`. Calling a function that is not there does nothing.
- `for (k in o)` gives the names the latest made first, as AVM1 does (`$.keys`). `delete`, `with`, `switch`, labels, `try` with typed `catch` clauses, `arguments` (and `arguments.callee`: the module is not strict), `Function.prototype.apply` and `call` are JavaScript's. Arrays' `sort` and `sortOn` take AVM1's options (`$.sort(a, ...)`).
- **Case**: from SWF 7 names count their case. Before, a name is found whatever its case: the translator gives every name the movie's scripts have one spelling (the player's for its own names, `gotoAndPlay`, `_x`, `Math`; else the first written), and the module gives that table to `avm1.js` (`$.names`), which folds the names scripts make at run time (`o[k]`, `eval`, paths, instance names the host registers) the same way. A name made at run time that no script spells keeps its own case.

Not translated: `intrinsic` classes (the parser does not read them: they declare the player's own classes, which the host gives), `__resolve` and `watch`. A function's `arguments.caller` is not there.

### The host

`avm1.js` defines `avm1` (and `module.exports` in Node). A host:

- sets `avm1.host`: `root(clip)` and `level(n)` (the movie's timelines), `targetPath(clip)` (`"_level0.a.b"`), `gotoScene(clip, scene, frame, play)`, `scene(clip, ±1)`, `callFrame(clip, frame)`, `stopDrag()`, `getURL(url, window, method)`, `fscommand(command, argument)`, `stopAllSounds()`;
- sets `avm1.output` (where `trace` writes), `avm1.clock` (milliseconds since the start), and `avm1.manualTimers` with `avm1.runTimers(now)` to drive `setInterval` from its frames;
- gives movie clips as objects with `$clip` true, whose timeline actions are their methods (`gotoAndPlay`, `play`...) and whose properties (`_x`, `_name`, `_parent`) it keeps;
- runs the translated modules with `avm1.runMovies(movie)`, `movie` having `classes(defines)` (give it to `avm1.defineClasses(defines, scope)`), `register(linkage, className)`, `initclip(timeline, order, run)`, `frame(timeline, frame, run)` and `handlers(id, list)`. Each `run` is called with the timeline as `this` and a scope, `run.call(clip, avm1.scope(clip))`.

`avm1.registered` holds the classes `Object.registerClass` linked, by linkage name.

```javascript
avm1.output = (text) => console.log(text);
avm1.host.level = (n) => (n === 0 ? root : undefined);
avm1.host.targetPath = (clip) => pathOf(clip);
const frames = [];
avm1.runMovies({
    classes(defines) { avm1.defineClasses(defines, avm1.scope({})); },
    register(linkage, name) { Object.registerClass(linkage, avm1.qualified(name)); },
    initclip(timeline, order, run) { /* run before the frame, by order */ },
    frame(timeline, frame, run) { frames.push([frame, run]); },
    handlers(id, list) { /* list: {events, clip, run} for the instance id */ },
});
for (const [frame, run] of frames) run.call(root, avm1.scope(root));
```

(This is the shape of the stand-in player in `test/js_as2_tests.cpp`.)
