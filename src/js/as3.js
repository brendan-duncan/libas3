// as3.js: what JavaScript translated from ActionScript 3 by libas3 runs on,
// beside a JavaScript implementation of the flash.* classes. It defines the
// global as3: the registry of definitions (packages as nested namespace
// objects, classes defined when first used), the language's operators that
// JavaScript has otherwise (is, as, int and uint, typeof, for-in), and the
// top level's classes JavaScript lacks (Vector, Dictionary, XML, the
// errors). See libas3's README, "Translating to JavaScript".
(function (root) {
"use strict";

const as3 = root.as3 || {};
root.as3 = as3;
as3.root = root;
as3.global = root;

// What a class's metadata is kept under (its qualified name, interfaces,
// fields), and a function's for an interface.
const INFO = Symbol("as3.info");
as3.INFO = INFO;
const INTERFACE = Symbol("as3.interface");

// Definitions

const definitions = new Map();
const toplevel = {};
as3.toplevel = toplevel;

function splitName(qualified) {
    const name = String(qualified).replace("::", ".");
    const dot = name.lastIndexOf(".");
    return dot < 0 ? ["", name] : [name.slice(0, dot), name.slice(dot + 1)];
}

// A package's namespace object: com.example is root.com.example. Names
// outside packages are kept apart, in as3.toplevel, so that a class named
// Error does not take the global's place.
as3.package = function (name) {
    if (!name) return toplevel;
    let at = root;
    for (const part of name.split(".")) {
        let next = Object.prototype.hasOwnProperty.call(at, part) ? at[part] : undefined;
        if (next === undefined || next === null) {
            next = {};
            Object.defineProperty(at, part, { value: next, writable: true, enumerable: false, configurable: true });
        }
        at = next;
    }
    return at;
};

function install(entry) {
    const [pkg, short] = splitName(entry.name);
    const namespace = as3.package(pkg);
    if (entry.state === 2 || entry.value !== undefined) {
        Object.defineProperty(namespace, short, {
            value: entry.value, writable: !!entry.writable, enumerable: true, configurable: true,
        });
        return;
    }
    Object.defineProperty(namespace, short, {
        enumerable: true,
        configurable: true,
        get() { return load(entry); },
        set(value) {
            load(entry);
            namespace[short] = value;
        },
    });
}

function makeInterface(name, extended) {
    const face = {
        [INTERFACE]: true,
        [INFO]: { name, short: splitName(name)[1], interfaces: extended || [], kind: "interface" },
        [Symbol.hasInstance](value) { return implementsInterface(value, face); },
        toString() { return "[class " + splitName(name)[1] + "]"; },
    };
    return face;
}

function setupClass(cls, descriptor, name) {
    const short = splitName(name)[1];
    const fields = descriptor.fields || {};
    cls[INFO] = {
        name,
        short,
        kind: "class",
        interfaces: descriptor.interfaces || [],
        dynamic: !!descriptor.dynamic,
        final: !!descriptor.final,
        // Its variables, by name: their types ("int", "const String",
        // "private Number").
        types: fields,
        fields: Object.keys(fields),
    };
    Object.defineProperty(cls.prototype, Symbol.toStringTag, { value: short, configurable: true });
}

function load(entry) {
    if (entry.state === 2) return entry.value;
    if (entry.state === 1) {
        if (entry.value !== undefined) return entry.value;
        throw new as3.Error("Error #1014: Class " + entry.name + " could not be found (it depends on itself).", 1014);
    }
    entry.state = 1;
    let descriptor;
    try {
        descriptor = entry.factory();
    } catch (error) {
        entry.state = 0;
        throw error;
    }
    let value;
    if (descriptor.class) {
        value = descriptor.class;
        setupClass(value, descriptor, entry.name);
    } else if (descriptor.interface) {
        value = makeInterface(entry.name, descriptor.extends);
    } else if ("value" in descriptor) {
        value = descriptor.value;
    }
    entry.writable = !!descriptor.variable;
    entry.value = value;
    if (entry.scope === null) install(entry);
    if (descriptor.init) {
        const made = descriptor.init();
        if (descriptor.variable || descriptor.constant) entry.value = made;
    }
    entry.state = 2;
    if (entry.scope === null) install(entry);
    return entry.value;
}

function lookup(map, name) {
    let entry = map.get(name);
    if (!entry && name.includes("::")) entry = map.get(name.replace("::", "."));
    return entry;
}

// A definition: its factory runs when it is first used, giving
// { class, interfaces, dynamic, final, fields, init }, { interface,
// extends }, { value, init } (a function or constant) or { variable,
// init } (init gives the value).
as3.define = function (name, factory) {
    const entry = { name, factory, state: 0, value: undefined, scope: null };
    definitions.set(name, entry);
    install(entry);
};

// A value made outside the translated code (the flash.* runtime's classes
// and functions), defined at once. A class gets its metadata if it has none.
as3.register = function (name, value) {
    if (typeof value === "function" && /^class\b/.test(Function.prototype.toString.call(value)) &&
        !Object.prototype.hasOwnProperty.call(value, INFO)) {
        setupClass(value, { interfaces: value.interfaces || [] }, name);
    }
    const entry = { name, factory: null, state: 2, value: adopt(value), scope: null, writable: false };
    definitions.set(name, entry);
    install(entry);
    return value;
};

// Whether a name is defined (without defining it).
as3.isDefined = function (name) {
    return !!lookup(definitions, name) || topLevelValue(name) !== undefined ||
        outside(String(name).replace("::", ".")) !== undefined;
};

// Where a name not defined through as3 is looked for last: a runtime that
// keeps its classes elsewhere adds a function (name) => value, undefined
// for a name it does not have.
as3.resolvers = [];

function outside(name) {
    // In the package tree (a runtime may have put its classes there
    // itself), then by the runtime's own resolvers.
    const [pkg, short] = splitName(name);
    if (pkg) {
        let at = root;
        for (const part of pkg.split(".")) {
            at = at !== null && at !== undefined && Object.prototype.hasOwnProperty.call(at, part) ? at[part] : undefined;
        }
        if (at !== null && at !== undefined && Object.prototype.hasOwnProperty.call(at, short) && at[short] !== undefined) {
            return at[short];
        }
    }
    for (const resolve of as3.resolvers) {
        const value = resolve(name);
        if (value !== undefined && value !== null) return value;
    }
    return undefined;
}

// A runtime class made without the construction protocol (a constructor
// that does all, no $construct): translated subclasses call
// super.$construct(...args), which then makes one with those arguments and
// takes its properties, or does nothing when there are none (the object
// was made by super() already).
function adopt(cls) {
    if (typeof cls !== "function" || !cls.prototype || typeof cls.prototype.$construct === "function") return cls;
    if (!isClass(cls) || cls === Object || cls === Function) return cls;
    Object.defineProperty(cls.prototype, "$construct", {
        value: function $construct(...args) {
            if (!args.length) return;
            const made = Reflect.construct(cls, args);
            for (const key of Object.keys(made)) this[key] = made[key];
        },
        writable: true,
        configurable: true,
    });
    return cls;
}
as3.adopt = adopt;

// What a name not defined stands for: anything done with it throws, as in
// a player, but only when it is done (a class may import what is not
// there and not use it).
function undefinedName(name) {
    const fail = () => {
        throw new as3.ReferenceError("Error #1065: Variable " + splitName(name)[1] + " is not defined.", 1065);
    };
    return new Proxy(function () {}, { get: fail, set: fail, has: fail, apply: fail, construct: fail });
}

// The definition of a qualified name, defined now if it is not yet.
as3.use = function (name) {
    const entry = lookup(definitions, name);
    if (entry) return load(entry);
    const value = topLevelValue(name);
    if (value !== undefined) return value;
    const found = outside(String(name).replace("::", "."));
    if (found !== undefined) return adopt(found);
    return undefinedName(name);
};

// A file's definitions outside its packages: only the file sees them.
// They are kept as "file$name" (so that interfaces name them so), in no
// package.
as3.file = function (fileName) {
    return {
        define(name, factory) {
            definitions.set(fileName + "$" + name, { name, factory, state: 0, value: undefined, scope: fileName });
        },
        use(name) {
            const entry = definitions.get(fileName + "$" + name);
            if (!entry) throw new as3.ReferenceError("Error #1065: Variable " + name + " is not defined.", 1065);
            return load(entry);
        },
    };
};

// A file's code outside its packages, run by runScripts once every file
// is loaded.
const scripts = [];
as3.script = function (fileName, run) {
    scripts.push({ fileName, run });
};
as3.runScripts = function () {
    while (scripts.length) scripts.shift().run();
};

// Classes and types

const BUILTIN_CLASSES = new Set([Object, Array, String, Number, Boolean, Date, RegExp, Function, Error, Map, Set]);

function isClass(value) {
    if (typeof value !== "function") return false;
    if (Object.prototype.hasOwnProperty.call(value, INFO)) return value[INFO].kind !== "function";
    if (BUILTIN_CLASSES.has(value)) return true;
    return /^class\b/.test(Function.prototype.toString.call(value));
}

function classOf(value) {
    if (value === null || value === undefined) return null;
    if (typeof value === "number") return Number;
    if (typeof value === "string") return String;
    if (typeof value === "boolean") return Boolean;
    if (value[XML_OBJECT]) return value[XML_OBJECT] === "list" ? as3.XMLList : as3.XML;
    const proto = Object.getPrototypeOf(value);
    return proto && proto.constructor ? proto.constructor : Object;
}

function infoOf(cls) {
    return cls && Object.prototype.hasOwnProperty.call(cls, INFO) ? cls[INFO] : null;
}

// "flash.display::Sprite", as getQualifiedClassName gives it.
function qualifiedName(cls) {
    if (cls === null || cls === undefined) return "null";
    if (cls === as3.int) return "int";
    if (cls === as3.uint) return "uint";
    const info = cls[INFO] && (Object.prototype.hasOwnProperty.call(cls, INFO) || cls[INTERFACE]) ? cls[INFO] : null;
    if (info && info.qualified) return info.qualified;
    if (info) {
        const [pkg, short] = splitName(info.name);
        return pkg ? pkg + "::" + short : short;
    }
    return cls.name || "Object";
}
as3.qualifiedName = qualifiedName;

function shortName(cls) {
    const info = infoOf(cls);
    if (info) return info.short;
    if (cls === as3.int) return "int";
    if (cls === as3.uint) return "uint";
    return cls.name || "Object";
}

function implementsInterface(value, face) {
    if (value === null || value === undefined) return false;
    const wanted = face[INFO].name;
    const seen = new Set();
    // Each interface named: the one wanted, or one extending it. One not
    // defined (the player's) by its name.
    const check = (names) => {
        for (const name of names) {
            if (seen.has(name)) continue;
            seen.add(name);
            const entry = lookup(definitions, name);
            if (!entry) {
                if (name === wanted) return true;
                continue;
            }
            const other = load(entry);
            if (other === face) return true;
            if (other && other[INFO] && check(other[INFO].interfaces || [])) return true;
        }
        return false;
    };
    for (let cls = classOf(value); cls && cls !== Object && cls !== Function.prototype; cls = Object.getPrototypeOf(cls)) {
        const info = infoOf(cls);
        if (info && check(info.interfaces || [])) return true;
    }
    return false;
}

// int and uint, as conversions and as types.
as3.int = function int(value) { return value | 0; };
as3.int.MAX_VALUE = 2147483647;
as3.int.MIN_VALUE = -2147483648;
as3.uint = function uint(value) { return value >>> 0; };
as3.uint.MAX_VALUE = 4294967295;
as3.uint.MIN_VALUE = 0;
as3.int[INFO] = { name: "int", short: "int", kind: "class", interfaces: [], fields: [] };
as3.uint[INFO] = { name: "uint", short: "uint", kind: "class", interfaces: [], fields: [] };

// Class, the type of classes.
as3.Class = function Class() {};
as3.Class[INFO] = { name: "Class", short: "Class", kind: "class", interfaces: [], fields: [] };

// x is T.
as3.is = function (value, type) {
    if (type === null || type === undefined) {
        throw new as3.TypeError("Error #1009: Cannot access a property or method of a null object reference.", 1009);
    }
    switch (type) {
        case as3.int: return typeof value === "number" && (value | 0) === value;
        case as3.uint: return typeof value === "number" && value >>> 0 === value;
        case Number: return typeof value === "number";
        case String: return typeof value === "string";
        case Boolean: return typeof value === "boolean";
        case Object: return value !== null && value !== undefined;
        case Function: return typeof value === "function" && !isClass(value);
        case as3.Class: return isClass(value) || !!(value && value[INTERFACE]);
        case Array: return Array.isArray(value) && !(value instanceof VectorBase);
        default: break;
    }
    if (value === null || value === undefined) return false;
    if (type[INTERFACE]) return implementsInterface(value, type);
    if (typeof type !== "function") return false;
    if (type === as3.XML) return value[XML_OBJECT] === "xml";
    if (type === as3.XMLList) return value[XML_OBJECT] === "list";
    if (typeof value !== "object" && typeof value !== "function") return false;
    return value instanceof type;
};

// x as T.
as3.as = function (value, type) {
    return as3.is(value, type) ? value : null;
};

// T(x), for a class T.
as3.cast = function (value, type) {
    if (value === null || value === undefined) return null;
    if (as3.is(value, type)) return value;
    throw new as3.TypeError("Error #1034: Type Coercion failed: cannot convert " + describeValue(value) + " to " +
        qualifiedName(type).replace("::", ".") + ".", 1034);
};

function describeValue(value) {
    if (typeof value === "object" || typeof value === "function") {
        return "[object " + shortName(classOf(value)) + "@" + "0" + "]";
    }
    return as3.string(value);
}

// typeof x.
as3.typeOf = function (value) {
    const type = typeof value;
    if (type === "function") return isClass(value) ? "object" : "function";
    if (type === "object" && value !== null && value[XML_OBJECT]) return "xml";
    if (type === "bigint" || type === "symbol") return "object";
    return type;
};

// String(x), as ActionScript has it.
as3.string = function (value) {
    if (value === undefined) return "undefined";
    if (value === null) return "null";
    if (typeof value === "function") {
        if (value[INTERFACE]) return value.toString();
        if (isClass(value) || value === as3.int || value === as3.uint) return "[class " + shortName(value) + "]";
        return "function Function() {}";
    }
    if (typeof value === "object" && value[INTERFACE]) return value.toString();
    if (value instanceof Date) return dateString(value);
    return String(value);
};

// A value stored as a String: null and undefined are null.
as3.str = function (value) {
    return value === null || value === undefined ? null : as3.string(value);
};

// trace(...): its arguments as strings, with spaces.
as3.output = function (text) { console.log(text); };
as3.trace = function (...values) {
    as3.output(values.map(as3.string).join(" "));
};

// parseInt(text, radix): with a leading 0 and no radix, octal.
as3.parseInt = function (text, radix = 0) {
    const value = String(text).trim();
    if (!radix && /^[-+]?0[0-7]+$/.test(value)) return parseInt(value, 8);
    return radix ? parseInt(value, radix) : parseInt(value);
};

// A method read as a value: bound to its object, the same function each
// time (so that removeEventListener finds what addEventListener had).
const BOUND = Symbol("as3.bound");
as3.bind = function (object, name) {
    if (object === null || object === undefined) {
        throw new as3.TypeError("Error #1009: Cannot access a property or method of a null object reference.", 1009);
    }
    if (typeof object !== "object" && typeof object !== "function") {
        // A string's or number's method: bound, each time.
        const original = object[name];
        return typeof original === "function" ? original.bind(object) : original;
    }
    let bound = object[BOUND];
    if (!bound) {
        bound = new Map();
        Object.defineProperty(object, BOUND, { value: bound, enumerable: false, configurable: true });
    }
    let method = bound.get(name);
    if (!method) {
        const original = object[name];
        if (typeof original !== "function" || isClass(original)) return original;
        method = original.bind(object);
        bound.set(name, method);
    }
    return method;
};

// with (o): whether o has a name, before the scope around it.
as3.has = function (object, name) {
    if (object === null || object === undefined) return false;
    const type = typeof object;
    if (type !== "object" && type !== "function") return false;
    return name in object;
};

// for-in and for each-in.

as3.keys = function (object) {
    if (object === null || object === undefined) return [];
    if (object[DICTIONARY]) return Array.from(object[DICTIONARY].keys());
    if (object[XML_OBJECT]) {
        const count = object.length();
        const keys = [];
        for (let i = 0; i < count; i++) keys.push(String(i));
        return keys;
    }
    const type = typeof object;
    if (type !== "object") return [];
    const keys = Object.keys(object);
    // A sealed class's fields are not enumerated.
    const sealed = new Set();
    for (let cls = classOf(object); cls && cls !== Object; cls = Object.getPrototypeOf(cls)) {
        const info = infoOf(cls);
        if (info && info.fields) for (const field of info.fields) sealed.add(field);
    }
    return sealed.size ? keys.filter((key) => !sealed.has(key)) : keys;
};

as3.values = function (object) {
    if (object === null || object === undefined) return [];
    if (object[DICTIONARY]) return Array.from(object[DICTIONARY].values());
    if (object[XML_OBJECT]) {
        const values = [];
        const count = object.length();
        for (let i = 0; i < count; i++) values.push(object[i]);
        return values;
    }
    return as3.keys(object).map((key) => object[key]);
};

// Dictionary, and o[k] where o may be one.

const DICTIONARY = Symbol("as3.dictionary");

function dictionaryKey(key) {
    return key !== null && (typeof key === "object" || typeof key === "function") ? key : String(key);
}

const dictionaryHandler = {
    get(target, property, receiver) {
        if (typeof property === "symbol") return Reflect.get(target, property, receiver);
        const map = target[DICTIONARY];
        if (map.has(property)) return map.get(property);
        return Reflect.get(target, property, receiver);
    },
    set(target, property, value, receiver) {
        if (typeof property === "symbol") return Reflect.set(target, property, value, receiver);
        target[DICTIONARY].set(property, value);
        return true;
    },
    has(target, property) {
        if (typeof property === "symbol") return property in target;
        return target[DICTIONARY].has(property);
    },
    deleteProperty(target, property) {
        if (typeof property === "symbol") return Reflect.deleteProperty(target, property);
        target[DICTIONARY].delete(property);
        return true;
    },
};

class Dictionary {
    constructor(...args) {
        Object.defineProperty(this, DICTIONARY, { value: new Map(), enumerable: false });
        const proxy = new Proxy(this, dictionaryHandler);
        if (new.target === Dictionary) proxy.$construct(...args);
        return proxy;
    }
    $construct(weakKeys = false) {
        // Weak keys are accepted; the keys are kept.
        void weakKeys;
    }
}
as3.Dictionary = Dictionary;

as3.getItem = function (object, key) {
    if (object && object[DICTIONARY]) return object[DICTIONARY].get(dictionaryKey(key));
    return object[key];
};
as3.setItem = function (object, key, value) {
    if (object && object[DICTIONARY]) object[DICTIONARY].set(dictionaryKey(key), value);
    else object[key] = value;
    return value;
};
as3.hasItem = function (object, key) {
    if (object && object[DICTIONARY]) return object[DICTIONARY].has(dictionaryKey(key));
    return as3.has(object, typeof key === "symbol" ? key : String(key));
};
as3.deleteItem = function (object, key) {
    if (object && object[DICTIONARY]) return object[DICTIONARY].delete(dictionaryKey(key)) || true;
    return delete object[key];
};

// Array: sort's options, sortOn, insertAt, removeAt.

const CASEINSENSITIVE = 1;
const DESCENDING = 2;
const UNIQUESORT = 4;
const RETURNINDEXEDARRAY = 8;
const NUMERIC = 16;
for (const [name, value] of Object.entries({ CASEINSENSITIVE, DESCENDING, UNIQUESORT, RETURNINDEXEDARRAY, NUMERIC })) {
    if (!(name in Array)) Object.defineProperty(Array, name, { value, enumerable: false });
}

function compareValues(a, b, options) {
    if (a === undefined || b === undefined) {
        if (a === undefined && b === undefined) return 0;
        return a === undefined ? 1 : -1;
    }
    let result;
    if (options & NUMERIC) {
        const x = Number(a);
        const y = Number(b);
        result = x < y ? -1 : (x > y ? 1 : 0);
    } else {
        let x = as3.string(a);
        let y = as3.string(b);
        if (options & CASEINSENSITIVE) {
            x = x.toLowerCase();
            y = y.toLowerCase();
        }
        result = x < y ? -1 : (x > y ? 1 : 0);
    }
    return options & DESCENDING ? -result : result;
}

function sortWith(array, compare, options) {
    const indexes = array.map((_, i) => i);
    const copy = array.slice();
    // Stable, as JavaScript's sort is.
    indexes.sort((i, j) => compare(copy[i], copy[j]));
    if (options & UNIQUESORT) {
        for (let i = 1; i < indexes.length; i++) {
            if (compare(copy[indexes[i - 1]], copy[indexes[i]]) === 0) return 0;
        }
    }
    if (options & RETURNINDEXEDARRAY) return indexes;
    for (let i = 0; i < indexes.length; i++) array[i] = copy[indexes[i]];
    return array;
}

// a.sort(...): a compare function, options, or both.
as3.sort = function (array, ...args) {
    if (!Array.isArray(array)) return array.sort(...args);
    let compare = null;
    let options = 0;
    if (typeof args[0] === "function") {
        compare = args[0];
        options = args[1] | 0;
    } else {
        options = args[0] | 0;
    }
    if (compare) {
        const user = compare;
        return sortWith(array, (a, b) => {
            const r = Number(user(a, b));
            const sign = r < 0 ? -1 : (r > 0 ? 1 : 0);
            return options & DESCENDING ? -sign : sign;
        }, options);
    }
    return sortWith(array, (a, b) => compareValues(a, b, options), options);
};

// a.sortOn(names, options).
as3.sortOn = function (array, names, options = 0) {
    if (!Array.isArray(array)) return array.sortOn(names, options);
    const fields = Array.isArray(names) ? names.map(String) : [String(names)];
    let fieldOptions = [];
    let flags = 0;
    if (Array.isArray(options)) {
        fieldOptions = options.map((o) => o | 0);
        if (fieldOptions.length) flags = fieldOptions[0] & (UNIQUESORT | RETURNINDEXEDARRAY);
    } else {
        flags = options | 0;
    }
    return sortWith(array, (a, b) => {
        for (let i = 0; i < fields.length; i++) {
            const option = i < fieldOptions.length ? fieldOptions[i] : flags;
            const x = a !== null && typeof a === "object" ? a[fields[i]] : undefined;
            const y = b !== null && typeof b === "object" ? b[fields[i]] : undefined;
            const result = compareValues(x, y, option);
            if (result !== 0) return result;
        }
        return 0;
    }, flags);
};

as3.insertAt = function (array, index, value) {
    if (!Array.isArray(array)) return array.insertAt(index, value);
    array.splice(index < 0 ? Math.max(0, array.length + index) : index, 0, value);
};
as3.removeAt = function (array, index) {
    if (!Array.isArray(array)) return array.removeAt(index);
    return array.splice(index < 0 ? array.length + index : index, 1)[0];
};

// Vector.<T>

const FIXED = Symbol("as3.fixed");
const ELEMENT = Symbol("as3.element");

class VectorBase extends Array {}

function coercerOf(type) {
    switch (type) {
        case as3.int: return (v) => v | 0;
        case as3.uint: return (v) => v >>> 0;
        case Number: return (v) => +v;
        case String: return (v) => as3.str(v);
        case Boolean: return (v) => !!v;
        case null: case undefined: return (v) => v;
        default: return (v) => (v === undefined ? null : v);
    }
}

function defaultOf(type) {
    switch (type) {
        case as3.int: case as3.uint: return 0;
        case Number: return 0;
        case Boolean: return false;
        case null: case undefined: return undefined;
        default: return null;
    }
}

const vectors = new Map();

function fixedError() {
    return new as3.RangeError("Error #1126: Cannot change the length of a fixed Vector.", 1126);
}

function makeVector(type) {
    const coerce = coercerOf(type);
    const fill = defaultOf(type);
    const elementName = type === null || type === undefined ? "*" : qualifiedName(type);
    class Vector extends VectorBase {
        constructor(length = 0, fixed = false) {
            super();
            const count = length >>> 0;
            for (let i = 0; i < count; i++) Array.prototype.push.call(this, fill);
            Object.defineProperty(this, FIXED, { value: !!fixed, writable: true, enumerable: false });
        }
        get fixed() { return this[FIXED]; }
        set fixed(value) { this[FIXED] = !!value; }
        push(...items) {
            if (this[FIXED] && items.length) throw fixedError();
            return super.push(...items.map(coerce));
        }
        unshift(...items) {
            if (this[FIXED] && items.length) throw fixedError();
            return super.unshift(...items.map(coerce));
        }
        pop() {
            if (this[FIXED]) throw fixedError();
            return super.pop();
        }
        shift() {
            if (this[FIXED]) throw fixedError();
            return super.shift();
        }
        splice(start, deleteCount, ...items) {
            if (this[FIXED]) throw fixedError();
            if (arguments.length < 2) return super.splice(start);
            return super.splice(start, deleteCount, ...items.map(coerce));
        }
        insertAt(index, value) { this.splice(index, 0, value); }
        removeAt(index) { return this.splice(index, 1)[0]; }
        sort(...args) { return as3.sort(Array.from(this), ...args) === 0 ? 0 : sortVector(this, args); }
        toString() { return Array.prototype.join.call(this, ","); }
    }
    Vector[ELEMENT] = type;
    Vector[INFO] = {
        name: "__AS3__.vec.Vector.<" + elementName.replace("::", ".") + ">",
        qualified: "__AS3__.vec::Vector.<" + elementName + ">",
        short: "Vector.<" + elementName + ">",
        kind: "class",
        interfaces: [],
        fields: [],
    };
    Object.defineProperty(Vector.prototype, Symbol.toStringTag, { value: "Vector.<" + elementName + ">" });
    return Vector;
}

function sortVector(vector, args) {
    const items = Array.from(vector);
    const sorted = as3.sort(items, ...args);
    if (sorted === 0 || (Array.isArray(sorted) && sorted !== items)) return sorted;
    for (let i = 0; i < items.length; i++) vector[i] = items[i];
    return vector;
}

// Vector.<T>: the class of vectors of T (null for *).
as3.Vector = function (type) {
    const key = type === undefined ? null : type;
    let made = vectors.get(key);
    if (!made) {
        made = makeVector(key);
        vectors.set(key, made);
    }
    return made;
};
as3.Vector[INFO] = { name: "__AS3__.vec.Vector", short: "Vector", kind: "class", interfaces: [], fields: [] };

// new Vector.<T>(length, fixed).
as3.vector = function (type, length = 0, fixed = false) {
    const Made = as3.Vector(type);
    return new Made(length, fixed);
};

// new <T>[a, b, c].
as3.vectorOf = function (type, items) {
    const made = as3.vector(type);
    made.push(...items);
    return made;
};

// Vector.<T>(value): a vector of T from an array or vector.
as3.toVector = function (type, value) {
    const Made = as3.Vector(type);
    if (value instanceof Made) return value;
    if (value === null || value === undefined) return null;
    if (!Array.isArray(value)) {
        throw new as3.TypeError("Error #1034: Type Coercion failed: cannot convert " + describeValue(value) +
            " to __AS3__.vec.Vector.<" + qualifiedName(type) + ">.", 1034);
    }
    const made = new Made();
    made.push(...value);
    return made;
};

// v.length = n, filling with T's default.
as3.setLength = function (vector, length) {
    if (vector instanceof VectorBase) {
        if (vector[FIXED] && length !== vector.length) throw fixedError();
        const fill = defaultOf(vector.constructor[ELEMENT]);
        const old = vector.length;
        Array.prototype.splice.call(vector, length);
        for (let i = old; i < length; i++) Array.prototype.push.call(vector, fill);
        return length;
    }
    vector.length = length;
    return length;
};

// Errors

class AS3Error extends Error {
    constructor(...args) {
        super();
        if (new.target === AS3Error) this.$construct(...args);
    }
    $construct(message = "", id = 0) {
        Object.defineProperty(this, "message", {
            value: message === undefined || message === null ? String(message ?? "") : String(message),
            writable: true, enumerable: false, configurable: true,
        });
        this.errorID = id | 0;
    }
    getStackTrace() {
        return this.stack;
    }
    toString() {
        return this.message !== "" ? this.name + ": " + this.message : this.name;
    }
}
Object.defineProperty(AS3Error.prototype, "name", { value: "Error", writable: true, configurable: true });
AS3Error[INFO] = { name: "Error", short: "Error", kind: "class", interfaces: [], fields: [], dynamic: true };
as3.Error = AS3Error;

function errorClass(name, base) {
    const made = {
        [name]: class extends base {
            constructor(...args) {
                super();
                if (new.target === made) this.$construct(...args);
            }
        },
    }[name];
    Object.defineProperty(made.prototype, "name", { value: name, writable: true, configurable: true });
    made[INFO] = { name, short: name, kind: "class", interfaces: [], fields: [], dynamic: true };
    return made;
}

for (const name of ["ArgumentError", "DefinitionError", "EvalError", "RangeError", "ReferenceError", "SecurityError",
    "SyntaxError", "TypeError", "URIError", "VerifyError", "UninitializedError"]) {
    as3[name] = errorClass(name, AS3Error);
}

// flash.errors.
for (const name of ["IOError", "EOFError", "IllegalOperationError", "InvalidSWFError", "MemoryError", "ScriptTimeoutError",
    "StackOverflowError"]) {
    const base = name === "EOFError" ? "IOError" : null;
    const made = errorClass(name, base ? definitions.get("flash.errors." + base).value : AS3Error);
    made[INFO].name = "flash.errors." + name;
    as3.register("flash.errors." + name, made);
}

// A thrown value as ActionScript has it: JavaScript's own errors become
// the errors (and messages) a player gives.
as3.error = function (thrown) {
    if (!(thrown instanceof Error) || thrown instanceof AS3Error) return thrown;
    const message = String(thrown.message || "");
    let made;
    if (thrown instanceof TypeError) {
        if (/^Cannot (read|set) propert(y|ies) of (null|undefined)/.test(message) ||
            /is (null|undefined)$/.test(message) || /null has no properties/.test(message) ||
            /^Cannot (convert|use)/.test(message) && /(null|undefined)/.test(message)) {
            made = new as3.TypeError("Error #1009: Cannot access a property or method of a null object reference.", 1009);
        } else if (/is not a function/.test(message)) {
            made = new as3.TypeError("Error #1006: value is not a function.", 1006);
        } else if (/is not a constructor/.test(message)) {
            made = new as3.TypeError("Error #1007: Instantiation attempted on a non-constructor.", 1007);
        } else {
            made = new as3.TypeError(message);
        }
    } else if (thrown instanceof ReferenceError) {
        const name = /^(\S+) is not defined/.exec(message);
        made = new as3.ReferenceError(name ? "Error #1065: Variable " + name[1] + " is not defined." : message, name ? 1065 : 0);
    } else if (thrown instanceof RangeError) {
        if (/call stack/i.test(message)) made = new as3.Error("Error #1023: Stack overflow occurred.", 1023);
        else if (/Invalid array length/.test(message)) made = new as3.RangeError("Error #1005: Array index is not a positive integer.", 1005);
        else made = new as3.RangeError(message);
    } else if (thrown instanceof SyntaxError) {
        if (/JSON/.test(message) || /in JSON at position/.test(message)) made = new as3.SyntaxError("Error #1132: Invalid JSON parse input.", 1132);
        else made = new as3.SyntaxError(message);
    } else if (thrown instanceof URIError) {
        made = new as3.URIError(message);
    } else if (thrown instanceof EvalError) {
        made = new as3.EvalError(message);
    } else {
        made = new as3.Error(message);
    }
    if (thrown.stack) Object.defineProperty(made, "stack", { value: thrown.stack, configurable: true, writable: true });
    return made;
};

// A regular expression with named groups, (?<name>...): the groups are the
// match's properties too, as ActionScript has them.
class AS3RegExp extends RegExp {
    exec(text) {
        const match = super.exec(text);
        if (match && match.groups) Object.assign(match, match.groups);
        return match;
    }
}
as3.regExp = function (pattern) {
    return new AS3RegExp(pattern.source, pattern.flags);
};

// Namespace and QName, as values.
class Namespace {
    constructor(prefix, uri) {
        if (uri === undefined) {
            this.uri = prefix === undefined ? "" : String(prefix);
            this.prefix = undefined;
        } else {
            this.prefix = prefix;
            this.uri = String(uri);
        }
    }
    toString() { return this.uri; }
    valueOf() { return this.uri; }
}
class QName {
    constructor(namespace, localName) {
        if (localName === undefined) {
            this.uri = "";
            this.localName = String(namespace);
        } else {
            this.uri = namespace === null ? null : String(namespace instanceof Namespace ? namespace.uri : namespace);
            this.localName = String(localName);
        }
    }
    toString() { return this.uri ? this.uri + "::" + this.localName : this.localName; }
}
as3.Namespace = Namespace;
as3.QName = QName;

as3.isXMLName = function (name) {
    return /^[A-Za-z_À-￿][\w.\-·À-￿]*$/.test(String(name));
};

// Date: ActionScript's properties (fullYear, month, time...) and String().

const DAYS = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];

function two(n) { return (n < 10 ? "0" : "") + n; }

function dateString(date) {
    if (isNaN(date.getTime())) return "Invalid Date";
    const offset = -date.getTimezoneOffset();
    const sign = offset < 0 ? "-" : "+";
    const abs = Math.abs(offset);
    return DAYS[date.getDay()] + " " + MONTHS[date.getMonth()] + " " + date.getDate() + " " + two(date.getHours()) + ":" +
        two(date.getMinutes()) + ":" + two(date.getSeconds()) + " GMT" + sign + two(Math.floor(abs / 60)) + two(abs % 60) +
        " " + date.getFullYear();
}

// d.toString() and the rest, as a player writes them.
as3.dateText = function (date, method) {
    if (!(date instanceof Date)) return date[method]();
    if (isNaN(date.getTime())) return "Invalid Date";
    const local = dateString(date);
    const parts = local.split(" ");   // day month date clock zone year
    const hours = date.getHours();
    const clock12 = two(hours % 12 === 0 ? 12 : hours % 12) + ":" + two(date.getMinutes()) + ":" + two(date.getSeconds()) +
        (hours < 12 ? " AM" : " PM");
    switch (method) {
        case "toString": return local;
        case "toDateString": case "toLocaleDateString": return parts[0] + " " + parts[1] + " " + parts[2] + " " + parts[5];
        case "toTimeString": return parts[3] + " " + parts[4];
        case "toLocaleString": return parts[0] + " " + parts[1] + " " + parts[2] + " " + parts[5] + " " + clock12;
        case "toLocaleTimeString": return clock12;
        case "toUTCString":
            return DAYS[date.getUTCDay()] + " " + MONTHS[date.getUTCMonth()] + " " + date.getUTCDate() + " " +
                two(date.getUTCHours()) + ":" + two(date.getUTCMinutes()) + ":" + two(date.getUTCSeconds()) + " " +
                date.getUTCFullYear() + " UTC";
        default: return date[method]();
    }
};

const dateProperties = {
    fullYear: "FullYear", month: "Month", date: "Date", hours: "Hours", minutes: "Minutes", seconds: "Seconds",
    milliseconds: "Milliseconds", fullYearUTC: "UTCFullYear", monthUTC: "UTCMonth", dateUTC: "UTCDate",
    hoursUTC: "UTCHours", minutesUTC: "UTCMinutes", secondsUTC: "UTCSeconds", millisecondsUTC: "UTCMilliseconds",
};
for (const [name, method] of Object.entries(dateProperties)) {
    if (name in Date.prototype) continue;
    Object.defineProperty(Date.prototype, name, {
        get() { return this["get" + method](); },
        set(value) { this["set" + method](value); },
        enumerable: false,
        configurable: true,
    });
}
for (const [name, method] of Object.entries({ day: "getDay", dayUTC: "getUTCDay", timezoneOffset: "getTimezoneOffset" })) {
    if (name in Date.prototype) continue;
    Object.defineProperty(Date.prototype, name, { get() { return this[method](); }, enumerable: false, configurable: true });
}
if (!("time" in Date.prototype)) {
    Object.defineProperty(Date.prototype, "time", {
        get() { return this.getTime(); },
        set(value) { this.setTime(value); },
        enumerable: false,
        configurable: true,
    });
}

// flash.utils: the clock and timers. The host may drive them: as3.clock
// gives the milliseconds since the movie started, and with
// as3.manualTimers the host runs what is due with as3.runTimers(now).

const started = typeof performance !== "undefined" ? performance.now() : Date.now();
as3.clock = function () {
    return (typeof performance !== "undefined" ? performance.now() : Date.now()) - started;
};
as3.manualTimers = false;
const timers = [];
let nextTimer = 1;

function schedule(callback, delay, args, repeat) {
    if (typeof callback !== "function") {
        throw new as3.TypeError("Error #1034: Type Coercion failed: Function expected.", 1034);
    }
    const id = nextTimer++;
    const interval = repeat ? Math.max(1, Number(delay) || 0) : 0;
    const entry = { id, callback, args, interval, due: as3.clock() + Math.max(0, Number(delay) || 0), handle: null };
    timers.push(entry);
    if (!as3.manualTimers) {
        const run = () => {
            const at = timers.indexOf(entry);
            if (at < 0) return;
            if (entry.interval > 0) {
                entry.due += entry.interval;
                entry.handle = root.setTimeout(run, entry.interval);
            } else {
                timers.splice(at, 1);
            }
            entry.callback(...entry.args);
        };
        entry.handle = root.setTimeout(run, Math.max(0, Number(delay) || 0));
    }
    return id;
}

function clearTimer(id) {
    const at = timers.findIndex((entry) => entry.id === (id | 0));
    if (at < 0) return;
    const [entry] = timers.splice(at, 1);
    if (entry.handle !== null) root.clearTimeout(entry.handle);
}

// What is due by now (milliseconds, as as3.clock counts them), in order.
as3.runTimers = function (now) {
    for (;;) {
        let due = null;
        for (const entry of timers) {
            if (!due || entry.due < due.due) due = entry;
        }
        if (!due || due.due > now) return;
        if (due.interval > 0) due.due += Math.max(due.interval, 1);
        else timers.splice(timers.indexOf(due), 1);
        due.callback(...due.args);
    }
};

const flashUtils = {
    getTimer() { return Math.floor(as3.clock()); },
    setTimeout(callback, delay, ...args) { return schedule(callback, delay, args, false); },
    setInterval(callback, delay, ...args) { return schedule(callback, delay, args, true); },
    clearTimeout: clearTimer,
    clearInterval: clearTimer,
    getDefinitionByName(name) {
        const text = String(name);
        if (as3.isDefined(text)) return as3.use(text);
        throw new as3.ReferenceError("Error #1065: Variable " + text + " is not defined.", 1065);
    },
    getQualifiedClassName(value) {
        if (value === undefined) return "void";
        if (value === null) return "null";
        if (typeof value === "number") return (value | 0) === value ? "int" : "Number";
        if (typeof value === "string") return "String";
        if (typeof value === "boolean") return "Boolean";
        if ((typeof value === "function" && (isClass(value) || value === as3.int || value === as3.uint)) || value[INTERFACE]) {
            return qualifiedName(value);
        }
        if (typeof value === "function") return "Function";
        return qualifiedName(classOf(value));
    },
    getQualifiedSuperclassName(value) {
        if (value === null || value === undefined) return null;
        const cls = typeof value === "function" && isClass(value) ? value : classOf(value);
        if (!cls || cls === Object) return null;
        const base = Object.getPrototypeOf(cls);
        if (!base || base === Function.prototype) return "Object";
        if (base === Error) return "Object";
        return qualifiedName(base);
    },
    describeType(value) {
        return as3.toXML(describe(value));
    },
    escapeMultiByte(text) { return encodeURIComponent(String(text)); },
    unescapeMultiByte(text) { return decodeURIComponent(String(text)); },
};

function describe(value) {
    const attr = escapeAttribute;
    const isType = typeof value === "function" && isClass(value);
    const cls = isType ? value : classOf(value);
    const info = infoOf(cls);
    const name = value === null ? "null" : value === undefined ? "void" : qualifiedName(cls);
    const heritage = () => {
        let out = "";
        for (let at = Object.getPrototypeOf(cls); at && at !== Function.prototype; at = Object.getPrototypeOf(at)) {
            out += '<extendsClass type="' + attr(qualifiedName(at)) + '"/>';
        }
        if (cls !== Object) out += '<extendsClass type="Object"/>';
        for (let at = cls; at && at !== Function.prototype; at = Object.getPrototypeOf(at)) {
            const own = infoOf(at);
            if (own) for (const face of own.interfaces) out += '<implementsInterface type="' + attr(face.replace(/\.([^.]*)$/, "::$1")) + '"/>';
        }
        return out;
    };
    const members = () => {
        const names = new Map();
        for (let at = cls; at && at !== Object && at !== Function.prototype; at = Object.getPrototypeOf(at)) {
            const own = infoOf(at);
            const declaredBy = qualifiedName(at);
            if (own && own.types) {
                for (const field of own.fields) {
                    const type = own.types[field];
                    if (field.includes("$") || names.has(field) || /^(private|protected|internal) /.test(type)) continue;
                    const constant = type.startsWith("const ");
                    const typeName = constant ? type.slice(6) : type;
                    names.set(field, "<" + (constant ? "constant" : "variable") + ' name="' + attr(field) + '" type="' +
                        attr(typeName.includes(".") ? typeName.replace(/\.([^.<]*)$/, "::$1") : typeName) + '"/>');
                }
            }
            for (const key of Object.getOwnPropertyNames(at.prototype || {})) {
                if (key === "constructor" || key.includes("$") || names.has(key)) continue;
                const descriptor = Object.getOwnPropertyDescriptor(at.prototype, key);
                if (descriptor.get || descriptor.set) {
                    const access = descriptor.get && descriptor.set ? "readwrite" : descriptor.get ? "readonly" : "writeonly";
                    names.set(key, '<accessor name="' + attr(key) + '" access="' + access + '" type="*" declaredBy="' + attr(declaredBy) + '"/>');
                } else if (typeof descriptor.value === "function") {
                    names.set(key, '<method name="' + attr(key) + '" declaredBy="' + attr(declaredBy) + '" returnType="*"/>');
                }
            }
        }
        return Array.from(names.keys()).sort().map((key) => names.get(key)).join("");
    };
    if (isType) {
        return '<type name="' + attr(name) + '" base="Class" isDynamic="true" isFinal="true" isStatic="true">' +
            '<extendsClass type="Class"/><extendsClass type="Object"/><factory type="' + attr(name) + '">' + heritage() +
            members() + "</factory></type>";
    }
    const base = cls && cls !== Object ? flashUtils.getQualifiedSuperclassName(cls) : null;
    return '<type name="' + attr(name) + '"' + (base ? ' base="' + attr(base) + '"' : "") + ' isDynamic="' +
        (info ? !!info.dynamic : true) + '" isFinal="' + (info ? !!info.final : false) + '" isStatic="false">' +
        (cls ? heritage() + members() : "") + "</type>";
}

Object.assign(as3, flashUtils);

// The top level's classes, by name.
function topLevelValue(name) {
    switch (name) {
        case "int": return as3.int;
        case "uint": return as3.uint;
        case "Number": return Number;
        case "String": return String;
        case "Boolean": return Boolean;
        case "Object": return Object;
        case "Array": return Array;
        case "Function": return Function;
        case "Math": return Math;
        case "Date": return Date;
        case "RegExp": return RegExp;
        case "JSON": return JSON;
        case "Class": return as3.Class;
        case "XML": return as3.XML;
        case "XMLList": return as3.XMLList;
        case "Namespace": return as3.Namespace;
        case "QName": return as3.QName;
        case "__AS3__.vec.Vector": case "Vector": return as3.Vector;
        default: break;
    }
    if (/Error$/.test(name) && as3[name] && as3[name][INFO]) return as3[name];
    return undefined;
}

// XML and XMLList (E4X): XML parsed into a tree of nodes; x.name gives the
// children of that name, x["@name"] an attribute, x.descendants(name) the
// descendants, as3.xmlFilter(list, test) those items the test is true of.

const XML_OBJECT = Symbol("as3.xml");
const NODE = Symbol("as3.node");
const settings = {
    ignoreComments: true,
    ignoreProcessingInstructions: true,
    ignoreWhitespace: true,
    prettyPrinting: true,
    prettyIndent: 2,
};

class XmlNode {
    constructor(kind, name = "", value = "") {
        this.kind = kind;   // element, text, comment, processing-instruction, attribute
        this.name = name;
        this.value = value;
        this.attributes = [];   // [name, value]
        this.children = [];
        this.parent = null;
    }
}

function xmlError(message, id) {
    return new as3.TypeError(message, id);
}

function parseXml(text) {
    let at = 0;
    const nodes = [];
    const isNameChar = (c) => /[A-Za-z0-9_:.\-]/.test(c) || c.charCodeAt(0) >= 0x80;
    const fail = (what) => { throw xmlError("Error #1090: XML parser failure: " + what + ".", 1090); };
    const readName = () => {
        const start = at;
        while (at < text.length && isNameChar(text[at])) at++;
        if (at === start) fail("element is malformed");
        return text.slice(start, at);
    };
    const skipSpace = () => {
        while (at < text.length && /\s/.test(text[at])) at++;
    };
    const decode = (raw) => raw.replace(/&(#x[0-9a-fA-F]+|#[0-9]+|lt|gt|amp|quot|apos);/g, (all, entity) => {
        switch (entity) {
            case "lt": return "<";
            case "gt": return ">";
            case "amp": return "&";
            case "quot": return '"';
            case "apos": return "'";
            default: {
                const code = entity[1] === "x" || entity[1] === "X" ? parseInt(entity.slice(2), 16) : parseInt(entity.slice(1), 10);
                return code ? String.fromCodePoint(code) : all;
            }
        }
    });
    const addText = (into, parent, raw, cdata) => {
        let value = cdata ? raw : decode(raw);
        if (!cdata && settings.ignoreWhitespace) {
            value = value.replace(/^[ \t\r\n]+|[ \t\r\n]+$/g, "");
            if (!value) return;
        }
        const node = new XmlNode("text", "", value);
        node.parent = parent;
        into.push(node);
    };
    const readNode = (into, parent) => {
        if (text[at] !== "<") {
            const next = text.indexOf("<", at);
            const end = next < 0 ? text.length : next;
            addText(into, parent, text.slice(at, end), false);
            at = end;
            return;
        }
        if (text.startsWith("<!--", at)) {
            const end = text.indexOf("-->", at);
            if (end < 0) fail("unterminated comment");
            if (!settings.ignoreComments) {
                const node = new XmlNode("comment", "", text.slice(at + 4, end));
                node.parent = parent;
                into.push(node);
            }
            at = end + 3;
            return;
        }
        if (text.startsWith("<![CDATA[", at)) {
            const end = text.indexOf("]]>", at);
            if (end < 0) fail("unterminated CDATA section");
            addText(into, parent, text.slice(at + 9, end), true);
            at = end + 3;
            return;
        }
        if (text.startsWith("<?", at)) {
            const end = text.indexOf("?>", at);
            if (end < 0) fail("unterminated processing instruction");
            if (!settings.ignoreProcessingInstructions) {
                const body = text.slice(at + 2, end);
                const space = body.search(/[ \t\r\n]/);
                const node = new XmlNode("processing-instruction", space < 0 ? body : body.slice(0, space),
                    space < 0 ? "" : body.slice(space + 1));
                node.parent = parent;
                into.push(node);
            }
            at = end + 2;
            return;
        }
        if (text.startsWith("</", at)) fail("unexpected end tag");
        at++;
        const element = new XmlNode("element");
        element.parent = parent;
        element.name = readName();
        for (;;) {
            skipSpace();
            if (at >= text.length) fail("element is malformed");
            if (text[at] === "/") {
                if (!text.startsWith("/>", at)) fail("element is malformed");
                at += 2;
                into.push(element);
                return;
            }
            if (text[at] === ">") {
                at++;
                break;
            }
            const name = readName();
            skipSpace();
            if (text[at] !== "=") fail("attribute is malformed");
            at++;
            skipSpace();
            const quote = text[at];
            if (quote !== '"' && quote !== "'") fail("attribute is malformed");
            at++;
            const end = text.indexOf(quote, at);
            if (end < 0) fail("attribute is malformed");
            element.attributes.push([name, decode(text.slice(at, end))]);
            at = end + 1;
        }
        for (;;) {
            if (at >= text.length) fail("element is not closed");
            if (text.startsWith("</", at)) {
                at += 2;
                const name = readName();
                skipSpace();
                if (name !== element.name || text[at] !== ">") {
                    throw xmlError('Error #1085: The element type "' + element.name +
                        '" must be terminated by the matching end-tag "</' + element.name + '>".', 1085);
                }
                at++;
                break;
            }
            readNode(element.children, element);
        }
        into.push(element);
    };
    while (at < text.length) {
        if (text.startsWith("<?xml", at)) {
            const end = text.indexOf("?>", at);
            at = end < 0 ? text.length : end + 2;
            continue;
        }
        if (text.startsWith("<!DOCTYPE", at)) {
            const end = text.indexOf(">", at);
            at = end < 0 ? text.length : end + 1;
            continue;
        }
        readNode(nodes, null);
    }
    return nodes;
}

function escapeText(text) {
    return String(text).replace(/[<>&]/g, (c) => (c === "<" ? "&lt;" : c === ">" ? "&gt;" : "&amp;"));
}

function escapeAttribute(text) {
    return String(text).replace(/[<&"]/g, (c) => (c === "<" ? "&lt;" : c === "&" ? "&amp;" : "&quot;"));
}

function simpleContent(node) {
    if (node.kind !== "element") return node.kind === "text" || node.kind === "attribute";
    return !node.children.some((child) => child.kind === "element");
}

function writeNode(node, level) {
    const indent = settings.prettyPrinting ? " ".repeat(level * settings.prettyIndent) : "";
    switch (node.kind) {
        case "text": return indent + escapeText(node.value);
        case "attribute": return escapeAttribute(node.value);
        case "comment": return indent + "<!--" + node.value + "-->";
        case "processing-instruction": return indent + "<?" + node.name + (node.value ? " " + node.value : "") + "?>";
        default: break;
    }
    let out = indent + "<" + node.name;
    for (const [name, value] of node.attributes) out += " " + name + '="' + escapeAttribute(value) + '"';
    if (!node.children.length) return out + "/>";
    out += ">";
    const onlyText = node.children.every((child) => child.kind === "text");
    if (onlyText || !settings.prettyPrinting) {
        for (const child of node.children) out += child.kind === "text" ? escapeText(child.value) : writeNode(child, 0);
    } else {
        for (const child of node.children) out += "\n" + writeNode(child, level + 1);
        out += "\n" + indent;
    }
    return out + "</" + node.name + ">";
}

function textOf(node) {
    if (node.kind === "text" || node.kind === "attribute") return node.value;
    if (node.kind === "element" && simpleContent(node)) {
        return node.children.filter((child) => child.kind === "text").map((child) => child.value).join("");
    }
    return writeNode(node, 0);
}

function listText(nodes, markup) {
    const simple = nodes.every(simpleContent);
    if (!markup && simple) return nodes.map(textOf).join("");
    return nodes.map((node) => writeNode(node, 0)).join("\n");
}

function copyNode(node, parent) {
    const made = new XmlNode(node.kind, node.name, node.value);
    made.attributes = node.attributes.map((pair) => pair.slice());
    made.parent = parent;
    made.children = node.children.map((child) => copyNode(child, made));
    return made;
}

function textNode(text, parent) {
    const node = new XmlNode("text", "", text);
    node.parent = parent;
    return node;
}

function attributeNode(element, pair) {
    const node = new XmlNode("attribute", pair[0], pair[1]);
    node.parent = element;
    return node;
}

function pick(node, name) {
    if (node.kind !== "element") return [];
    if (name.startsWith("@")) {
        const attribute = name.slice(1);
        return node.attributes.filter((pair) => attribute === "*" || pair[0] === attribute).map((pair) => attributeNode(node, pair));
    }
    return node.children.filter((child) => name === "*" || (child.kind === "element" && child.name === name));
}

function descendantsOf(node, name, found) {
    if (node.kind !== "element") return;
    if (name.startsWith("@")) found.push(...pick(node, name));
    for (const child of node.children) {
        if (!name.startsWith("@") && (name === "*" || (child.kind === "element" && child.name === name))) found.push(child);
        descendantsOf(child, name, found);
    }
}

function isXmlValue(value) {
    return value !== null && value !== undefined && !!value[XML_OBJECT];
}

function nodesOf(value) {
    if (!isXmlValue(value)) return [];
    const target = value[NODE];
    return target.nodes ? target.nodes : [target.node];
}

function putChild(node, name, value) {
    if (node.kind !== "element") return;
    if (name.startsWith("@")) {
        const attribute = name.slice(1);
        const text = as3.string(value);
        const pair = node.attributes.find((p) => p[0] === attribute);
        if (pair) pair[1] = text;
        else node.attributes.push([attribute, text]);
        return;
    }
    const isXml = isXmlValue(value);
    const content = isXml ? nodesOf(value).map((item) => copyNode(item, node)) : [];
    if (name === "*") {
        node.children = isXml ? content : [textNode(as3.string(value), node)];
        return;
    }
    const children = node.children;
    const first = children.findIndex((child) => child.kind === "element" && child.name === name);
    if (first < 0) {
        const element = new XmlNode("element", name);
        element.parent = node;
        if (isXml) {
            for (const item of content) item.parent = element;
            element.children = content;
        } else {
            element.children = [textNode(as3.string(value), element)];
        }
        children.push(element);
        return;
    }
    if (isXml && content.length === 1 && content[0].kind === "element") {
        content[0].parent = node;
        children[first] = content[0];
    } else {
        const target = children[first];
        if (isXml) {
            for (const item of content) item.parent = target;
            target.children = content;
        } else {
            target.children = [textNode(as3.string(value), target)];
        }
    }
    for (let i = children.length - 1; i > first; i--) {
        if (children[i].kind === "element" && children[i].name === name) children.splice(i, 1);
    }
}

function removeChildren(node, name) {
    if (node.kind !== "element") return;
    if (name.startsWith("@")) {
        const attribute = name.slice(1);
        node.attributes = node.attributes.filter((pair) => !(attribute === "*" || pair[0] === attribute));
        return;
    }
    node.children = node.children.filter((child) => !(name === "*" || (child.kind === "element" && child.name === name)));
}

function isIndex(key) {
    return typeof key === "string" && /^(0|[1-9][0-9]*)$/.test(key);
}

const xmlObjects = new WeakMap();

function makeXml(node) {
    let made = xmlObjects.get(node);
    if (!made) {
        const target = Object.create(XML.prototype);
        target.node = node;
        made = new Proxy(target, xmlHandler);
        xmlObjects.set(node, made);
    }
    return made;
}

function makeList(nodes, target = null, targetName = "", from = null) {
    const list = Object.create(XMLList.prototype);
    list.nodes = nodes;
    list.target = target;
    list.targetName = targetName;
    list.from = from;
    return new Proxy(list, listHandler);
}

function parseValue(value) {
    if (isXmlValue(value)) return nodesOf(value).map((node) => copyNode(node, null));
    if (value === null || value === undefined) return [];
    return parseXml(as3.string(value));
}

function parseOne(value) {
    const nodes = parseValue(value);
    if (!nodes.length) return textNode("", null);
    if (nodes.length > 1) {
        throw xmlError("Error #1088: The markup in the document following the root element must be well-formed.", 1088);
    }
    nodes[0].parent = null;
    return nodes[0];
}

// Names in XML read children before methods, unless they are methods.
function xmlGet(target, property, receiver, pickFrom) {
    if (property === NODE) return target;
    if (typeof property === "symbol") return Reflect.get(target, property, receiver);
    if (property in Object.getPrototypeOf(target) || property === "node" || property === "nodes") {
        const value = Reflect.get(target, property, receiver);
        return typeof value === "function" ? value.bind(receiver) : value;
    }
    return pickFrom(property);
}

const xmlHandler = {
    get(target, property, receiver) {
        return xmlGet(target, property, receiver, (name) => {
            if (isIndex(name)) return name === "0" ? receiver : undefined;
            return makeList(pick(target.node, name), target.node, name);
        });
    },
    set(target, property, value) {
        if (typeof property === "symbol") {
            target[property] = value;
            return true;
        }
        if (!isIndex(property)) putChild(target.node, property, value);
        return true;
    },
    has(target, property) {
        if (typeof property === "symbol") return property in target;
        if (isIndex(property)) return property === "0";
        if (property.startsWith("@")) return true;
        return pick(target.node, property).length > 0;
    },
    deleteProperty(target, property) {
        if (typeof property !== "symbol") removeChildren(target.node, property);
        return true;
    },
};

function listContainer(list) {
    if (list.target) return list.target;
    if (list.from) {
        const from = list.from;
        if (!from.nodes.length) listMakeOne(from);
        if (from.nodes.length === 1) list.target = from.nodes[0];
    }
    return list.target;
}

function listMakeOne(list) {
    const name = list.targetName;
    if (!name || name === "*" || name.startsWith("@")) return;
    const where = listContainer(list);
    if (!where) return;
    const element = new XmlNode("element", name);
    element.parent = where;
    where.children.push(element);
    list.nodes.push(element);
}

const listHandler = {
    get(target, property, receiver) {
        return xmlGet(target, property, receiver, (name) => {
            if (isIndex(name)) {
                const node = target.nodes[Number(name)];
                return node ? makeXml(node) : undefined;
            }
            const found = [];
            for (const node of target.nodes) found.push(...pick(node, name));
            return makeList(found, target.nodes.length === 1 ? target.nodes[0] : null, name, target);
        });
    },
    set(target, property, value) {
        if (typeof property === "symbol") {
            target[property] = value;
            return true;
        }
        if (!isIndex(property)) {
            if (!target.nodes.length) listMakeOne(target);
            if (target.nodes.length === 1) putChild(target.nodes[0], property, value);
            return true;
        }
        const index = Number(property);
        const isXml = isXmlValue(value);
        if (index < target.nodes.length) {
            const node = target.nodes[index];
            if (node.kind === "attribute") {
                const element = node.parent;
                if (element) for (const pair of element.attributes) if (pair[0] === node.name) pair[1] = as3.string(value);
                node.value = as3.string(value);
                return true;
            }
            node.children = isXml ? nodesOf(value).map((item) => copyNode(item, node)) : [textNode(as3.string(value), node)];
            return true;
        }
        const where = listContainer(target);
        const name = target.targetName;
        if (where && name && name !== "*" && !name.startsWith("@")) {
            const element = new XmlNode("element", name);
            element.parent = where;
            element.children = isXml ? nodesOf(value).map((item) => copyNode(item, element)) : [textNode(as3.string(value), element)];
            where.children.push(element);
            target.nodes.push(element);
        }
        return true;
    },
    has(target, property) {
        if (typeof property === "symbol") return property in target;
        if (isIndex(property)) return Number(property) < target.nodes.length;
        if (property.startsWith("@")) return true;
        return target.nodes.some((node) => pick(node, property).length > 0);
    },
    deleteProperty(target, property) {
        if (typeof property === "symbol") return true;
        if (isIndex(property)) {
            const index = Number(property);
            const node = target.nodes[index];
            if (node) {
                const parent = node.parent;
                if (parent) {
                    if (node.kind === "attribute") parent.attributes = parent.attributes.filter((pair) => pair[0] !== node.name);
                    else parent.children = parent.children.filter((child) => child !== node);
                }
                target.nodes.splice(index, 1);
            }
            return true;
        }
        for (const node of target.nodes) removeChildren(node, property);
        return true;
    },
};

// What XML and XMLList share, on the nodes of either.
const shared = {
    toString() { return listText(nodesOf(this), false); },
    toXMLString() { return listText(nodesOf(this), true); },
    valueOf() { return this; },
    [Symbol.toPrimitive]() { return listText(nodesOf(this), false); },
    length() { return nodesOf(this).length; },
    children() {
        const nodes = nodesOf(this);
        const found = [];
        for (const node of nodes) found.push(...pick(node, "*"));
        return makeList(found, nodes.length === 1 ? nodes[0] : null, "*");
    },
    child(name) {
        const nodes = nodesOf(this);
        if (typeof name === "number") {
            const found = [];
            for (const node of nodes) {
                const all = pick(node, "*");
                if (name < all.length) found.push(all[name]);
            }
            return makeList(found);
        }
        const key = as3.string(name);
        const found = [];
        for (const node of nodes) found.push(...pick(node, key));
        return makeList(found, nodes.length === 1 ? nodes[0] : null, key);
    },
    elements(name = "*") {
        const found = [];
        for (const node of nodesOf(this)) found.push(...pick(node, as3.string(name)).filter((item) => item.kind === "element"));
        return makeList(found);
    },
    attributes() {
        const found = [];
        for (const node of nodesOf(this)) found.push(...pick(node, "@*"));
        return makeList(found);
    },
    attribute(name) {
        const found = [];
        for (const node of nodesOf(this)) found.push(...pick(node, "@" + as3.string(name)));
        return makeList(found);
    },
    descendants(name = "*") {
        const found = [];
        for (const node of nodesOf(this)) descendantsOf(node, as3.string(name), found);
        return makeList(found);
    },
    text() {
        const found = [];
        for (const node of nodesOf(this)) found.push(...node.children.filter((child) => child.kind === "text"));
        return makeList(found);
    },
    comments() {
        const found = [];
        for (const node of nodesOf(this)) found.push(...node.children.filter((child) => child.kind === "comment"));
        return makeList(found);
    },
    hasSimpleContent() { return nodesOf(this).every(simpleContent); },
    hasComplexContent() { return nodesOf(this).some((node) => node.kind === "element" && !simpleContent(node)); },
    copy() {
        const copies = nodesOf(this).map((node) => copyNode(node, null));
        return this[XML_OBJECT] === "xml" ? makeXml(copies[0]) : makeList(copies);
    },
    parent() {
        const nodes = nodesOf(this);
        const parent = nodes.length ? nodes[0].parent : null;
        if (!parent || nodes.some((node) => node.parent !== parent)) return undefined;
        return makeXml(parent);
    },
    hasOwnProperty(name) {
        return as3.has(this, String(name));
    },
};

class XML {
    constructor(value) {
        const target = this;
        target.node = parseOne(value);
        const made = new Proxy(target, xmlHandler);
        xmlObjects.set(target.node, made);
        return made;
    }
    static get ignoreComments() { return settings.ignoreComments; }
    static set ignoreComments(v) { settings.ignoreComments = !!v; }
    static get ignoreProcessingInstructions() { return settings.ignoreProcessingInstructions; }
    static set ignoreProcessingInstructions(v) { settings.ignoreProcessingInstructions = !!v; }
    static get ignoreWhitespace() { return settings.ignoreWhitespace; }
    static set ignoreWhitespace(v) { settings.ignoreWhitespace = !!v; }
    static get prettyPrinting() { return settings.prettyPrinting; }
    static set prettyPrinting(v) { settings.prettyPrinting = !!v; }
    static get prettyIndent() { return settings.prettyIndent; }
    static set prettyIndent(v) { settings.prettyIndent = Math.max(0, v | 0); }
    static settings() { return Object.assign({}, settings); }
    static setSettings(value) {
        if (!value) {
            Object.assign(settings, XML.defaultSettings());
            return;
        }
        for (const key of Object.keys(settings)) if (key in value) settings[key] = value[key];
    }
    static defaultSettings() {
        return { ignoreComments: true, ignoreProcessingInstructions: true, ignoreWhitespace: true, prettyPrinting: true, prettyIndent: 2 };
    }
    name() {
        const node = this.node;
        return node.kind === "element" || node.kind === "attribute" || node.kind === "processing-instruction" ? node.name : null;
    }
    localName() {
        const name = this.node.name;
        const colon = name.indexOf(":");
        return colon < 0 ? name : name.slice(colon + 1);
    }
    setName(name) { this.node.name = as3.string(name); }
    setLocalName(name) { this.node.name = as3.string(name); }
    nodeKind() { return this.node.kind; }
    childIndex() {
        const parent = this.node.parent;
        return parent ? parent.children.indexOf(this.node) : -1;
    }
    appendChild(value) {
        for (const item of parseValue(value)) {
            item.parent = this.node;
            this.node.children.push(item);
        }
        return makeXml(this.node);
    }
    prependChild(value) {
        const items = parseValue(value);
        for (const item of items) item.parent = this.node;
        this.node.children.unshift(...items);
        return makeXml(this.node);
    }
    insertChildAfter(ref, value) {
        const refs = nodesOf(ref);
        const items = parseValue(value);
        for (const item of items) item.parent = this.node;
        const children = this.node.children;
        if (!refs.length) {
            children.unshift(...items);
        } else {
            const at = children.indexOf(refs[0]);
            if (at < 0) return undefined;
            children.splice(at + 1, 0, ...items);
        }
        return makeXml(this.node);
    }
    insertChildBefore(ref, value) {
        const refs = nodesOf(ref);
        const items = parseValue(value);
        for (const item of items) item.parent = this.node;
        const children = this.node.children;
        if (!refs.length) {
            children.push(...items);
        } else {
            const at = children.indexOf(refs[0]);
            if (at < 0) return undefined;
            children.splice(at, 0, ...items);
        }
        return makeXml(this.node);
    }
    setChildren(value) {
        putChild(this.node, "*", value);
        return makeXml(this.node);
    }
    replace(name, value) {
        const node = this.node;
        if (typeof name === "number") {
            if (name < node.children.length) {
                let items = parseValue(value);
                if (!items.length) items = [textNode(as3.string(value), node)];
                for (const item of items) item.parent = node;
                node.children.splice(name, 1, ...items);
            }
            return makeXml(node);
        }
        putChild(node, as3.string(name), value);
        return makeXml(node);
    }
    normalize() { return makeXml(this.node); }
}
Object.defineProperty(XML.prototype, XML_OBJECT, { value: "xml" });

class XMLList {
    constructor(value) {
        const target = this;
        target.nodes = parseValue(value);
        target.target = null;
        target.targetName = "";
        target.from = null;
        return new Proxy(target, listHandler);
    }
    contains(value) {
        const wanted = as3.string(value);
        return this.nodes.some((node) => writeNode(node, 0) === wanted || textOf(node) === wanted);
    }
    name() { return this.nodes.length === 1 ? makeXml(this.nodes[0]).name() : undefined; }
    localName() { return this.nodes.length === 1 ? makeXml(this.nodes[0]).localName() : undefined; }
    nodeKind() { return this.nodes.length === 1 ? this.nodes[0].kind : undefined; }
    appendChild(value) { return this.nodes.length === 1 ? makeXml(this.nodes[0]).appendChild(value) : undefined; }
    childIndex() { return this.nodes.length === 1 ? makeXml(this.nodes[0]).childIndex() : -1; }
}
Object.defineProperty(XMLList.prototype, XML_OBJECT, { value: "list" });

for (const key of Reflect.ownKeys(shared)) {
    for (const cls of [XML, XMLList]) {
        if (!Object.prototype.hasOwnProperty.call(cls.prototype, key)) {
            Object.defineProperty(cls.prototype, key, { value: shared[key], writable: true, configurable: true });
        }
    }
}
XML[INFO] = { name: "XML", short: "XML", kind: "class", interfaces: [], fields: [], final: true, dynamic: true };
XMLList[INFO] = { name: "XMLList", short: "XMLList", kind: "class", interfaces: [], fields: [], final: true, dynamic: true };
as3.XML = XML;
as3.XMLList = XMLList;

// XML(value) and XMLList(value), called.
as3.toXML = function (value) {
    if (isXmlValue(value)) {
        if (value[XML_OBJECT] === "xml") return value;
        const nodes = nodesOf(value);
        if (nodes.length === 1) return makeXml(nodes[0]);
    }
    return makeXml(parseOne(value));
};
as3.toXMLList = function (value) {
    if (isXmlValue(value) && value[XML_OBJECT] === "list") return value;
    return makeList(parseValue(value));
};

// {value} in an XML literal: in content, escaped text (XML as markup); as
// an attribute's value, quoted.
as3.xmlText = function (value) {
    if (isXmlValue(value)) return listText(nodesOf(value), true);
    return escapeText(as3.string(value));
};
as3.xmlAttribute = function (value) {
    return '"' + escapeAttribute(as3.string(value)) + '"';
};

// list.(test): the items it is true of.
as3.xmlFilter = function (list, test) {
    if (!isXmlValue(list)) {
        throw new as3.TypeError("Error #1123: Filter operator not supported on type " + flashUtils.getQualifiedClassName(list) + ".", 1123);
    }
    const kept = nodesOf(list).filter((node) => test(makeXml(node)));
    return makeList(kept);
};

// flash.utils.ByteArray: bytes, read and written at a position, in either
// byte order; b[i] is a byte. (Not here: compress, and AMF's readObject and
// writeObject.)

const BYTES = Symbol("as3.bytes");

function eofError() {
    const EOFError = as3.use("flash.errors.EOFError");
    return new EOFError("Error #2030: End of file was encountered.", 2030);
}

const byteArrayHandler = {
    get(target, key, receiver) {
        if (typeof key === "string" && isIndex(key)) {
            const i = Number(key);
            return i < target.length ? target[BYTES][i] : undefined;
        }
        return Reflect.get(target, key, receiver);
    },
    set(target, key, value, receiver) {
        if (typeof key === "string" && isIndex(key)) {
            const i = Number(key);
            target.reserve(i + 1);
            if (i >= target._length) target._length = i + 1;
            target[BYTES][i] = value & 0xFF;
            return true;
        }
        return Reflect.set(target, key, value, receiver);
    },
    has(target, key) {
        if (typeof key === "string" && isIndex(key)) return Number(key) < target._length;
        return key in target;
    },
};

class ByteArray {
    constructor(...args) {
        Object.defineProperty(this, BYTES, { value: new Uint8Array(16), writable: true, enumerable: false });
        Object.defineProperty(this, "_length", { value: 0, writable: true, enumerable: false });
        Object.defineProperty(this, "_position", { value: 0, writable: true, enumerable: false });
        Object.defineProperty(this, "_little", { value: false, writable: true, enumerable: false });
        const proxy = new Proxy(this, byteArrayHandler);
        if (new.target === ByteArray) proxy.$construct(...args);
        return proxy;
    }
    $construct() {}
    reserve(size) {
        if (size <= this[BYTES].length) return;
        let capacity = this[BYTES].length * 2;
        while (capacity < size) capacity *= 2;
        const grown = new Uint8Array(capacity);
        grown.set(this[BYTES].subarray(0, this._length));
        this[BYTES] = grown;
    }
    view(size) {
        return new DataView(this[BYTES].buffer, this[BYTES].byteOffset + this._position, size);
    }
    // Room for size bytes at the position, the length past them.
    room(size) {
        this.reserve(this._position + size);
        if (this._position + size > this._length) this._length = this._position + size;
        const view = this.view(size);
        this._position += size;
        return view;
    }
    // size bytes to read at the position.
    taken(size) {
        if (this._position + size > this._length) throw eofError();
        const view = this.view(size);
        this._position += size;
        return view;
    }
    get length() { return this._length; }
    set length(value) {
        const size = value >>> 0;
        this.reserve(size);
        if (size > this._length) this[BYTES].fill(0, this._length, size);
        this._length = size;
        if (this._position > size) this._position = size;
    }
    get position() { return this._position; }
    set position(value) { this._position = value >>> 0; }
    get bytesAvailable() { return Math.max(0, this._length - this._position); }
    get endian() { return this._little ? "littleEndian" : "bigEndian"; }
    set endian(value) { this._little = value === "littleEndian"; }
    clear() {
        this[BYTES] = new Uint8Array(16);
        this._length = 0;
        this._position = 0;
    }
    writeBoolean(value) { this.room(1).setUint8(0, value ? 1 : 0); }
    writeByte(value) { this.room(1).setInt8(0, value); }
    writeShort(value) { this.room(2).setInt16(0, value, this._little); }
    writeInt(value) { this.room(4).setInt32(0, value, this._little); }
    writeUnsignedInt(value) { this.room(4).setUint32(0, value >>> 0, this._little); }
    writeFloat(value) { this.room(4).setFloat32(0, value, this._little); }
    writeDouble(value) { this.room(8).setFloat64(0, value, this._little); }
    writeUTFBytes(text) {
        for (const b of utf8(as3.string(text))) this.room(1).setUint8(0, b);
    }
    writeUTF(text) {
        const bytes = utf8(as3.string(text));
        if (bytes.length > 65535) throw new as3.RangeError("Error #2006: The supplied index is out of bounds.", 2006);
        this.writeShort(bytes.length);
        for (const b of bytes) this.room(1).setUint8(0, b);
    }
    writeMultiByte(text, charSet) {
        void charSet;
        this.writeUTFBytes(text);
    }
    writeBytes(bytes, offset = 0, length = 0) {
        const from = bytes[BYTES];
        let count = length >>> 0;
        if (count === 0) count = Math.max(0, bytes.length - offset);
        if (offset + count > bytes.length) throw new as3.RangeError("Error #2006: The supplied index is out of bounds.", 2006);
        const copy = from.slice(offset, offset + count);
        for (const b of copy) this.room(1).setUint8(0, b);
    }
    readBoolean() { return this.taken(1).getUint8(0) !== 0; }
    readByte() { return this.taken(1).getInt8(0); }
    readUnsignedByte() { return this.taken(1).getUint8(0); }
    readShort() { return this.taken(2).getInt16(0, this._little); }
    readUnsignedShort() { return this.taken(2).getUint16(0, this._little); }
    readInt() { return this.taken(4).getInt32(0, this._little); }
    readUnsignedInt() { return this.taken(4).getUint32(0, this._little); }
    readFloat() { return this.taken(4).getFloat32(0, this._little); }
    readDouble() { return this.taken(8).getFloat64(0, this._little); }
    readUTFBytes(length) {
        const count = length >>> 0;
        if (this._position + count > this._length) throw eofError();
        const text = fromUtf8(this[BYTES].subarray(this._position, this._position + count));
        this._position += count;
        return text;
    }
    readUTF() { return this.readUTFBytes(this.readUnsignedShort()); }
    readMultiByte(length, charSet) {
        void charSet;
        return this.readUTFBytes(length);
    }
    readBytes(bytes, offset = 0, length = 0) {
        let count = length >>> 0;
        if (count === 0) count = this.bytesAvailable;
        if (this._position + count > this._length) throw eofError();
        const copy = this[BYTES].slice(this._position, this._position + count);
        this._position += count;
        const at = bytes.position;
        bytes.position = offset;
        for (const b of copy) bytes.room(1).setUint8(0, b);
        bytes.position = at;
    }
    toString() { return fromUtf8(this[BYTES].subarray(0, this._length)); }
}

function utf8(text) {
    if (typeof TextEncoder !== "undefined") return new TextEncoder().encode(text);
    const out = [];
    for (const c of unescape(encodeURIComponent(text))) out.push(c.charCodeAt(0));
    return out;
}

function fromUtf8(bytes) {
    if (typeof TextDecoder !== "undefined") return new TextDecoder().decode(bytes);
    let text = "";
    for (const b of bytes) text += String.fromCharCode(b);
    return decodeURIComponent(escape(text));
}

as3.ByteArray = ByteArray;

class Endian {}
Endian.BIG_ENDIAN = "bigEndian";
Endian.LITTLE_ENDIAN = "littleEndian";

// flash.utils, unless the flash.* runtime has its own.
for (const name of ["getTimer", "setTimeout", "setInterval", "clearTimeout", "clearInterval", "getDefinitionByName",
    "getQualifiedClassName", "getQualifiedSuperclassName", "describeType", "escapeMultiByte", "unescapeMultiByte"]) {
    if (!definitions.has("flash.utils." + name)) as3.register("flash.utils." + name, flashUtils[name]);
}
if (!definitions.has("flash.utils.Dictionary")) as3.register("flash.utils.Dictionary", Dictionary);
if (!definitions.has("flash.utils.ByteArray")) as3.register("flash.utils.ByteArray", ByteArray);
if (!definitions.has("flash.utils.Endian")) as3.register("flash.utils.Endian", Endian);

})(typeof globalThis !== "undefined" ? globalThis : this);
