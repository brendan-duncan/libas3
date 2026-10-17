# The Built-in Library

Every `as3::VM` is made with ActionScript 3's top-level classes and functions and those of `flash.utils` and `flash.errors` (`src/runtime/builtins_*.cpp`). Making a VM with them takes about a quarter of a millisecond.

What the library does not have is the Flash Player API: the display list, events, geometry, text, net, media and the rest of `flash.*`. A host provides what its scripts need as native classes ([EMBEDDING.md](EMBEDDING.md)); a script importing a class nobody provides gets a ReferenceError when it uses it.

The examples' output is what `test/library_tests.cpp` checks.

## Top-level functions and values

| | |
|-|-|
| `trace(...)` | Its arguments as strings, separated by spaces, to standard output. A host usually replaces it: `vm.defineFunction("trace", ...)`. `print` is the same function. |
| `isNaN`, `isFinite` | |
| `parseInt(text, radix)`, `parseFloat(text)` | `parseInt("12px")` is 12, `parseInt("0x1F")` 31, `parseFloat("3.5e2abc")` 350 |
| `escape`, `unescape` | As ActionScript has them: `escape("a b+c/")` is `a%20b+c/` |
| `encodeURI`, `encodeURIComponent`, `decodeURI`, `decodeURIComponent` | |
| `isXMLName` | |
| `undefined`, `NaN`, `Infinity` | |

## Object

`hasOwnProperty`, `propertyIsEnumerable`, `setPropertyIsEnumerable`, `isPrototypeOf`, `toString` (`[object Object]`, `[object ClassName]` for an instance), `toLocaleString`, `valueOf`, and `constructor`. An object may give itself its own `toString`, which string conversion uses.

## Array

`length` (settable), `push`, `pop`, `shift`, `unshift`, `splice`, `slice`, `concat`, `join`, `reverse`, `indexOf`, `lastIndexOf`, `forEach`, `map`, `filter`, `some`, `every`, `toString`, `toLocaleString`, `sort` and `sortOn`.

`sort` and `sortOn` take a compare function or ActionScript's options, combined with `|`: `Array.NUMERIC`, `Array.CASEINSENSITIVE`, `Array.DESCENDING`, `Array.UNIQUESORT` (0 when two are equal) and `Array.RETURNINDEXEDARRAY` (the sorted order as indexes, the array left as it was).

```actionscript
trace([10, 9, 1, 2].sort());                                // 1,10,2,9
trace([10, 9, 1, 2].sort(Array.NUMERIC | Array.DESCENDING)); // 10,9,2,1
people.sortOn("v", Array.NUMERIC | Array.DESCENDING);
```

