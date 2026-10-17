// avm1.js: what ActionScript 1 and 2 translated to JavaScript needs beyond
// JavaScript itself, as the first ActionScript virtual machine (AVM1) has
// it: its conversions (undefined, null and numbers in strings, numbers
// from strings), its operators, the scope chain names are looked up along
// (with blocks, the timeline the code is on, _global), target paths
// ("/a/b:score", "_root.a.b", "../a"), prototype classes, for-in's order,
// and the global functions (trace, getTimer, setInterval, escape...).
//
// A host (a player's runtime) gives movie clips their ActionScript 2 face
// and plugs in with avm1.host; translated movies call avm1.movie(module).
// The libas3 README ("ActionScript 1 and 2") describes the interface.
var avm1 = (function () {
    "use strict";

    const $ = {};
    // The SWF version the movie is: 7 and later are case sensitive, and
    // give undefined as "undefined" and NaN; earlier ones as "" and 0.
    $.version = 8;
    // Where trace writes; the host sets it.
    $.output = (text) => console.log(text);
    // The milliseconds since the movie started, for getTimer.
    const started = Date.now();
    $.clock = () => Date.now() - started;

    // ---- Conversions

    const isClip = (v) => v != null && typeof v === "object" && v.$clip === true;

    // A number as AVM1 writes it: 15 significant digits, %g's exponents.
    $.numberText = function (d) {
        if (Number.isNaN(d)) return "NaN";
        if (d === Infinity) return "Infinity";
        if (d === -Infinity) return "-Infinity";
        if (d === 0) return "0";
        if (Number.isInteger(d) && Math.abs(d) < 1e15) return String(d);
        let text = d.toExponential(14);
        let at = text.indexOf("e");
        let mantissa = text.slice(0, at);
        const exponent = parseInt(text.slice(at + 1), 10);
        if (exponent < -4 || exponent >= 15) {
            if (mantissa.indexOf(".") >= 0) mantissa = mantissa.replace(/0+$/, "").replace(/\.$/, "");
            const digits = String(Math.abs(exponent));
            return mantissa + "e" + (exponent < 0 ? "-" : "+") + (digits.length < 2 ? "0" + digits : digits);
        }
        text = d.toFixed(Math.max(0, 14 - exponent));
        if (text.indexOf(".") >= 0) text = text.replace(/0+$/, "").replace(/\.$/, "");
        return text;
    };

    // ToPrimitive, as AVM1 asks objects: valueOf, then toString.
    function primitive(v, hint) {
        if (v === null || typeof v !== "object" && typeof v !== "function") return v;
        if (v instanceof Date && hint !== "number") return $.s(v);
        if (typeof v === "function") return hint === "number" ? NaN : "[type Function]";
        const valueOf = v.valueOf;
        if (typeof valueOf === "function") {
            const value = valueOf.call(v);
            if (value === null || typeof value !== "object" && typeof value !== "function") return value;
        }
        return $.s(v);
    }

    // ToNumber.
    $.n = function (v) {
        switch (typeof v) {
            case "number": return v;
            case "boolean": return v ? 1 : 0;
            case "undefined": return $.version < 7 ? 0 : NaN;
            case "string": return numberOf(v);
            default:
                if (v === null) return $.version < 7 ? 0 : NaN;
                return $.n(primitive(v, "number"));
        }
    };

    function numberOf(s) {
        const text = s.replace(/^[\s ]+/, "");
        if (text === "") return NaN;
        if (/^[-+]?0x[0-9a-f]+$/i.test(text)) {
            const negative = text[0] === "-";
            const n = parseInt(text.replace(/^[-+]?0x/i, ""), 16);
            return negative ? -n : n;
        }
        if (!/^[-+]?(\d+\.?\d*|\.\d+)(e[-+]?\d+)?$/i.test(text) && !/^[-+]?Infinity$/.test(text)) return NaN;
        return Number(text);
    }

    // ToString.
    $.s = function (v) {
        switch (typeof v) {
            case "string": return v;
            case "number": return $.numberText(v);
            case "boolean": return v ? "true" : "false";
            case "undefined": return $.version < 7 ? "" : "undefined";
            case "function": return "[type Function]";
            default: {
                if (v === null) return "null";
                if (Array.isArray(v) && v.toString === Array.prototype.toString) return $.join(v, ",");
                if (v instanceof Error && v.toString === Error.prototype.toString) return $.s(v.message);
                if (v instanceof Date && v.toString === Date.prototype.toString) return dateText(v);
                if (v instanceof Number || v instanceof Boolean || v instanceof String) return $.s(v.valueOf());
                const toString = v.toString;
                if (typeof toString === "function" && toString !== Object.prototype.toString) {
                    const text = toString.call(v);
                    return typeof text === "string" ? text : $.s(text);
                }
                if (isClip(v) && $.host.targetPath) return $.host.targetPath(v);
                return "[object Object]";
            }
        }
    };

    // ToBoolean: from SWF 7 a string is true when not empty; before, when
    // it is a number other than 0.
    $.b = function (v) {
        if (typeof v === "string") return $.version < 7 ? !Number.isNaN(numberOf(v)) && numberOf(v) !== 0 : v !== "";
        return !!v;
    };

    function dateText(d) {
        const days = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
        const months = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
        const two = (n) => (n < 10 ? "0" : "") + n;
        const offset = -d.getTimezoneOffset();
        const zone = "GMT" + (offset < 0 ? "-" : "+") + two(Math.floor(Math.abs(offset) / 60)) + two(Math.abs(offset) % 60);
        return days[d.getDay()] + " " + months[d.getMonth()] + " " + d.getDate() + " " + two(d.getHours()) + ":" +
            two(d.getMinutes()) + ":" + two(d.getSeconds()) + " " + zone + " " + d.getFullYear();
    }

    // An array's elements joined as AVM1 joins them.
    $.join = function (a, separator) {
        const sep = separator === undefined ? "," : $.s(separator);
        let out = "";
        for (let i = 0; i < a.length; i++) {
            if (i) out += sep;
            const item = a[i];
            out += item === undefined ? ($.version < 7 ? "" : "undefined") : item === null ? "null" : $.s(item);
        }
        return out;
    };

    // ---- Operators

    // a + b: strings when either is one, numbers otherwise.
    $.add = function (a, b) {
        if (typeof a === "number" && typeof b === "number") return a + b;
        const x = primitive(a), y = primitive(b);
        if (typeof x === "string" || typeof y === "string") return $.s(x) + $.s(y);
        return $.n(x) + $.n(y);
    };

    // a == b.
    $.eq = function (a, b) {
        if (a === b) return typeof a !== "number" || !Number.isNaN(a);
        const an = a === undefined || a === null, bn = b === undefined || b === null;
        if (an || bn) return an && bn;
        const ta = typeof a, tb = typeof b;
        const ao = ta === "object" || ta === "function", bo = tb === "object" || tb === "function";
        if (ao && bo) return false;
        if (ao) return $.eq(primitive(a), b);
        if (bo) return $.eq(a, primitive(b));
        if (ta === tb) return a === b;
        return $.n(a) === $.n(b);
    };

    // a < b, with both strings compared as strings.
    $.lt = function (a, b) {
        const x = primitive(a, "number"), y = primitive(b, "number");
        if (typeof x === "string" && typeof y === "string") return x < y;
        const m = $.n(x), n = $.n(y);
        if (Number.isNaN(m) || Number.isNaN(n)) return $.version < 5 ? false : undefined;
        return m < n;
    };
    $.gt = (a, b) => $.lt(b, a);
    $.le = function (a, b) {
        const r = $.lt(b, a);
        return r === undefined ? false : !r;
    };
    $.ge = function (a, b) {
        const r = $.lt(a, b);
        return r === undefined ? false : !r;
    };

    $.typeOf = function (v) {
        if (v === null) return "null";
        if (isClip(v)) return v.$button ? "object" : "movieclip";
        if (typeof v === "symbol" || typeof v === "bigint") return "object";
        return typeof v;
    };

    // x instanceof C, with the interfaces a class implements.
    $.instanceOf = function (x, C) {
        if (typeof C !== "function" || x === null || x === undefined) return false;
        if (typeof x !== "object" && typeof x !== "function") return false;
        const target = C.prototype;
        for (let p = Object.getPrototypeOf(x), guard = 0; p && guard < 256; p = Object.getPrototypeOf(p), guard++) {
            if (p === target) return true;
            const made = Object.prototype.hasOwnProperty.call(p, "constructor") ? p.constructor : null;
            if (made && implementsOf(made, C)) return true;
        }
        return false;
    };

    function implementsOf(cls, iface, depth = 0) {
        const list = cls && cls.$implements;
        if (!list || depth > 32) return false;
        for (const i of list) {
            if (i === iface || implementsOf(i, iface, depth + 1)) return true;
        }
        return false;
    }

    // ---- Members

    // A member's key: a name kept as one spelling where case does not
    // count (before SWF 7).
    $.names = Object.create(null);
    $.fold = function (name) {
        if ($.version >= 7) return name;
        const known = $.names[String(name).toLowerCase()];
        return known === undefined ? name : known;
    };
    $.key = function (k) {
        if (typeof k === "number") return Number.isInteger(k) ? k : $.numberText(k);
        if (typeof k !== "string") k = $.s(k);
        return $.version >= 7 ? k : $.fold(k);
    };

    // o.k = v where o may be no object at all.
    $.put = function (o, k, v) {
        if (o !== null && o !== undefined && (typeof o === "object" || typeof o === "function")) o[k] = v;
        return v;
    };

    // o.k = f(o.k), for op= and ++ on members; the new value.
    $.update = function (o, k, f) {
        const ok = o !== null && o !== undefined && (typeof o === "object" || typeof o === "function");
        const v = f(ok ? o[k] : undefined);
        if (ok) o[k] = v;
        return v;
    };

    // Whether an object has a name, its prototypes' too.
    $.has = function (o, k) {
        if (o === null || o === undefined) return false;
        if (typeof o !== "object" && typeof o !== "function") return false;
        return k in o;
    };

    // delete o[k]; false where there is nothing to delete.
    $.del = function (o, k) {
        if (o === null || o === undefined || typeof o !== "object" && typeof o !== "function") return false;
        if (!Object.prototype.hasOwnProperty.call(o, k)) return false;
        try {
            return delete o[k];
        } catch (e) {
            return false;
        }
    };

    // The names for (k in o) gives: the latest made first, as AVM1 has
    // them, then those of its prototypes; not those hidden.
    $.keys = function (o) {
        if (o === null || o === undefined || typeof o !== "object" && typeof o !== "function") return [];
        const out = [], seen = new Set();
        for (let p = o, guard = 0; p && p !== Object.prototype && p !== Function.prototype && guard < 256; p = Object.getPrototypeOf(p), guard++) {
            const own = Object.keys(p);
            for (let i = own.length - 1; i >= 0; i--) {
                const k = own[i];
                if (!seen.has(k)) {
                    seen.add(k);
                    out.push(k);
                }
            }
            if (Array.isArray(p) || typeof p === "function") break;
        }
        return out;
    };

    // new C(...args), C being anything.
    $.make = function (C, args) {
        if (typeof C !== "function") return undefined;
        return Reflect.construct(C, args);
    };

    // o.f(...args) where f may not be a function: undefined then.
    $.callMethod = function (o, k, args) {
        if (o === null || o === undefined) return undefined;
        const f = o[k];
        return typeof f === "function" ? f.apply(o, args) : undefined;
    };

    // f(...args), f a value.
    $.callValue = function (f, self, args) {
        return typeof f === "function" ? f.apply(self, args) : undefined;
    };

    // ---- The scope chain

    // _global: the classes and the global functions, and what scripts
    // put there.
    const global = Object.create(null);
    $.global = global;

    // What names are looked up along: with blocks' objects (innermost
    // first), the timeline the code is on (its target), then _global.
    // Locals are JavaScript's own and never come here.
    class Scope {
        constructor(target, withs, self) {
            this.target = target;
            this.withs = withs || [];
            this.self = self;
        }
        // with (o) { ... }
        with(o) {
            return new Scope(this.target, (o !== null && o !== undefined && (typeof o === "object" || typeof o === "function")) ? [o].concat(this.withs) : this.withs, this.self);
        }
        // tellTarget (t) { ... }: another timeline as the target.
        tell(t) {
            const clip = this.resolve(t);
            return new Scope(clip || this.target, this.withs, this.self);
        }
        // Names that are not variables: _root, _global, _levelN.
        special(name) {
            switch (name) {
                case "_global": return global;
                case "_root": return isClip(this.target) ? $.host.root(this.target) : $.host.level(0);
                case "this": return this.self;
                default:
                    if (/^_level\d+$/.test(name)) return $.host.level(parseInt(name.slice(6), 10));
                    return undefined;
            }
        }
        holder(name) {
            for (const o of this.withs) {
                if (name in o) return o;
            }
            const t = this.target;
            if (t && name in t) return t;
            if (name in global) return global;
            return null;
        }
        get(name) {
            name = $.fold(name);
            if (name.charCodeAt(0) === 95 && (name === "_global" || name === "_root" || name.startsWith("_level"))) {
                const value = this.special(name);
                if (value !== undefined || name !== "_root") return value;
            }
            if (name === "this") return this.self;
            const o = this.holder(name);
            return o ? o[name] : undefined;
        }
        set(name, value) {
            name = $.fold(name);
            for (const o of this.withs) {
                if (name in o) {
                    o[name] = value;
                    return value;
                }
            }
            if (this.target) this.target[name] = value;
            return value;
        }
        // A local inside with blocks: their objects' first.
        getLocal(name, local) {
            for (const o of this.withs) {
                if (name in o) return o[name];
            }
            return local();
        }
        setLocal(name, value, local) {
            for (const o of this.withs) {
                if (name in o) {
                    o[name] = value;
                    return value;
                }
            }
            local(value);
            return value;
        }
        // var name = value, on the timeline.
        def(name, value) {
            name = $.fold(name);
            if (this.target) this.target[name] = value;
            return value;
        }
        // delete name.
        del(name) {
            name = $.fold(name);
            const o = this.holder(name);
            return o ? $.del(o, name) : false;
        }
        // name(...args): called on the object it was found on.
        call(name, args) {
            name = $.fold(name);
            const o = this.holder(name);
            const f = o ? o[name] : undefined;
            if (typeof f !== "function") return undefined;
            return f.apply(o === global ? this.target : o, args);
        }
        // A target given as a clip, a path or a name: the clip, or null.
        resolve(t) {
            if (isClip(t)) return t;
            if (t === undefined || t === null) return null;
            if (typeof t === "object") return t;
            const text = $.s(t);
            if (text === "") return this.target;
            const found = this.path(text);
            return found && typeof found === "object" ? found : null;
        }
        // A path's object: "/a/b", "../a", "_root.a.b", "_parent", "a.b".
        path(text) {
            text = String(text);
            let at = this.target;
            let parts;
            if (text.indexOf("/") >= 0 || text.startsWith("..")) {
                if (text[0] === "/") {
                    at = $.host.root(this.target);
                    text = text.slice(1);
                }
                parts = text.split("/");
            } else {
                parts = text.split(".");
                if (parts.length && parts[0] !== "") {
                    const first = parts.shift();
                    at = first === "_root" || first === "_global" || /^_level\d+$/.test(first) || first === "this"
                        ? this.get(first) : (first === "_parent" ? this.target && this.target._parent : this.get(first));
                }
            }
            for (let part of parts) {
                if (part === "" || part === ".") continue;
                if (at === null || at === undefined) return undefined;
                if (part === "..") {
                    at = at._parent;
                    continue;
                }
                part = $.fold(part);
                at = at[part];
            }
            return at;
        }
        // eval(name), and paths with a variable: "/a/b:score", "a.b".
        eval(name) {
            const text = $.s(name);
            const colon = text.lastIndexOf(":");
            if (colon >= 0) {
                const owner = colon === 0 ? this.target : this.resolve(text.slice(0, colon));
                return owner ? owner[$.fold(text.slice(colon + 1))] : undefined;
            }
            if (text.indexOf("/") >= 0 || text.indexOf(".") >= 0) return this.path(text);
            return this.get(text);
        }
        // set(name, value), and the paths eval reads.
        setPath(name, value) {
            const text = $.s(name);
            let colon = text.lastIndexOf(":");
            let dot = text.lastIndexOf(".");
            if (colon < 0 && dot > 0 && text.indexOf("/") < 0) colon = dot;
            if (colon >= 0) {
                const owner = colon === 0 ? this.target : this.resolve(text.slice(0, colon));
                if (owner) owner[$.fold(text.slice(colon + 1))] = value;
                return value;
            }
            return this.set(text, value);
        }

        // ---- The timeline actions, on the target.

        clip(t) {
            return t === undefined ? this.target : this.resolve(t);
        }
        play() {
            $.callMethod(this.target, "play", []);
        }
        stop() {
            $.callMethod(this.target, "stop", []);
        }
        nextFrame() {
            $.callMethod(this.target, "nextFrame", []);
        }
        prevFrame() {
            $.callMethod(this.target, "prevFrame", []);
        }
        // gotoAndPlay and gotoAndStop: a frame (from 1) or a label, in a
        // scene when one is given (the main timeline's).
        goto(play, frame, scene) {
            const t = this.target;
            if (!t) return;
            if (scene !== undefined && $.host.gotoScene && t === $.host.root(t)) {
                $.host.gotoScene(t, $.s(scene), frame, play);
                return;
            }
            $.callMethod(t, play ? "gotoAndPlay" : "gotoAndStop", [frame]);
        }
        nextScene() {
            if ($.host.scene) $.host.scene($.host.root(this.target), 1);
        }
        prevScene() {
            if ($.host.scene) $.host.scene($.host.root(this.target), -1);
        }
        // ifFrameLoaded: a movie's frames are all there at once.
        frameLoaded() {
            return true;
        }
        // call(frame): another frame's script, run here.
        callFrame(frame) {
            if ($.host.callFrame) $.host.callFrame(this.target, frame);
        }
        getProperty(t, name) {
            const clip = this.clip(t);
            return clip ? clip[name] : undefined;
        }
        setProperty(t, name, value) {
            const clip = this.clip(t);
            if (clip) clip[name] = value;
        }
        duplicateMovieClip(t, name, depth) {
            const clip = this.clip(t);
            if (clip) $.callMethod(clip, "duplicateMovieClip", [name, $.n(depth) + 16384]);
        }
        removeMovieClip(t) {
            const clip = this.clip(t);
            if (clip) $.callMethod(clip, "removeMovieClip", []);
        }
        startDrag(t, lock, left, top, right, bottom) {
            const clip = this.clip(t);
            if (clip) $.callMethod(clip, "startDrag", arguments.length > 2 ? [lock, left, top, right, bottom] : [lock]);
        }
        stopDrag() {
            if ($.host.stopDrag) $.host.stopDrag();
        }
        loadMovie(url, t, method) {
            const clip = this.clip(t);
            if (clip) $.callMethod(clip, "loadMovie", [url, method]);
        }
        loadMovieNum(url, level) {
            if ($.host.loadMovieNum) $.host.loadMovieNum($.s(url), $.n(level));
        }
        unloadMovie(t) {
            const clip = this.clip(t);
            if (clip) $.callMethod(clip, "unloadMovie", []);
        }
        unloadMovieNum(level) {
            if ($.host.unloadMovieNum) $.host.unloadMovieNum($.n(level));
        }
        loadVariables(url, t, method) {
            const clip = this.clip(t);
            if (clip) $.callMethod(clip, "loadVariables", [url, method]);
        }
        loadVariablesNum(url, level, method) {
            const clip = $.host.level($.n(level));
            if (clip) $.callMethod(clip, "loadVariables", [url, method]);
        }
        targetPath(t) {
            return $.targetPath(this.clip(t));
        }
    }
    $.Scope = Scope;

    // A scope for code on \a target, with \a self as its this.
    $.scope = (target, self) => new Scope(target, [], self === undefined ? target : self);

    // ---- The host: what a player gives.

    $.host = {
        // The root timeline of the movie \a clip is in.
        root(clip) {
            let at = clip;
            for (let guard = 0; at && at._parent && guard < 1024; guard++) at = at._parent;
            return at;
        },
        level(n) {
            return n === 0 ? $.host.root(null) : undefined;
        },
    };

    // ---- Global functions

    $.trace = function (v) {
        $.output($.s(v));
    };
    $.getTimer = () => Math.floor($.clock());
    // random(n): a whole number from 0 to n - 1.
    $.random = function (n) {
        n = Math.floor($.n(n));
        return n > 0 ? Math.floor(Math.random() * n) : 0;
    };
    // int(x): x's whole part.
    $.int = function (x) {
        const n = $.n(x);
        return Number.isFinite(n) ? (n < 0 ? Math.ceil(n) : Math.floor(n)) | 0 : (Number.isNaN(n) ? ($.version < 7 ? 0 : NaN) : 0);
    };
    $.chr = (n) => String.fromCharCode($.n(n) & 0xffff);
    $.ord = (s) => {
        const text = $.s(s);
        return text.length ? text.charCodeAt(0) : 0;
    };
    $.length = (s) => $.s(s).length;
    // substring(s, from 1, count): AVM1's StringExtract.
    $.substring = function (s, index, count) {
        const text = $.s(s);
        let from = Math.floor($.n(index)) - 1;
        let n = Math.floor($.n(count));
        if (Number.isNaN(from) || from < 0) from = 0;
        if (Number.isNaN(n) || n < 0) n = text.length;
        return text.substr(from, n);
    };
    $.targetPath = function (clip) {
        if (!isClip(clip)) return undefined;
        return $.host.targetPath ? $.host.targetPath(clip) : "_level0";
    };
    $.getURL = function (url, window, method) {
        if ($.host.getURL) $.host.getURL($.s(url), window === undefined ? "" : $.s(window), method);
    };
    $.fscommand = function (command, argument) {
        if ($.host.fscommand) $.host.fscommand($.s(command), argument === undefined ? "" : $.s(argument));
    };
    $.stopAllSounds = function () {
        if ($.host.stopAllSounds) $.host.stopAllSounds();
    };
    // AVM1's parseInt: a leading 0 is octal, 0x hexadecimal.
    $.parseInt = function (s, radix) {
        const text = $.s(s).replace(/^[\s ]+/, "");
        if (radix === undefined || $.n(radix) === 0) {
            if (/^[-+]?0x/i.test(text)) return parseInt(text, 16);
            if (/^[-+]?0[0-7]/.test(text)) return parseInt(text, 8);
            return parseInt(text, 10);
        }
        return parseInt(text, $.n(radix));
    };
    $.parseFloat = (s) => parseFloat($.s(s));
    $.isNaN = (v) => Number.isNaN($.n(v));
    $.isFinite = (v) => Number.isFinite($.n(v));
    // escape: what is not a letter or a digit as %XX of its UTF-8 bytes.
    $.escape = function (s) {
        const bytes = new TextEncoder().encode($.s(s));
        let out = "";
        for (const b of bytes) {
            const c = String.fromCharCode(b);
            out += /[A-Za-z0-9@*_+\-./]/.test(c) && b < 128 ? c : "%" + (b < 16 ? "0" : "") + b.toString(16).toUpperCase();
        }
        return out;
    };
    $.unescape = function (s) {
        const text = $.s(s);
        const bytes = [];
        for (let i = 0; i < text.length; i++) {
            const c = text.charCodeAt(i);
            if (text[i] === "%" && /^[0-9a-f]{2}$/i.test(text.substr(i + 1, 2))) {
                bytes.push(parseInt(text.substr(i + 1, 2), 16));
                i += 2;
            } else if (c < 128) {
                bytes.push(c);
            } else {
                for (const b of new TextEncoder().encode(text[i])) bytes.push(b);
            }
        }
        return new TextDecoder().decode(new Uint8Array(bytes));
    };

    // ---- Timers: setInterval(f, ms, ...), setInterval(o, "name", ms, ...).

    const timers = new Map();
    let nextTimer = 1;
    // With manualTimers the host runs what is due with runTimers(now).
    $.manualTimers = false;
    function addTimer(args, repeat) {
        let fn, self, at = 1;
        if (typeof args[0] === "function") {
            fn = args[0];
            self = undefined;
        } else if (args[0] !== null && typeof args[0] === "object" && args.length >= 2) {
            self = args[0];
            const name = $.s(args[1]);
            fn = function () {
                return $.callMethod(self, name, Array.prototype.slice.call(arguments));
            };
            at = 2;
        } else {
            return undefined;
        }
        const ms = Math.max(0, $.n(args[at]) || 0);
        const rest = Array.prototype.slice.call(args, at + 1);
        const id = nextTimer++;
        const timer = { fn, self, ms, rest, repeat, due: $.clock() + ms, handle: null };
        timers.set(id, timer);
        if (!$.manualTimers) {
            const run = () => {
                if (!timers.has(id)) return;
                if (!repeat) timers.delete(id);
                $.guard(() => fn.apply(self, rest));
            };
            timer.handle = repeat ? setInterval(run, Math.max(ms, 1)) : setTimeout(run, ms);
        }
        return id;
    }
    function clearTimer(id) {
        const timer = timers.get($.n(id));
        if (!timer) return;
        timers.delete($.n(id));
        if (timer.handle !== null) {
            if (timer.repeat) clearInterval(timer.handle);
            else clearTimeout(timer.handle);
        }
    }
    $.runTimers = function (now) {
        for (const [id, timer] of Array.from(timers)) {
            if (timer.due > now || !timers.has(id)) continue;
            if (timer.repeat) {
                timer.due += Math.max(timer.ms, 1);
                if (timer.due <= now) timer.due = now + Math.max(timer.ms, 1);
            }
            else timers.delete(id);
            $.guard(() => timer.fn.apply(timer.self, timer.rest));
        }
    };
    // Runs code a script gave, reporting what it throws.
    $.guard = function (run) {
        try {
            return run();
        } catch (e) {
            $.error(e);
            return undefined;
        }
    };
    $.error = (e) => $.output("Error: " + (e && e.stack ? e.stack : $.s(e)));

    // ---- Arrays: sort and sortOn with AVM1's options.

    const CASEINSENSITIVE = 1, DESCENDING = 2, UNIQUESORT = 4, RETURNINDEXEDARRAY = 8, NUMERIC = 16;
    function comparer(options, by) {
        return function (a, b) {
            let x = by ? (a === null || a === undefined ? undefined : a[by]) : a;
            let y = by ? (b === null || b === undefined ? undefined : b[by]) : b;
            let r;
            if (options & NUMERIC) {
                const m = $.n(x), n = $.n(y);
                r = m < n ? -1 : m > n ? 1 : 0;
            } else {
                let s = x === undefined ? "undefined" : $.s(x), t = y === undefined ? "undefined" : $.s(y);
                if (options & CASEINSENSITIVE) {
                    s = s.toLowerCase();
                    t = t.toLowerCase();
                }
                r = s < t ? -1 : s > t ? 1 : 0;
            }
            return options & DESCENDING ? -r : r;
        };
    }
    function sorted(a, compare, options) {
        const indices = a.map((v, i) => i);
        indices.sort((i, j) => compare(a[i], a[j]) || i - j);
        if (options & UNIQUESORT) {
            for (let k = 1; k < indices.length; k++) {
                if (compare(a[indices[k - 1]], a[indices[k]]) === 0) return 0;
            }
        }
        if (options & RETURNINDEXEDARRAY) return indices;
        const values = indices.map((i) => a[i]);
        for (let k = 0; k < values.length; k++) a[k] = values[k];
        return a;
    }
    // a.sort(), a.sort(options), a.sort(compare, options).
    $.sort = function (a, first, second) {
        if (!Array.isArray(a)) return $.callMethod(a, "sort", Array.prototype.slice.call(arguments, 1));
        if (typeof first === "function") {
            const options = $.n(second) | 0;
            const compare = (x, y) => {
                const r = $.n(first(x, y));
                return options & DESCENDING ? -r : r;
            };
            return sorted(a, compare, options);
        }
        const options = $.n(first) | 0;
        return sorted(a, comparer(options), options);
    };
    // a.sortOn(name or names, options or a list of them).
    $.sortOn = function (a, names, options) {
        if (!Array.isArray(a)) return $.callMethod(a, "sortOn", Array.prototype.slice.call(arguments, 1));
        const list = Array.isArray(names) ? names : [names];
        const flags = Array.isArray(options) ? options.map((o) => $.n(o) | 0) : list.map(() => $.n(options) | 0);
        const compare = (x, y) => {
            for (let i = 0; i < list.length; i++) {
                const r = comparer(flags[i] || 0, $.s(list[i]))(x, y);
                if (r) return r;
            }
            return 0;
        };
        return sorted(a, compare, flags[0] || 0);
    };

    // ---- Classes

    // Sub extends Base: its prototype one of Base's, with __constructor__.
    $.extend = function (Sub, Base) {
        if (typeof Base !== "function") return;
        const proto = Object.create(Base.prototype);
        Object.defineProperty(proto, "constructor", { value: Sub, writable: true, configurable: true });
        Object.defineProperty(proto, "__constructor__", { value: Base, writable: true, configurable: true });
        Sub.prototype = proto;
    };
    $.implement = function (cls, interfaces) {
        cls.$implements = interfaces.filter((i) => typeof i === "function");
    };
    // A getter and setter pair as a property, as addProperty makes one.
    $.accessor = function (o, name, getter, setter) {
        Object.defineProperty(o, name, {
            get: typeof getter === "function" ? function () { return getter.call(this); } : undefined,
            set: typeof setter === "function" ? function (v) { setter.call(this, v); } : function () {},
            enumerable: false,
            configurable: true,
        });
        return true;
    };
    // The packages of a qualified name made on _global; its owner and last part.
    $.owner = function (name) {
        const parts = name.split(".");
        let at = global;
        for (let i = 0; i < parts.length - 1; i++) {
            if (at[parts[i]] === null || typeof at[parts[i]] !== "object" && typeof at[parts[i]] !== "function") at[parts[i]] = {};
            at = at[parts[i]];
        }
        return [at, parts[parts.length - 1]];
    };
    $.qualified = function (name) {
        let at = global;
        for (const part of name.split(".")) {
            if (at === null || at === undefined) return undefined;
            at = at[part];
        }
        return at;
    };
    // Members hidden from for-in.
    $.hide = function (o, names) {
        if (o === null || o === undefined) return;
        const list = names === null || names === undefined ? Object.keys(o) : Array.isArray(names) ? names : $.s(names).split(",");
        for (const k of list) {
            const d = Object.getOwnPropertyDescriptor(o, k);
            if (d && d.configurable) Object.defineProperty(o, k, Object.assign(d, { enumerable: false }));
        }
    };

    // ---- What _global has from the language

    function defineGlobals() {
        const g = global;
        g.Object = Object;
        g.Array = Array;
        g.Function = Function;
        g.Math = Math;
        g.Date = Date;
        g.Error = Error;
        g.Boolean = function (v) {
            return new.target ? new Boolean($.b(v)) : $.b(v);
        };
        g.Boolean.prototype = Boolean.prototype;
        g.Number = function (v) {
            const n = arguments.length ? $.n(v) : 0;
            return new.target ? new Number(n) : n;
        };
        g.Number.prototype = Number.prototype;
        for (const k of ["MAX_VALUE", "MIN_VALUE", "NaN", "NEGATIVE_INFINITY", "POSITIVE_INFINITY"]) g.Number[k] = Number[k];
        g.String = function (v) {
            const s = arguments.length ? $.s(v) : "";
            return new.target ? new String(s) : s;
        };
        g.String.prototype = String.prototype;
        g.String.fromCharCode = String.fromCharCode;
        g.trace = $.trace;
        g.getTimer = $.getTimer;
        g.setInterval = function () { return addTimer(arguments, true); };
        g.setTimeout = function () { return addTimer(arguments, false); };
        g.clearInterval = clearTimer;
        g.clearTimeout = clearTimer;
        g.escape = $.escape;
        g.unescape = $.unescape;
        g.parseInt = $.parseInt;
        g.parseFloat = $.parseFloat;
        g.isNaN = $.isNaN;
        g.isFinite = $.isFinite;
        g.NaN = NaN;
        g.Infinity = Infinity;
        g.ASSetPropFlags = $.hide;
        g.updateAfterEvent = function () {
            if ($.host.updateAfterEvent) $.host.updateAfterEvent();
        };
        // Object.registerClass(linkage, class): the host makes the symbol's
        // clips of the class.
        Object.defineProperty(Object, "registerClass", {
            value: function (name, cls) {
                $.registered[$.s(name)] = cls;
                return true;
            },
            writable: true,
            configurable: true,
        });
        for (const [k, v] of [
            ["CASEINSENSITIVE", CASEINSENSITIVE], ["DESCENDING", DESCENDING], ["UNIQUESORT", UNIQUESORT],
            ["RETURNINDEXEDARRAY", RETURNINDEXEDARRAY], ["NUMERIC", NUMERIC],
        ]) {
            if (!(k in Array)) Object.defineProperty(Array, k, { value: v });
        }
        const hidden = (o, name, value) => {
            if (!Object.prototype.hasOwnProperty.call(o, name)) Object.defineProperty(o, name, { value, writable: true, configurable: true });
        };
        hidden(Object.prototype, "addProperty", function (name, getter, setter) {
            if (typeof getter !== "function") return false;
            return $.accessor(this, $.s(name), getter, setter);
        });
        hidden(Object.prototype, "registerClass", undefined);
        delete Object.prototype.registerClass;
        hidden(Array.prototype, "sortOn", function (names, options) { return $.sortOn(this, names, options); });
    }
    // linkage name -> class, from Object.registerClass.
    $.registered = Object.create(null);

    // ---- Movies

    const modules = [];
    // A translated movie: module($, movie) adds its classes, scripts and
    // handlers to \a movie when the host runs it (runMovies).
    $.movie = function (module) {
        modules.push(module);
    };
    $.runMovies = function (movie) {
        for (const module of modules.splice(0)) module($, movie);
    };

    // The classes of a movie, each after the ones it needs: defines is
    // {name: [needs, function ($s)]}.
    $.defineClasses = function (defines, scope) {
        const done = new Set();
        const define = (name, depth) => {
            const entry = defines[name];
            if (!entry || done.has(name) || depth > 64) return;
            done.add(name);
            for (const need of entry[0]) define(need, depth + 1);
            $.guard(() => entry[1](scope));
        };
        for (const name of Object.keys(defines)) define(name, 0);
    };

    defineGlobals();
    return $;
})();
if (typeof module === "object" && module.exports) module.exports = avm1;
