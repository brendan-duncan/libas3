#include "test_runner.h"
#include "../src/parser/parser.h"
#include "../src/bytecode/compiler.h"
#include "../src/bytecode/vm.h"
#include <stdexcept>

using namespace as3;
using namespace as3test;

// The built-in library, as ActionScript 3 has it.
TEST_SUITE(Library)

namespace {

std::string run(VM& vm, const std::string& code) {
    std::string out;
    vm.defineFunction("trace", [&out](VM& vm, const Val&, Args args) {
        for (size_t i = 0; i < args.size(); ++i) {
            out += (i ? " " : "") + vm.toString(args[i]);
        }
        out += "\n";
        return Val();
    });
    if (!vm.load(code, "library.as")) {
        throw std::runtime_error("Error: " + vm.getError() + " (line " + std::to_string(vm.getErrorLine()) + ")\n" +
                                 vm.getErrorStack() + "\nOutput so far:\n" + out);
    }
    return out;
}

std::string run(const std::string& code) {
    VM vm;
    return run(vm, code);
}

void expect(const std::string& got, const std::string& wanted) {
    if (got != wanted) {
        throw std::runtime_error("got \"" + got + "\", wanted \"" + wanted + "\"");
    }
}

} // namespace

static void init_library_tests() {
    auto& suite = suite_Library;

    suite.addTest("Object", []() {
        expect(run("var o = {a: 1}; class K { public var f:int; }\n"
                   "trace(o.hasOwnProperty('a'), o.hasOwnProperty('b'), o.propertyIsEnumerable('a'), o.toString(), o.valueOf() == o, o.constructor == Object);\n"
                   "trace(new K().hasOwnProperty('f'), new K().toString(), [1].constructor == Array, (5).constructor == Number, {toString: function() { return 'mine'; }});"),
               "true false true [object Object] true true\ntrue [object K] true true mine\n");
    });

    suite.addTest("Array: length, push, pop, shift, unshift, splice", []() {
        expect(run("var a = [1, 2, 3]; a.length = 5; trace(a.length, a);\n"
                   "a.length = 2; trace(a.push(7, 8), a.pop(), a.shift(), a.unshift(0, 1), a);\n"
                   "var b = [1, 2, 3, 4, 5]; var r = b.splice(1, 2, 'a', 'b', 'c'); trace(b, r, b.splice(-2), b);\n"
                   "trace(new Array(3).length, new Array(1, 2).length, Array(4).length, [].pop());"),
               "5 1,2,3,,\n4 8 1 4 0,1,2,7\n1,a,b,c 2,3 4,5 1,a,b,c\n3 2 4 undefined\n");
    });

    suite.addTest("Array: slice, concat, join, reverse, indexOf", []() {
        expect(run("var a = [1, 2, 3, 4]; trace(a.slice(1, 3), a.slice(-2), a.concat([5], 6, [[7]]).length, a.join('-'), a.reverse(), a);\n"
                   "var b = [1, 2, 1, '1']; trace(b.indexOf(1), b.lastIndexOf(1), b.indexOf('1'), b.indexOf(5), b.indexOf(1, 1));"),
               "2,3 3,4 7 1-2-3-4 4,3,2,1 4,3,2,1\n0 2 3 -1 2\n");
    });

    suite.addTest("Array: sort and sortOn", []() {
        expect(run("trace([10, 9, 1, 2].sort(), [10, 9, 1, 2].sort(Array.NUMERIC), [10, 9, 1, 2].sort(Array.NUMERIC | Array.DESCENDING));\n"
                   "trace(['b', 'A', 'c'].sort(), ['b', 'A', 'c'].sort(Array.CASEINSENSITIVE), [3, 1, 2].sort(Array.NUMERIC | Array.RETURNINDEXEDARRAY), [1, 1].sort(Array.UNIQUESORT));\n"
                   "var people = [{n: 'b', v: 2}, {n: 'a', v: 10}, {n: 'c', v: 1}];\n"
                   "people.sortOn('n'); var s = ''; for each (var p in people) s += p.n;\n"
                   "people.sortOn('v', Array.NUMERIC | Array.DESCENDING); for each (p in people) s += p.v;\n"
                   "trace(s, [3, undefined, 1].sort(), [5, 1, 4].sort(function(x, y) { return x - y; }));"),
               "1,10,2,9 1,2,9,10 10,9,2,1\nA,b,c A,b,c 1,2,0 0\nabc1021 1,3, 1,4,5\n");
    });

    suite.addTest("Array: forEach, map, filter, some, every", []() {
        expect(run("var a = [1, 2, 3]; var t = 0; a.forEach(function(v, i, arr) { t += v * i; });\n"
                   "trace(t, a.map(function(v) { return v * 2; }), a.filter(function(v) { return v != 2; }), a.some(function(v) { return v > 2; }), a.every(function(v) { return v > 2; }));"),
               "8 2,4,6 1,3 true false\n");
    });

    suite.addTest("Vector", []() {
        expect(run("var v:Vector.<int> = new Vector.<int>(3); v[0] = 2.7; v.push(4.2); trace(v, v.length, v is Vector.<int>, v is Vector.<Number>);\n"
                   "var n = new Vector.<Number>(2); var s = Vector.<String>(['a', 1]); trace(n, s, s.join('+'));\n"
                   "var f = new Vector.<int>(2, true); var e1 = ''; try { f.push(1); } catch (e:RangeError) { e1 = e.errorID; }\n"
                   "var e2 = ''; try { v[9] = 1; } catch (e:RangeError) { e2 = e.errorID; }\n"
                   "var w = v.slice(1); w.push(1.5); trace(e1, e2, w, w is Vector.<int>, v.indexOf(4), v.map(function(x) { return x * 1.5; }));"),
               "2,0,0,4 4 true false\n0,0 a,1 a+1\n1126 1125 0,0,4,1 true 3 3,0,0,6\n");
    });

    suite.addTest("String", []() {
        expect(run("var s = 'hello';\n"
                   "trace(s.length, s.charAt(1), s.charCodeAt(1), String.fromCharCode(72, 105), s.indexOf('l'), s.lastIndexOf('l'), s.indexOf('z'));\n"
                   "trace(s.substr(1, 3), s.substring(3, 1), s.slice(-3), s.slice(1, -1), s.substr(-2), s.toUpperCase(), 'ABC'.toLowerCase());\n"
                   "trace('a,b,,c'.split(',').length, 'abc'.split(''), 'a b c'.split(' ', 2), s.concat(' ', 'world'), 'a'.localeCompare('b'));\n"
                   "trace(String(null), String(undefined), String(5), String([1, 2]), s.charAt(10) == '', isNaN(s.charCodeAt(10)));"),
               "5 e 101 Hi 2 3 -1\nell el llo ell lo HELLO abc\n4 a,b,c a,b hello world -1\nnull undefined 5 1,2 true true\n");
    });

    suite.addTest("String: replace, match, search, split with regular expressions", []() {
        expect(run("var s = 'hello';\n"
                   "trace(s.replace('l', 'L'), s.replace(/l/g, 'L'), s.replace(/(h)(e)/, '$2$1'), s.replace(/l/g, function(m, i) { return i; }), s.replace(/x/, 'y'));\n"
                   "trace(s.match(/l+/), s.match(/l/g).length, s.match(/z/g), s.search(/o/), 'a1b22c333'.split(/\\d+/), 'a1b2'.replace(/(\\d)/g, '[$&]'));"),
               "heLlo heLLo ehllo he23o hello\nll 2 null 4 a,b,c, a[1]b[2]\n");
    });

    suite.addTest("Strings count UTF-16 units", []() {
        expect(run("var s = 'h\\u00e9llo \\u65e5\\u672c'; trace(s.length, s.charAt(1), s.charCodeAt(6), s.substr(6), s.indexOf('l'), s.toUpperCase());"),
               "8 \xC3\xA9 26085 \xE6\x97\xA5\xE6\x9C\xAC 2 H\xC3\x89LLO \xE6\x97\xA5\xE6\x9C\xAC\n");
    });

    suite.addTest("Number, int, uint and conversions", []() {
        expect(run("trace((1.005).toFixed(2), (0.05).toFixed(1), (123.456).toPrecision(4), (0.000123).toPrecision(2), (123456).toPrecision(2), (-1.5).toFixed(0), (255).toString(2), (0.5).toString(2));\n"
                   "trace(Number('0x1A'), Number(''), Number('12px'), Number(' 3 '), Number(true), Number(null), Number(undefined), Number([5]));\n"
                   "trace(parseInt('12px'), parseInt('ff', 16), parseInt('0x1F'), parseInt('-7.9'), parseFloat('3.5e2abc'), parseFloat('.5'), isNaN(NaN), isFinite(Infinity), isFinite(1));\n"
                   "trace(int.MAX_VALUE, int.MIN_VALUE, uint.MAX_VALUE, Number.POSITIVE_INFINITY, -Number.NEGATIVE_INFINITY > 0, Number.MIN_VALUE > 0, int(2147483648), uint(4294967297), Boolean(''), Boolean('0'));"),
               "1.00 0.1 123.5 0.00012 1.2e+5 -2 11111111 0.1\n"
               "26 0 NaN 3 1 0 NaN 5\n"
               "12 255 31 -7 350 0.5 true false true\n"
               "2147483647 -2147483648 4294967295 Infinity true true -2147483648 1 false true\n");
    });

    suite.addTest("Math", []() {
        expect(run("trace(Math.max(), Math.min(), Math.max(1, 5, 3), Math.min(1, 2, 0.5), Math.max(1, NaN), Math.round(-2.5), Math.round(2.5), Math.round(-2.6), Math.floor(-1.5), Math.ceil(1.1));\n"
                   "trace(Math.pow(2, 10), Math.sqrt(16), Math.abs(-3), Math.PI, Math.E, Math.SQRT2, Math.LN2, Math.LOG10E, Math.atan2(1, 1) * 4 == Math.PI);\n"
                   "var r = Math.random(); trace(r >= 0 && r < 1, Math.sin(0), Math.cos(0), Math.exp(0), Math.log(1), Math.tan(0), Math.asin(0), Math.acos(1), Math.atan(0));"),
               "-Infinity Infinity 5 0.5 NaN -2 3 -3 -2 2\n"
               "1024 4 3 3.141592653589793 2.718281828459045 1.4142135623730951 0.6931471805599453 0.4342944819032518 true\n"
               "true 0 1 1 0 0 0 0 0\n");
    });

    suite.addTest("Date", []() {
        expect(run("var d = new Date(2026, 0, 31); trace(d.getFullYear(), d.getMonth(), d.getDate(), d.day, d.fullYear);\n"
                   "d.setMonth(1); trace(d.getMonth(), d.getDate());\n"
                   "d.date = 10; d.hours = 13; d.minutes = 5; trace(d.month, d.date, d.hours, d.getMinutes());\n"
                   "trace(Date.UTC(2000, 0, 1), new Date(0).getUTCFullYear(), new Date(946684800000).toUTCString(), new Date(Date.UTC(2000, 1, 29, 12)).getUTCDate());\n"
                   "var p = new Date(Date.parse('02/03/2004')); trace(p.fullYear, p.month, p.date);\n"
                   "var c = new Date(d.time); c.setTime(c.getTime() + 86400000); trace(c.date, new Date(2000, 0, 1).day, d.valueOf() == d.time, d < c, isNaN(new Date('nonsense').getTime()));"),
               "2026 0 31 6 2026\n2 3\n2 10 13 5\n946684800000 1970 Sat Jan 1 00:00:00 2000 UTC 29\n2004 1 3\n11 6 true true true\n");
    });

    suite.addTest("RegExp", []() {
        expect(run("var r = /(\\w+)@(\\w+)\\.com/; var m = r.exec('mail me@site.com now');\n"
                   "trace(m[0], m[1], m[2], m.index, r.source, r.global, r.test('x@y.com'), r.test('nope'));\n"
                   "var g = /o/g; trace(g.exec('foo boo').index, g.lastIndex, g.exec('foo boo').index, g.lastIndex);\n"
                   "var n = /(?P<word>[a-z]+)-(\\d+)/.exec('abc-12'); trace(n.word, n[2]);\n"
                   "trace(/a.b/s.test('a\\nb'), /a.b/.test('a\\nb'), /a b c/x.test('abc'), /^b/m.test('a\\nb'), /^b/.test('a\\nb'), new RegExp('A', 'i').test('a'), /x/gi);"),
               "me@site.com me site 5 (\\w+)@(\\w+)\\.com false true false\n1 2 2 3\nabc 12\ntrue false true true false true /x/gi\n");
    });

    suite.addTest("JSON", []() {
        expect(run("trace(JSON.stringify({a: 1, b: [true, null, 'x\"y'], c: undefined, f: function() {}}));\n"
                   "var o = JSON.parse('{\"x\": [1, 2.5, {\"y\": \"z\\\\u0041\"}], \"t\": true}'); trace(o.x[2].y, o.x.length, o.t, o.x[1]);\n"
                   "class P { public var name:String = 'p'; public var n:int = 2; }\n"
                   "trace(JSON.stringify(new P()), JSON.stringify([1, [2]], null, 1).split('\\n').length, JSON.parse('[1,2]', function(k, v) { return v is Number ? v * 10 : v; }));\n"
                   "var err = ''; try { JSON.parse('{bad'); } catch (e:SyntaxError) { err = e.errorID; } trace(err, JSON.stringify('a\\nb'), JSON.stringify(NaN));"),
               "{\"a\":1,\"b\":[true,null,\"x\\\"y\"]}\nzA 3 true 2.5\n{\"n\":2,\"name\":\"p\"} 6 10,20\n1132 \"a\\nb\" null\n");
    });

    suite.addTest("Dictionary", []() {
        expect(run("import flash.utils.Dictionary;\n"
                   "var d = new Dictionary(true); var k1 = {}; var k2 = {}; d[k1] = 'one'; d[k2] = 'two'; d['s'] = 3; d.t = 4;\n"
                   "var n = 0; for (var k in d) n++; var vals = 0; for each (var v in d) vals++;\n"
                   "delete d[k1]; trace(d[k2], d[k1], d.s, d['t'], n, vals, k2 in d, k1 in d);"),
               "two undefined 3 4 4 4 true false\n");
    });

    suite.addTest("Error classes", []() {
        expect(run("class MyError extends Error { public function MyError(m:String) { super(m, 42); name = 'MyError'; } }\n"
                   "function thrower() { throw new MyError('custom'); }\n"
                   "try { thrower(); } catch (e:Error) { trace(e, e.name, e.message, e.errorID, e is MyError, e.getStackTrace().indexOf('thrower') > 0); }\n"
                   "var e2 = new Error('plain'); trace(e2, e2.errorID, new TypeError('t').name, new Error().toString());\n"
                   "import flash.errors.IOError; trace(new IOError('io') is Error);"),
               "MyError: custom MyError custom 42 true true\nError: plain 0 TypeError Error\ntrue\n");
    });

    suite.addTest("Global functions", []() {
        expect(run("trace(escape('a b+c/\\u00fc!'), unescape('%41%u0042c'), encodeURIComponent('a b&c/'), encodeURI('http://x.com/a b?q=1&r=#f'), decodeURIComponent('a%20b%26'), decodeURI('%3Fa%20b'));"),
               "a%20b+c/%FC%21 ABc a%20b%26c%2F http://x.com/a%20b?q=1&r=#f a b& %3Fa b\n");
    });

    suite.addTest("flash.utils: class names and definitions", []() {
        expect(run("import flash.utils.*;\n"
                   "class Mine {}\n"
                   "trace(getQualifiedClassName(5), getQualifiedClassName(5.5), getQualifiedClassName('s'), getQualifiedClassName(null), getQualifiedClassName(undefined));\n"
                   "trace(getQualifiedClassName([]), getQualifiedClassName(Array), getQualifiedClassName(new Vector.<int>()), getQualifiedClassName(new Dictionary()), getQualifiedClassName(new Mine()));\n"
                   "trace(getQualifiedSuperclassName(new Mine()), getQualifiedSuperclassName(Object), getDefinitionByName('flash.utils::Dictionary') == Dictionary, getDefinitionByName('Math') == Math);\n"
                   "var e = ''; try { getDefinitionByName('no.Such'); } catch (x:ReferenceError) { e = x.errorID; } trace(e);"),
               "int Number String null void\nArray Array __AS3__.vec::Vector.<int> flash.utils::Dictionary Mine\nObject null true true\n1065\n");
    });

    suite.addTest("Timers run when the host says", []() {
        VM vm;
        double now = 0;
        vm.setClock([&now]() { return now; });
        std::string out = run(vm,
                              "import flash.utils.*;\n"
                              "var log = '';\n"
                              "setTimeout(function(a, b) { log += 'T' + a + b; }, 100, 1, 2);\n"
                              "var id = setInterval(function() { log += 'I'; if (log.length > 6) clearInterval(id); }, 30);\n"
                              "var cancelled = setTimeout(function() { log += 'X'; }, 50); clearTimeout(cancelled);\n"
                              "trace(getTimer());");
        now = 95;
        vm.runTimers(now);
        now = 200;
        vm.runTimers(now);
        expect(out, "0\n");
        expect(Val(vm.getGlobal("log")).toString(), "IIIT12I");
    });
}

static struct LibraryTestInit {
    LibraryTestInit() { init_library_tests(); }
} library_test_init;
