#pragma once

// Programs the JavaScript suite runs both in the VM and, translated, in
// Node: the semantics translated code must keep, one area each.

struct TestProgram {
    const char* name;
    const char* code;
};

const TestProgram kPrograms[] = {
    {"trace and arithmetic", R"as3(
trace(1 + 2, "a" + 1, 7 / 2, 7 % 3, -5 >> 1, -5 >>> 28, 1 << 31);
trace(null, undefined, true, 0.1 + 0.2, 1e21, 1 / 3, -0, 0xFF, 1e-7, 123456789012);
)as3"},

    {"int and uint wrap where stored", R"as3(
var i:int = 2147483647; i++; trace(i);
var j:int = int.MAX_VALUE; j += 1; trace(j);
var u:uint = 0; u--; trace(u);
var w:uint = 4294967295; w += 2; trace(w);
var d:int = 7 / 2; trace(d, -7 / 2, int(-7 / 2));
var c:uint = 0xFF000000; var k:int = 0xFF000000; trace(c, k);
var m:int = 3.9; var n:int = -3.9; var s:int = int("12.7"); trace(m, n, s, uint(-1), int("0x10"), int(NaN), int(Infinity));
var t:int = 5; t *= 1.5; trace(t); t /= 2; trace(t); t <<= 30; trace(t);
var big:int = 65536 * 65536 + 3; trace(big);
var x:Number = 7; x /= 2; trace(x);
)as3"},

    {"Typed parameters and returns convert", R"as3(
function half(v:int):int { return v / 2; }
function count(n:uint):uint { return n; }
function last(s:String):String { return s; }
trace(half(7.9), half(-3), count(-1), half("9"));
var r:int = half(5) + half(5); trace(r);
function f():Number { return 1 / 4; } trace(f());
function g():int { return 1 / 4; } trace(g());
)as3"},

    {"Typed variables start at their defaults", R"as3(
var i:int; var u:uint; var n:Number; var b:Boolean; var s:String; var o:Object; var a:Array; var x; var star:*;
trace(i, u, n, b, s, o, a, x, star);
var s2:String = undefined; var n2:Number = undefined; var o2:Object = undefined; var b2:Boolean = undefined; var i2:int = undefined;
trace(s2, n2, o2, b2, i2);
var s3:String = null; s3 += "x"; trace(s3);
var n3:Number = null; trace(n3);
function later():String { var q:String; return q; } trace(later());
for (var k:int = 0; k < 2; k++) { var inLoop:int; inLoop++; } trace(inLoop);
)as3"},

    {"Conversions: Number, String, Boolean", R"as3(
trace(Number("3.5"), Number(" 12 "), Number(""), Number("abc"), Number(null), Number(undefined), Number(true), Number("1e3"));
trace(String(null), String(undefined), String(1.5), String(true), String([1, [2, 3]]), String({}));
trace(Boolean(""), Boolean("0"), Boolean(0), Boolean(NaN), Boolean(null), Boolean({}), Boolean("false"));
var s:String = 5; var b:Boolean = "yes"; var n:Number = "8"; trace(s + 1, b, n + 1);
trace(1 + "2", "3" * "4", "10" / 4, "a" < "b", 2 < "10", null + 1, undefined + 1);
trace(parseInt("077"), parseInt("12abc"), parseFloat("1.5e2x"), isNaN("x"), (12.345).toFixed(1), (0.1).toString());
)as3"},

    {"Classes: fields before the constructor body", R"as3(
class Base {
    public var log:String = "";
    public function Base() { log += "base;"; setup(); }
    public function setup():void { log += "base setup;"; }
}
class Sub extends Base {
    public var size:int = 5;
    public var list:Array = [1, 2];
    public function Sub() { log += "sub before;"; super(); log += "sub after;"; }
    override public function setup():void { log += "sub setup " + size + " " + list.length + ";"; }
}
var s:Sub = new Sub(); trace(s.log);
class Implicit extends Base { public var n:Number = 1.5; override public function setup():void { log += "n=" + n + ";"; } }
trace(new Implicit().log);
)as3"},

    {"Classes: getters, setters and overriding half", R"as3(
class A {
    protected var _v:int = 1;
    public function get v():int { return _v; }
    public function set v(x:int):void { _v = x * 10; }
    public function get ro():String { return "ro"; }
}
class B extends A {
    override public function get v():int { return super.v + 1; }
}
var b:B = new B(); trace(b.v); b.v = 3; trace(b.v, b.ro);
class C extends A { override public function set v(x:int):void { super.v = x + 1; } }
var c:C = new C(); c.v = 4; trace(c.v);
)as3"},

    {"Classes: statics and their initializers", R"as3(
class Counter {
    public static var count:int = 0;
    public static const NAMES:Array = ["a", "b"];
    public static var made:Counter = new Counter();
    public static function next():int { return ++count; }
    public var id:int;
    public function Counter() { id = next(); }
}
trace(Counter.count, Counter.NAMES, Counter.made.id);
var c1:Counter = new Counter(); var c2:Counter = new Counter(); trace(c1.id, c2.id, Counter.count);
class Child extends Counter { public function inc():int { return next() + count; } }
trace(new Child().inc());
)as3"},

    {"Classes: private names in a chain do not clash", R"as3(
class Base {
    private var secret:String = "base";
    private function hidden():String { return "base hidden"; }
    public function baseView():String { return secret + "/" + hidden(); }
}
class Sub extends Base {
    private var secret:String = "sub";
    private function hidden():String { return "sub hidden"; }
    public function subView():String { return secret + "/" + hidden(); }
}
var s:Sub = new Sub(); trace(s.baseView(), s.subView());
class Other { private var secret:int = 3; public function same(o:Other):int { return o.secret + secret; } }
trace(new Other().same(new Other()));
)as3"},

    {"Classes: methods read as values stay bound", R"as3(
class Button {
    private var listeners:Array = [];
    public function add(f:Function):void { listeners.push(f); }
    public function remove(f:Function):void { var i:int = listeners.indexOf(f); if (i >= 0) listeners.splice(i, 1); }
    public function click():void { for each (var f:Function in listeners) f(); }
    public function get count():int { return listeners.length; }
}
class Game {
    public var clicks:int = 0;
    public var button:Button = new Button();
    public function Game() { button.add(onClick); button.add(onClick); }
    private function onClick():void { clicks++; }
    public function stop():void { button.remove(onClick); }
}
var g:Game = new Game(); g.button.click(); trace(g.clicks, g.button.count);
g.stop(); g.button.click(); trace(g.clicks, g.button.count);
var f:Function = g.stop; trace(f == g.stop);
)as3"},

    {"Classes: toString, trace of classes and instances", R"as3(
class Point { public var x:Number; public var y:Number;
  public function Point(x:Number = 0, y:Number = 0) { this.x = x; this.y = y; }
  public function toString():String { return "(" + x + ", " + y + ")"; } }
class Plain {}
trace(new Point(1, 2), new Plain(), Point, [new Point(3, 4)], "p=" + new Point());
trace(Object(new Plain()).toString());
)as3"},

    {"Classes: dynamic classes and for-in", R"as3(
dynamic class Bag { public var fixed:int = 1; }
var b:Bag = new Bag(); b.extra = 2; b["more"] = 3;
var keys:Array = []; for (var k:String in b) keys.push(k); keys.sort(); trace(keys);
trace(b.fixed, b.extra, b.more, "extra" in b, "fixed" in b, "none" in b);
delete b.extra; trace(b.extra, "extra" in b);
class Sealed { public var a:int = 1; } var n:int = 0; for (var s:String in new Sealed()) n++; trace(n);
)as3"},

    {"Interfaces: is, as and inheritance", R"as3(
interface IA { function a():String; }
interface IB extends IA { function b():String; }
class Impl implements IB { public function a():String { return "a"; } public function b():String { return "b"; } }
class Sub extends Impl {}
class None {}
var s = new Sub();
trace(s is IA, s is IB, s is Impl, new None() is IA, (s as IA).a(), new None() as IB, s instanceof Impl);
var list:Array = [new Impl(), new None(), new Sub()]; var n:int = 0;
for each (var x in list) if (x is IA) n++; trace(n);
)as3"},

    {"Packages: classes, functions, variables and constants", R"as3(
package geo {
    public class Shape { public static var made:int = 0; public function Shape() { made++; } public function area():Number { return 0; } }
    public function describe(s:Shape):String { return "shape " + s.area(); }
    public var unit:String = "cm";
    public const SIDES:int = 4;
}
package geo.solid {
    import geo.Shape;
    public class Box extends Shape { public var side:Number; public function Box(n:Number) { super(); side = n; }
        override public function area():Number { return side * side; } }
}
import geo.*;
import geo.solid.Box;
var b:Box = new Box(3); trace(describe(b), Shape.made, unit, SIDES, b is Shape);
trace(unit, geo.unit, geo.SIDES);
trace(new geo.solid.Box(2).area(), geo.Shape.made);
)as3"},

    {"Packages: classes using each other in any order", R"as3(
package a {
    import b.Second;
    public class First { public static const NAME:String = "first";
        public function other():String { return Second.NAME + " " + new Second().me(); } }
}
package b {
    import a.First;
    public class Second { public static const NAME:String = "second";
        public function me():String { return "me " + First.NAME; } }
}
import a.First;
trace(new First().other());
)as3"},

    {"Packages: two classes of one name", R"as3(
package one { public class Item { public function get name():String { return "one"; } } }
package two { public class Item { public function get name():String { return "two"; } } }
package users { import two.Item; public class User { public function get it():String { return new Item().name + " " + new one.Item().name; } } }
import users.User;
trace(new User().it);
)as3"},

    {"is, as, typeof and instanceof with primitives", R"as3(
trace(5 is int, 5 is uint, -5 is uint, 5.5 is int, 5.5 is Number, "s" is String, true is Boolean, null is Object, undefined is Object);
trace(5 is Number, 4294967296 is uint, int.MAX_VALUE is int, [] is Array, {} is Object, [] is Object);
trace(typeof 5, typeof "s", typeof true, typeof undefined, typeof null, typeof {}, typeof [], typeof function() {}, typeof Math);
trace(5 as int, 5.5 as int, "5" as Number, null as String, [] as Array);
class Animal {} class Dog extends Animal {}
var d:Dog = new Dog(); trace(d is Animal, d instanceof Animal, d as Animal != null, typeof d, typeof Dog, Dog is Class, d is Class);
var f:Function = function():void {}; trace(f is Function, f is Object, Dog is Function);
)as3"},

    {"in and delete", R"as3(
var o:Object = {a: 1, b: undefined}; var arr:Array = [10, 20, 30];
trace("a" in o, "b" in o, "c" in o, 1 in arr, 5 in arr, "length" in arr);
delete o.a; delete arr[1]; trace("a" in o, o.a, arr, arr.length);
var key:String = "b"; delete o[key]; trace(key in o);
)as3"},

    {"for-in and for each over objects and arrays", R"as3(
var o:Object = {x: 1, y: 2, z: 3}; var keys:Array = []; var sum:int = 0;
for (var k:String in o) keys.push(k); for each (var v:int in o) sum += v;
keys.sort(); trace(keys, sum);
var arr:Array = ["a", "b", "c"]; arr.extra = "d"; var seen:String = "";
for (var i:String in arr) seen += i + ","; trace(seen);
var vals:String = ""; for each (var s:String in arr) vals += s; trace(vals);
var sparse:Array = []; sparse[2] = "x"; sparse[5] = "y"; trace(sparse.length);
for (var none in null) trace("never"); for each (var nothing in undefined) trace("never");
)as3"},

    {"Vector: typed elements, fixed length, methods", R"as3(
var v:Vector.<int> = new Vector.<int>(); v.push(1.9, 2.1, "3"); trace(v, v.length);
var n:Vector.<Number> = new <Number>[1.5, 2.5]; trace(n, n.indexOf(2.5), n.join("|"));
var s:Vector.<String> = Vector.<String>(["a", "b"]); s.unshift("z"); trace(s, s.shift(), s, s.pop(), s);
var f:Vector.<int> = new Vector.<int>(3, true); trace(f, f.fixed);
var err:String = "none"; try { f.push(1); } catch (e:RangeError) { err = "range " + e.errorID; } trace(err);
f.fixed = false; f.push(9); trace(f, f.length);
var sorted:Vector.<int> = new <int>[5, 3, 9, 1]; sorted.sort(function(a:int, b:int):Number { return a - b; }); trace(sorted);
var mapped:Vector.<int> = sorted.map(function(x:int, i:int, all:Vector.<int>):int { return x * 2; }); trace(mapped, mapped is Vector.<int>);
var sliced:Vector.<int> = sorted.slice(1, 3); trace(sliced, sorted.concat(sliced), sorted.reverse(), sorted.lastIndexOf(9));
var total:int = 0; for each (var x:int in sorted) total += x; trace(total);
var vv:Vector.<Vector.<int>> = new Vector.<Vector.<int>>(); vv.push(new <int>[1, 2]); trace(vv[0][1], vv.length);
)as3"},

    {"Array: sort options, sortOn, splice and the rest", R"as3(
var a:Array = [10, 9, 100, 1]; trace(a.sort(), a.sort(Array.NUMERIC), a.sort(Array.NUMERIC | Array.DESCENDING));
var w:Array = ["b", "B", "a", "C"]; trace(w.sort(), w.sort(Array.CASEINSENSITIVE));
trace([3, 1, 2].sort(Array.RETURNINDEXEDARRAY), [1, 2, 1].sort(Array.UNIQUESORT), [1, 2, 3].sort(Array.UNIQUESORT));
var people:Array = [{name: "bo", age: 30}, {name: "al", age: 25}, {name: "cy", age: 30}];
people.sortOn("age", Array.NUMERIC); trace(people.map(function(p:Object, i:int, all:Array):String { return p.name; }));
people.sortOn(["age", "name"], [Array.NUMERIC | Array.DESCENDING, 0]); trace(people.map(function(p:Object, i:int, all:Array):String { return p.name; }));
people.sortOn("name", Array.DESCENDING); trace(people[0].name);
var s:Array = [1, 2, 3, 4, 5]; trace(s.splice(1, 2), s, s.splice(1, 0, "x", "y"), s, s.slice(-2), s.indexOf("x"), s.lastIndexOf(9));
trace(s.concat([6, [7]]), s.join("-"), s.reverse(), [].pop(), [1].shift(), [2, 3].unshift(1));
var objs:Array = [1, 2, 3]; trace(objs.every(function(x:int, i:int, all:Array):Boolean { return x > 0; }), objs.some(function(x:int, i:int, all:Array):Boolean { return x > 2; }));
var thisObj:Object = {base: 10}; var sum:Array = []; objs.forEach(function(x:int, i:int, all:Array):void { sum.push(x + this.base); }, thisObj); trace(sum);
trace([1, [2, [3]]].toString(), [null, undefined, 1].join(","), new Array(3).length, new Array(1, 2), Array(2).length);
var compare:Array = ["10", "9", "1"]; compare.sort(function(x:String, y:String):int { return int(x) - int(y); }); trace(compare);
)as3"},

    {"Dictionary: objects as keys", R"as3(
import flash.utils.Dictionary;
var d:Dictionary = new Dictionary(); var k1:Object = {}; var k2:Array = [];
d[k1] = "object"; d[k2] = "array"; d["s"] = "string"; d[1] = "one";
trace(d[k1], d[k2], d["s"], d[1], d["1"], k1 in d, {} in d);
var n:int = 0; for (var k:* in d) n++; trace(n);
var values:Array = []; for each (var v:String in d) values.push(v); values.sort(); trace(values);
delete d[k1]; trace(d[k1], k1 in d);
var weak:Dictionary = new Dictionary(true); weak[d] = 5; trace(weak[d]);
)as3"},

    {"Closures: function scope, loops and recursion", R"as3(
var fs:Array = [];
for (var i:int = 0; i < 3; i++) { fs.push(function():int { return i; }); }
trace(fs[0](), fs[2]());
function counter():Function { var n:int = 0; return function():int { return ++n; }; }
var c1:Function = counter(); var c2:Function = counter(); c1(); c1(); trace(c1(), c2());
function fact(n:int):int { return n <= 1 ? 1 : n * fact(n - 1); } trace(fact(10));
var fib:Function = function f(n:int):int { return n < 2 ? n : f(n - 1) + f(n - 2); }; trace(fib(15));
function outer():String { var x:String = "o"; function inner():String { return x + "i"; } x = "O"; return inner(); } trace(outer());
if (true) { var hoisted:String = "h"; } trace(hoisted);
function early():String { return late(); function late():String { return "late"; } } trace(early());
)as3"},

    {"Functions: arguments, rest and defaults", R"as3(
function f(a:int, b:String = "dflt", ...rest):String { return a + " " + b + " " + rest.join("/"); }
trace(f(1), f(1, "x"), f(1, "x", 2, 3));
function g():int { return arguments.length; } trace(g(), g(1, 2, 3));
function h(x:Number = NaN, y:Boolean = true, z:Object = null):String { return x + " " + y + " " + z; } trace(h(), h(1, false, "z"));
function apply(fn:Function, ...args):* { return fn.apply(null, args); } trace(apply(Math.max, 3, 9, 4), apply(f, 7, "y"));
var call:Function = function(a:int, b:int):int { return a + b; }; trace(call.call(null, 2, 3), call.length);
)as3"},

    {"Statements: switch, labels, do-while", R"as3(
function kind(n:int):String { switch (n) { case 0: return "zero"; case 1: case 2: return "small"; default: return "big"; } }
trace(kind(0), kind(2), kind(9));
var log:String = ""; switch ("b") { case "a": log += "a"; case "b": log += "b"; case "c": log += "c"; break; case "d": log += "d"; } trace(log);
var found:String = "";
outer: for (var i:int = 0; i < 5; i++) { for (var j:int = 0; j < 5; j++) { if (j == 3) continue outer; if (i == 3) break outer; found += i + "" + j + " "; } }
trace(found);
var n:int = 0; do { n++; } while (n < 5); trace(n);
var w:int = 10; while (w > 0) { w -= 3; if (w == 4) break; } trace(w);
block: { if (n > 0) break block; trace("skipped"); } trace("after");
switch (2) { case 1 + 1: trace("expr case"); break; }
)as3"},

    {"Errors: try, catch by type, finally, rethrow", R"as3(
class MyError extends Error { public var extra:String; public function MyError(m:String, x:String) { super(m, 77); extra = x; } }
function risky(n:int):String {
    try {
        if (n == 1) throw new MyError("mine", "x");
        if (n == 2) throw new TypeError("type");
        if (n == 3) throw "a string";
        if (n == 4) { var o:Object = null; o.f(); }
        return "ok";
    } catch (e:MyError) { return "MyError " + e.message + " " + e.errorID + " " + e.extra + " " + (e is Error);
    } catch (e:TypeError) { return "TypeError " + e.message + " " + e.errorID;
    } catch (e:String) { return "String " + e;
    } finally { trace("finally " + n); }
    return "unreached";
}
trace(risky(0)); trace(risky(1)); trace(risky(2)); trace(risky(3)); trace(risky(4));
try { try { throw new Error("inner"); } catch (e:Error) { throw new RangeError("outer " + e.message); } } catch (e:Error) { trace(e, e.name, e is RangeError); }
var e2:Error = new Error(); trace(e2.message == "", e2.toString(), new ArgumentError("a").name, new Error("x", 5).errorID);
try { throw {code: 5}; } catch (x) { trace(x.code); }
)as3"},

    {"Errors: what the player throws", R"as3(
var s:String = "";
try { var o:Object = null; o.x = 1; } catch (e:TypeError) { s += e.errorID + " "; }
try { var u:Object; u.f(); } catch (e:TypeError) { s += e.errorID + " "; }
try { notDefined(); } catch (e:ReferenceError) { s += e.errorID + " "; }
try { var n:Object = 5; n(); } catch (e:TypeError) { s += e.errorID + " "; }
trace(s);
try { var z:Object = null; z.x; } catch (e:Error) { trace(e.message); }
)as3"},

    {"with statement", R"as3(
var o:Object = {a: 1, b: 2}; var c:int = 10;
with (o) { trace(a + b + c); a = 5; c = 20; }
trace(o.a, c, o.c);
with (Math) { trace(max(1, 2), floor(PI)); }
)as3"},

    {"Strings: methods and regular expressions", R"as3(
var s:String = "Hello, World";
trace(s.length, s.toUpperCase(), s.charAt(4), s.charCodeAt(0), s.indexOf("o"), s.lastIndexOf("o"), s.substr(7, 3), s.substring(7), s.slice(-5, -1));
trace(s.split(", "), s.replace("o", "0"), s.replace(/o/g, "0"), s.search(/W/), s.match(/l+/g), s.concat("!", "?"));
var re:RegExp = /(\d+)-(\d+)/; var m:Array = re.exec("call 555-1234 now"); trace(m[0], m[1], m[2], m.index);
trace("a1b2c3".replace(/\d/g, function(d:String, ...rest):String { return "[" + d + "]"; }), "x".localeCompare("y"), String.fromCharCode(65, 66));
trace("  pad  ".length, "abc".indexOf(""), "a,b,,c".split(","), "tEsT".toLowerCase(), "ab" < "b", "abc" == "abc");
var g:RegExp = /a/g; trace(g.test("banana"), g.lastIndex, g.global, g.source, /x/i.ignoreCase);
)as3"},

    {"Math and Number formatting", R"as3(
trace(Math.floor(-1.5), Math.ceil(-1.5), Math.round(-1.5), Math.round(1.5), Math.abs(-2), Math.max(1, 3, 2), Math.min(), Math.sqrt(2));
trace((3.14159).toFixed(2), (1234.5).toPrecision(6), (255).toString(16), (8).toString(2), Number.MAX_VALUE > 1e308, int.MIN_VALUE, uint.MAX_VALUE);
trace(1 / 0, -1 / 0, 0 / 0, isFinite(1 / 0), Math.pow(2, 53) + 1, 0.1 * 3, 100 / 3, 1e100, 5e-324 > 0);
)as3"},

    {"Objects: literals, hasOwnProperty, JSON", R"as3(
var o:Object = {name: "x", "quoted key": 2, 3: "three", nested: {deep: [1, 2]}};
trace(o.name, o["quoted key"], o[3], o.nested.deep[1], o.hasOwnProperty("name"), o.hasOwnProperty("nope"));
trace(JSON.stringify({a: [1, "b", null, true]}), JSON.parse('{"x": {"y": [3]}}').x.y[0]);
var empty:Object = {}; empty.added = 1; trace(empty.added, empty.missing, empty.missing == null, empty.missing === undefined);
)as3"},

    {"E4X: literals, children, attributes, filters", R"as3(
var x:XML = <shop name="corner"><item id="1" price="4"><name>apple</name></item><item id="2" price="9"><name>pear</name></item></shop>;
trace(x.@name, x.item.length(), x.item[0].name, x.item.(@price > 5).name, x..name.length(), x.item.@id);
trace(x.item[1].@price, x.item.(@id == "1").@price, x.name(), x.item[0].name());
var id:int = 2; var tag:String = "tag"; var made:XML = <list><entry n={id}>{tag}</entry></list>;
trace(made.toXMLString());
var total:int = 0; for each (var item:XML in x.item) total += int(item.@price); trace(total);
x.item[0].@price = 5; x.@open = "yes"; trace(x.item[0].@price, x.@open);
x.appendChild(<item id="3" price="1"><name>fig</name></item>); trace(x.item.length(), x.item[2].name);
var list:XMLList = x.item.(@price < 6); trace(list.length(), list[1].name);
delete x.item[0]; trace(x.item.length(), x.item[0].name);
trace(x.item[0].toXMLString());
)as3"},

    {"E4X: text, children, copies and strings", R"as3(
var x:XML = <a><b>one</b><b>two</b><c><d>deep</d></c></a>;
trace(x.b, x.b[0], x.children().length(), x.c.d, x.descendants("d").length(), x.b.text(), x.*.length());
trace(String(x.b[1]), x.c.hasComplexContent(), x.b[0].hasSimpleContent(), x.c.d.parent().name(), x.b[1].childIndex());
var copy:XML = x.copy(); copy.b[0] = "uno"; trace(x.b[0], copy.b[0]);
var parsed:XML = new XML("<r a='1'/>"); trace(parsed.@a, parsed.toXMLString(), XML("<s>t</s>"), typeof parsed);
var l:XMLList = new XMLList("<i>1</i><i>2</i>"); trace(l.length(), l[1], l.toXMLString());
)as3"},

    {"getQualifiedClassName and getDefinitionByName", R"as3(
package app.model { public class User {} }
import app.model.User;
import flash.utils.getQualifiedClassName;
import flash.utils.getQualifiedSuperclassName;
import flash.utils.getDefinitionByName;
class Local {}
trace(getQualifiedClassName(new User()), getQualifiedClassName(User), getQualifiedClassName(new Local()), getQualifiedClassName(1.5), getQualifiedClassName([]));
var cls:Class = getDefinitionByName("app.model.User") as Class; trace(cls == User, new cls() is User, getDefinitionByName("app.model::User") == User);
trace(getQualifiedSuperclassName(new User()), getQualifiedClassName(new Vector.<String>()), getQualifiedClassName(int));
)as3"},

    {"Number output: integers and fractions", R"as3(
var values:Array = [0, 1, -1, 0.5, 1 / 3, 2 / 3, 100, 1e15, 1e16, 123456.789, 0.000001, 1.5e-7, 9007199254740993, -0.0001];
trace(values.join(" "));
)as3"},

    {"Static initializers run once, before use", R"as3(
class Config { public static var log:Array = []; public static var ready:Boolean = init();
  private static function init():Boolean { log.push("init"); return true; } }
trace(Config.ready, Config.log, Config.log.length);
)as3"},

    {"Getters and setters on statics", R"as3(
class Settings { private static var _volume:Number = 0.5;
  public static function get volume():Number { return _volume; }
  public static function set volume(v:Number):void { _volume = Math.max(0, Math.min(1, v)); } }
Settings.volume = 2; trace(Settings.volume); Settings.volume -= 0.25; trace(Settings.volume);
)as3"},

    {"Super calls in methods and accessors", R"as3(
class A { public function hello(n:int):String { return "A" + n; } public function get name():String { return "a"; } }
class B extends A { override public function hello(n:int):String { return "B" + super.hello(n + 1); }
  override public function get name():String { return super.name + "b"; } }
class C extends B { override public function hello(n:int):String { return "C" + super.hello(n * 2); } }
trace(new C().hello(1), new C().name);
)as3"},

    {"Compound assignments on members and elements", R"as3(
class Box { public var n:int = 1; public var s:String = "a"; public var list:Array = [1, 2]; public var u:uint = 1; }
var b:Box = new Box(); b.n += 2.5; b.s += "b"; b.list[0] += 10; b.n *= 3; b.u -= 2; b.n %= 5; trace(b.n, b.s, b.list, b.u);
b.n++; ++b.n; b.n--; trace(b.n, b.n++, b.n, --b.n);
var arr:Array = [1, 2, 3]; var i:int = 0; arr[i++] += 5; trace(arr, i);
var o:Object = {count: 0}; o.count++; o["count"] += 2; trace(o.count);
var x:Number = 10; x -= 0.5; x *= 2; trace(x);
)as3"},

    {"Bitwise operations", R"as3(
var a:int = 0x0F0F; var b:int = 0x00FF;
trace(a & b, a | b, a ^ b, ~a, a << 4, a >> 2, -a >>> 28, 1 << 32, 1 << 33, -1 >>> 0, 5 & -1);
var color:uint = 0xFF336699; trace((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, color >>> 24, color >> 24);
var mixed:uint = (0x80 << 24) | 0x123456; trace(mixed, mixed.toString(16));
)as3"},

    {"Conditional, logical and comma operators", R"as3(
var a:int = 0; var b:String = ""; var c:Object = null;
trace(a || "default", b || "empty", c || "none", a && "never", 1 && "yes", null ?? "nc", 0 ?? "zero");
var n:int = 5; var r:String = n > 3 ? n > 4 ? "big" : "mid" : "small"; trace(r);
var x:int = (1, 2, 3); trace(x);
var i:int = 0, j:int = 10; for (; i < j; i++, j--) {} trace(i, j);
)as3"},

    {"Arrays of arrays and object keys", R"as3(
var grid:Array = []; for (var r:int = 0; r < 3; r++) { grid[r] = []; for (var c:int = 0; c < 3; c++) grid[r][c] = r * 3 + c; }
trace(grid[2][1], grid.length, grid[1]);
var o:Object = {}; o[1] = "a"; o["1"] = "b"; o[1.5] = "c"; trace(o[1], o["1.5"]);
var keys:Array = []; for (var k:String in o) keys.push(k); keys.sort(); trace(keys);
)as3"},

    {"Nested functions and this", R"as3(
class Holder { public var value:int = 7;
  public function make():Function { return function():int { return value * 2; }; }
  public function each(list:Array):int { var total:int = 0; list.forEach(function(x:int, i:int, all:Array):void { total += x + value; }); return total; } }
var h:Holder = new Holder(); trace(h.make()(), h.each([1, 2]));
)as3"},
};