`new Array(3)` has three empty places, `new Array(1, 2)` two elements; `new Array(-1)` is a RangeError (#1005). `insertAt` and `removeAt` (Flash Player 19) are not in the VM's library.

## Vector.\<T\>

A typed array: `new Vector.<int>(3)` is three zeros, `new Vector.<int>(2, true)` a fixed-length one, `Vector.<String>(["a", 1])` converts an array. Elements stored are converted to the element type (`v[0] = 2.7` in a `Vector.<int>` is 2). It has `Array`'s methods, giving vectors where `Array`'s give arrays, and `fixed`. Changing a fixed vector's length is a RangeError (#1126); writing past the end is a RangeError (#1125). `v is Vector.<int>` checks the element type; the class's name is `__AS3__.vec::Vector.<int>`.

## String

`length`, `charAt`, `charCodeAt`, `String.fromCharCode`, `indexOf`, `lastIndexOf`, `substr`, `substring`, `slice`, `split`, `replace`, `match`, `search`, `toUpperCase`, `toLowerCase`, `toLocaleUpperCase`, `toLocaleLowerCase`, `concat`, `localeCompare`, `toString`, `valueOf`.

- Strings are kept as UTF-8 and counted in UTF-16 units, as ActionScript counts them: `"héllo"` has length 5, and `charCodeAt` gives UTF-16 units.
- `split`, `replace`, `match` and `search` take a string or a regular expression. `replace` takes a replacement with `$1`, `$&` and the like, or a function called for each match.
- `String(null)` is `null`, `String(undefined)` `undefined`.

## Number, int, uint, Boolean

- `Number`: `toString(radix)`, `toFixed`, `toPrecision`, `toExponential`, `valueOf`; `MAX_VALUE`, `MIN_VALUE`, `NaN`, `NEGATIVE_INFINITY`, `POSITIVE_INFINITY`. `Number("0x1A")` is 26, `Number("")` 0, `Number("12px")` `NaN`.
- `int` and `uint`: `MAX_VALUE` and `MIN_VALUE`; `int(x)` and `uint(x)` convert and wrap (`uint(-1)` is 4294967295).
- `Boolean(x)`: `Boolean("")` is false, `Boolean("0")` true.

See [LANGUAGE.md](LANGUAGE.md#types-and-numbers) for how typed storage converts.

## Math

`abs`, `acos`, `asin`, `atan`, `atan2`, `ceil`, `cos`, `exp`, `floor`, `log`, `max`, `min`, `pow`, `random`, `round`, `sin`, `sqrt`, `tan`; `E`, `LN10`, `LN2`, `LOG10E`, `LOG2E`, `PI`, `SQRT1_2`, `SQRT2`. `Math.round(-2.5)` is -2, as ActionScript has it; `Math.max()` is `-Infinity`.

## Date

- `new Date()`, `new Date(milliseconds)`, `new Date(year, month, date, hours, minutes, seconds, ms)`, `new Date(text)`; `Date.UTC(...)` and `Date.parse(text)`.
- `getX` and `setX` for `FullYear`, `Month`, `Date`, `Hours`, `Minutes`, `Seconds` and `Milliseconds`, in local time and UTC (`getUTCHours`), and `getDay`/`getUTCDay`; the same as properties (`d.fullYear`, `d.month`, `d.hoursUTC`, `d.day`).
- `getTime`, `setTime`, `time`, `valueOf`, `getTimezoneOffset`, `timezoneOffset`.
- `toString`, `toDateString`, `toTimeString`, `toLocaleString`, `toLocaleDateString`, `toLocaleTimeString`, `toUTCString`, written as Flash Player writes them (`Sat Jan 1 00:00:00 2000 UTC`).

## RegExp

Literals (`/a+b/gi`) and `new RegExp(pattern, flags)`. `exec`, `test`, `source`, `global`, `ignoreCase`, `multiline`, `dotall`, `extended`, `lastIndex`.

Patterns run on `std::wregex` with ECMAScript syntax, plus ActionScript's `s` (dot matches newlines) and `x` (whitespace in the pattern ignored) flags and named groups: `(?P<word>[a-z]+)` gives `match.word`. What `std::regex`'s ECMAScript grammar lacks (lookbehind, for one) is not there either.

## JSON

`JSON.parse(text, reviver)` and `JSON.stringify(value, replacer, space)`, the replacer a function or an array of names. A class instance is written with its public variables and getters. Text that does not parse is a SyntaxError (#1132).

## Error and its subclasses

`Error` has `message`, `name`, `errorID` and `getStackTrace()` (the error, then the functions it was made in, a line each: `\tat app.Deep/inner()[Deep.as:8]`). Its subclasses are `TypeError`, `ReferenceError`, `RangeError`, `ArgumentError`, `SyntaxError`, `EvalError`, `URIError`, `SecurityError`, `VerifyError`, `DefinitionError`, `UninitializedError`, and in `flash.errors`, `IOError`, `EOFError`, `IllegalOperationError`, `InvalidSWFError`, `MemoryError`, `ScriptTimeoutError` and `StackOverflowError`. Scripts extend them:

```actionscript
class MyError extends Error {
    public function MyError(m:String) { super(m, 42); name = "MyError"; }
}
```

The VM's own errors carry Flash Player's numbers and messages: `TypeError: Error #1009: Cannot access a property or method of a null object reference.`, `#1010` (a member of undefined), `#1006` (not a function), `ReferenceError #1065` (not defined), `RangeError #1005`, `#1125`, `#1126`, `Error #1023` (stack overflow), `#1034` (type coercion), `#1085` (malformed XML).

## Function and Class

`Function`: `call`, `apply`, `length`. `Class` is the type of class values; a class called as a function casts (`Sprite(x)`), or converts for the built-in types (`int(x)`, `String(x)`, `Array(...)`).

## XML and XMLList

`XML` methods: `appendChild`, `attribute`, `attributes`, `child`, `childIndex`, `children`, `comments`, `contains`, `copy`, `descendants`, `elements`, `hasComplexContent`, `hasSimpleContent`, `insertChildAfter`, `insertChildBefore`, `length`, `localName`, `name`, `nodeKind`, `parent`, `prependChild`, `replace`, `setChildren`, `setName`, `text`, `toString`, `toXMLString`, `valueOf`. `XMLList` has the list forms of them.

The static settings `XML.ignoreComments`, `ignoreProcessingInstructions`, `ignoreWhitespace`, `prettyPrinting` and `prettyIndent`, and `XML.settings()`, `setSettings()`, `defaultSettings()`. These settings are shared by every VM in the process.

`new XML(text)` parses; malformed text is a TypeError (#1085). See [LANGUAGE.md](LANGUAGE.md#e4x) for the operators.

## flash.utils

| | |
|-|-|
| `Dictionary` | Objects as keys, by identity (strings and numbers as other objects have them: `d[1]` and `d["1"]` are one); `for-in` gives the keys, `for each` the values, `delete d[k]` removes one. The weak-keys argument is accepted. |
| `getDefinitionByName(name)` | A class, function or value by name (`"flash.utils.Dictionary"` or `"flash.utils::Dictionary"`), asking the class resolver if needed; ReferenceError #1065 when there is none |
| `getQualifiedClassName(value)` | `"com.example::Shape"`, `"int"` for 5, `"Number"` for 5.5, `"null"`, `"void"` |
| `getQualifiedSuperclassName(value)` | |
| `describeType(value)` | XML describing a class or instance: its name, base classes, interfaces, variables, constants, accessors and methods, with their metadata (as `describeType` in Flash Player, enough for as3corelib's JSON encoder) |
| `getTimer()` | Milliseconds since the VM was made, or by the host's clock |
| `setTimeout`, `setInterval`, `clearTimeout`, `clearInterval` | Timers the host runs with `vm.runTimers(now)` (see below) |

### Timers

Timers are kept on the VM and run only when the host says how much time has passed:

```cpp
double now = 0;
vm.setClock([&now]() { return now; });   // optional: getTimer() and timers follow this clock
vm.load("import flash.utils.*; setTimeout(function(a) { trace(a); }, 100, 'late');", "t.as");
now = 120;
vm.runTimers(now);   // traces "late"
```

Without a clock, time is measured from when the VM was made. `setTimeout` and `setInterval` pass their extra arguments to the function.

## What is not there

- `flash.utils.ByteArray`, `Proxy`, `Endian`, `Timer`, `IDataInput`/`IDataOutput` and the other `flash.*` classes. (The JavaScript runtime `as3.js` has `ByteArray` and `Endian`; see [JAVASCRIPT.md](JAVASCRIPT.md).)
- `Array.insertAt` and `removeAt`.
- `Namespace` and `QName`, and XML namespaces.
