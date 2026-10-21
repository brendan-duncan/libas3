#include "avm1_interpreter.h"

#include "avm1_actions.h"
#include "../bytecode/vm.h"
#include "../runtime/profile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <stdexcept>

namespace as3 {
namespace avm1 {

namespace detail {

// What keeps a function's frame off the C++ stack of the ones that call it.
#if defined(_MSC_VER)
#define AVM1_NOINLINE __declspec(noinline)
#else
#define AVM1_NOINLINE __attribute__((noinline))
#endif

// What stops the actions outright, which no try block catches: a script
// that runs on and on, or calls itself too deeply.
struct Halt {
    std::string message;
};

// A link of the scope chain names are looked up along: _global, a
// timeline, a function's locals, or a with block's object.
struct Scope;
using ScopePtr = std::shared_ptr<const Scope>;
struct Scope {
    enum Kind : uint8_t { Global, Target, Local, With };
    Kind kind;
    Val object;
    ScopePtr parent;
    Scope(Kind kind, Val object, ScopePtr parent) : kind(kind), object(std::move(object)), parent(std::move(parent)) {}
};

using Pool = std::shared_ptr<const std::vector<std::string>>;

// What ActionScript 1 keeps on an object the VM does not: its prototype
// (__proto__), the properties addProperty gave it, the names
// ASSetPropFlags hid from for-in, and the interfaces a class's prototype
// implements. As the object's host, it has the VM find members along the
// prototypes too.
class ObjectData : public HostObject {
public:
    explicit ObjectData(std::shared_ptr<Machine*> machine) : machine(std::move(machine)) {}

    std::shared_ptr<Machine*> machine;
    std::weak_ptr<Value> self;
    bool protoSet = false;
    Val proto;
    std::map<std::string, std::pair<Val, Val>> accessors;
    // Each accessor in the order they were added, with how many dynamic
    // properties there were then: where for-in finds it among them.
    std::vector<std::pair<std::string, size_t>> accessorOrder;
    std::set<std::string> hidden;
    // Those delete leaves be (ASSetPropFlags's 2).
    std::set<std::string> permanent;
    std::vector<Val> interfaces;
    // A super object: the this of the method it is in, and the object the
    // method was found on (its prototype is where super looks).
    bool isSuper = false;
    Val superThis;
    Val home;

    ValuePtr get(const std::string& name) override;
    bool set(const std::string& name, ValuePtr value) override;

    // Lets go of what it holds, to break cycles once the movie is done.
    virtual void clear() {
        proto = Val();
        accessors.clear();
        accessorOrder.clear();
        interfaces.clear();
        superThis = Val();
        home = Val();
    }
};

// A function the actions defined (DefineFunction, DefineFunction2): its
// body, the scope chain and constant pool it was defined with, its
// parameters and what DefineFunction2 preloads.
class FunctionData : public ObjectData {
public:
    using ObjectData::ObjectData;

    Actions code;
    size_t start = 0;
    size_t end = 0;
    std::string name;
    bool two = false;
    uint8_t registerCount = 0;
    uint16_t flags = 0;
    std::vector<std::pair<uint8_t, std::string>> params;
    ScopePtr scope;
    Pool pool;
    // The timeline it was defined on, which its timeline actions act on.
    Val base;
    int version = 8;
    std::string where;

    void clear() override {
        ObjectData::clear();
        scope.reset();
        base = Val();
    }
};

// _global: the VM's globals, and the definitions it has (Math, Array...).
class GlobalData : public HostObject {
public:
    explicit GlobalData(std::shared_ptr<Machine*> machine) : machine(std::move(machine)) {}
    std::shared_ptr<Machine*> machine;
    ValuePtr get(const std::string& name) override;
    bool set(const std::string& name, ValuePtr value) override;
};

// A try or with block the actions are in.
struct Block {
    enum Kind : uint8_t { WithBlock, TryBlock, CatchBlock, FinallyBlock };
    Kind kind = WithBlock;
    size_t start = 0;
    size_t end = 0;
    size_t catchStart = 0, catchEnd = 0, finallyStart = 0, finallyEnd = 0;
    bool hasCatch = false, hasFinally = false, inRegister = false;
    uint8_t reg = 0;
    std::string name;
    // A with block's: the scope chain before it.
    ScopePtr saved;
    // A finally block run on the way out: what comes after it.
    enum After : uint8_t { Continue, Rethrow, Return };
    After after = Continue;
    Val pending;
};

// A run of actions: a frame's, a handler's, or a function call's.
struct Activation {
    Actions code;
    const uint8_t* bytes = nullptr;
    size_t start = 0;
    size_t end = 0;
    Pool pool;
    ScopePtr scope;
    // The chain before tellTarget changed the target.
    ScopePtr beforeTarget;
    bool targetSet = false;
    Val self;
    // What timeline actions act on (tellTarget changes it), and the
    // timeline the actions are on.
    Val target;
    Val base;
    std::vector<Val> registers;
    // A function's: its locals, the function, its arguments and the object
    // it was found on (for super).
    bool inFunction = false;
    Val locals;
    FunctionData* function = nullptr;
    Val callee;
    std::vector<Val> args;
    Val home;
    int version = 8;
    std::vector<Val> stack;
    std::vector<Block> blocks;
    Val returned;
    std::string where;
};

const char* const kPropertyNames[] = {"_x",       "_y",           "_xscale",   "_yscale",       "_currentframe",
                                      "_totalframes", "_alpha",   "_visible",  "_width",        "_height",
                                      "_rotation", "_target",     "_framesloaded", "_name",     "_droptarget",
                                      "_url",     "_highquality", "_focusrect", "_soundbuftime", "_quality",
                                      "_xmouse",  "_ymouse"};
constexpr int kPropertyCount = static_cast<int>(sizeof(kPropertyNames) / sizeof(kPropertyNames[0]));

constexpr uint64_t kStepLimit = 40000000;
constexpr int kDepthLimit = 256;
const char* const kVersionText = "WIN 8,0,0,0";

bool sameNameIgnoringCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::u16string toUtf16(const std::string& text) {
    std::u16string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        uint32_t code = c;
        size_t extra = 0;
        if (c >= 0xF0) {
            code = c & 0x07;
            extra = 3;
        } else if (c >= 0xE0) {
            code = c & 0x0F;
            extra = 2;
        } else if (c >= 0xC0) {
            code = c & 0x1F;
            extra = 1;
        }
        ++i;
        for (size_t k = 0; k < extra && i < text.size(); ++k, ++i) {
            code = (code << 6) | (static_cast<unsigned char>(text[i]) & 0x3F);
        }
        if (code >= 0x10000) {
            code -= 0x10000;
            out += static_cast<char16_t>(0xD800 + (code >> 10));
            out += static_cast<char16_t>(0xDC00 + (code & 0x3FF));
        } else {
            out += static_cast<char16_t>(code);
        }
    }
    return out;
}

void appendUtf8(std::string& out, uint32_t code) {
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

std::string fromUtf16(const std::u16string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        uint32_t code = text[i];
        if (code >= 0xD800 && code < 0xDC00 && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] < 0xE000) {
            code = 0x10000 + ((code - 0xD800) << 10) + (text[i + 1] - 0xDC00);
            ++i;
        }
        appendUtf8(out, code);
    }
    return out;
}

// A number as ActionScript 1 writes it: fifteen significant digits, and
// exponents beyond them.
std::string numberText(double value) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value > 0 ? "Infinity" : "-Infinity";
    if (value == 0) return "0";
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.14e", value);
    std::string text = buffer;
    std::string out;
    size_t at = 0;
    if (text[0] == '-') {
        out += '-';
        at = 1;
    }
    const size_t e = text.find('e');
    std::string digits;
    for (size_t i = at; i < e; ++i) {
        if (text[i] != '.') digits += text[i];
    }
    const int exponent = std::atoi(text.c_str() + e + 1);
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    if (exponent < -5 || exponent >= 15) {
        out += digits[0];
        if (digits.size() > 1) out += "." + digits.substr(1);
        out += exponent >= 0 ? "e+" : "e-";
        out += std::to_string(std::abs(exponent));
        return out;
    }
    if (exponent >= 0) {
        const size_t whole = static_cast<size_t>(exponent) + 1;
        if (digits.size() <= whole) {
            out += digits + std::string(whole - digits.size(), '0');
        } else {
            out += digits.substr(0, whole) + "." + digits.substr(whole);
        }
        return out;
    }
    out += "0." + std::string(static_cast<size_t>(-exponent - 1), '0') + digits;
    return out;
}

// Text as a number, as ActionScript 1 reads it: NaN when it is not one
// (an empty string too), hexadecimal with 0x.
double parseNumber(const std::string& text) {
    size_t first = 0;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first]))) ++first;
    if (first == text.size()) return NAN;
    size_t last = text.size();
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) --last;
    const std::string trimmed = text.substr(first, last - first);
    size_t at = 0;
    bool negative = false;
    if (trimmed[0] == '-' || trimmed[0] == '+') {
        negative = trimmed[0] == '-';
        at = 1;
    }
    if (trimmed.size() > at + 2 && trimmed[at] == '0' && (trimmed[at + 1] == 'x' || trimmed[at + 1] == 'X')) {
        double result = 0;
        for (size_t i = at + 2; i < trimmed.size(); ++i) {
            const char c = trimmed[i];
            int digit;
            if (c >= '0' && c <= '9') {
                digit = c - '0';
            } else if (c >= 'a' && c <= 'f') {
                digit = c - 'a' + 10;
            } else if (c >= 'A' && c <= 'F') {
                digit = c - 'A' + 10;
            } else {
                return NAN;
            }
            result = result * 16 + digit;
        }
        return negative ? -result : result;
    }
    return stringToNumber(trimmed);
}

bool isIndex(const std::string& name, size_t& index) {
    if (name.empty() || name.size() > 9) return false;
    if (name.size() > 1 && name[0] == '0') return false;
    size_t value = 0;
    for (const char c : name) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<size_t>(c - '0');
    }
    index = value;
    return true;
}

bool sameObject(const Val& a, const Val& b) { return a.isObject() && b.isObject() && a.ref == b.ref; }

// Where a variable's name splits into the path to the clip it is on and
// its own name: "/hat/bow:size" at the colon, "_root.hat.size" at the last
// dot (not in a slash path, where ".." is the parent). npos for a plain
// name.
size_t variableSplit(const std::string& name) {
    const size_t colon = name.rfind(':');
    if (colon != std::string::npos) return colon;
    if (name.find('/') != std::string::npos) return std::string::npos;
    const size_t dot = name.rfind('.');
    return dot == 0 ? std::string::npos : dot;
}

} // namespace detail

using namespace detail;

class Machine {
public:
    Machine(VM& vm, Host& host, int version);
    ~Machine();

    VM& vm;
    Host& host;
    int version;
    std::shared_ptr<Machine*> life;
    // The values given ObjectData, emptied when the movie is done.
    std::vector<std::weak_ptr<Value>> attached;
    Val globalObject;
    // Returned from a constructor, new gives undefined (as a player's own
    // class does when given too little to make one).
    Val noObject;
    Val objectProto;
    Val functionProto;
    // The accessors running, by object and name: one read again from inside
    // itself reads undefined, as the player has it, rather than recursing.
    std::set<std::pair<const Value*, std::string>> runningAccessors;
    // ActionScript 1's own versions of the VM's array methods (join writes
    // undefined as the movie's version does), found before the VM's.
    std::map<std::string, Val> arrayMethods;
    std::map<std::string, Val> registered;
    std::mt19937 random{12345};
    int depth = 0;
    int runs = 0;
    uint64_t steps = 0;
    Activation* current = nullptr;

    // -- Objects

    ObjectData* data(const Val& value) const {
        if (!value.isObject() || !value->host) return nullptr;
        return dynamic_cast<ObjectData*>(value->host.get());
    }
    FunctionData* functionData(const Val& value) const {
        if (!value.isObject() || !value->host) return nullptr;
        return dynamic_cast<FunctionData*>(value->host.get());
    }
    // An object's ObjectData, given one when it has no host; null for an
    // object a host answers for.
    ObjectData* attach(const Val& value) {
        if (!value.isObject()) return nullptr;
        if (ObjectData* found = data(value)) return found;
        if (value->host) return nullptr;
        auto made = std::make_shared<ObjectData>(life);
        made->self = value.ref;
        value->host = made;
        attached.push_back(value.ref);
        return made.get();
    }
    Val newObject(const Val& proto) {
        Val object = vm.newObject();
        auto made = std::make_shared<ObjectData>(life);
        made->self = object.ref;
        made->protoSet = true;
        made->proto = proto;
        object->host = made;
        attached.push_back(object.ref);
        return object;
    }
    void hide(const Val& object, const std::string& name) {
        if (ObjectData* d = attach(object)) d->hidden.insert(name);
    }
    Val native(const std::string& name, NativeMethod method, int length = 0) {
        Val function = vm.makeFunction(name, std::move(method), length);
        vm.track(function);
        return function;
    }
    // A global by name: the VM's own, or a definition it has.
    bool globalValue(const std::string& name, Val& out) {
        Val found = vm.getGlobal(name);
        if (!found.isUndefined()) {
            out = found;
            return true;
        }
        try {
            found = vm.lookupQualified(name, true);
        } catch (const ScriptException&) {
            found = Val();
        }
        if (!found.isUndefined()) {
            out = found;
            return true;
        }
        return false;
    }

    Val protoOf(const Val& value);
    Val prototypeOf(const Val& constructor);
    bool vmHas(const Val& object, const std::string& name);
    Val vmGet(const Val& object, const std::string& name);
    bool lookupOwn(const Val& at, const std::string& name, Val& out, const Val& receiver, bool vmMembers);
    bool lookupFrom(const Val& start, const std::string& name, Val& out, const Val& receiver, Val* holder);
    bool lookup(const Val& object, const std::string& name, Val& out, Val* holder = nullptr);
    Val getMember(const Val& object, const std::string& name);
    void setMember(const Val& object, const std::string& name, const Val& value);
    bool deleteMember(const Val& object, const std::string& name);
    std::vector<std::string> enumerate(const Val& object);

    // -- Conversions

    Val toPrimitive(const Val& value, bool preferString);
    std::string toString(const Val& value);
    double toNumber(const Val& value);
    bool toBoolean(const Val& value);
    int32_t toInt32(const Val& value) { return as3::toInt32(toNumber(value)); }
    bool looseEquals(const Val& a, const Val& b);
    Val lessThan(const Val& a, const Val& b);
    std::string typeOf(const Val& value);
    bool instanceOf(const Val& value, const Val& constructor);
    bool implementsInterface(const Val& proto, const Val& constructor, int depth);
    Val result(bool value, int version) const { return version >= 5 ? Val::boolean(value) : Val::number(value ? 1 : 0); }
    std::string slashPath(const Val& clip);

    // -- Calls

    Val callValue(const Val& function, const Val& self, const std::vector<Val>& args, const Val& home = Val());
    Val callAvm1(FunctionData& function, const Val& callee, const Val& self, const std::vector<Val>& args,
                 const Val& home);
    Val callMethod(const Val& object, const std::string& name, const std::vector<Val>& args);
    Val construct(const Val& constructor, const std::vector<Val>& args);
    Val superObject(const Activation& a);
    Val argumentsArray(const Activation& a);
    Val makeFunction(FunctionData* made);

    // -- Variables

    bool findVariable(Activation& a, const std::string& name, Val& out, Val* self);
    Val getVariable(Activation& a, const std::string& name);
    void setVariable(Activation& a, const std::string& name, const Val& value);
    void defineLocal(Activation& a, const std::string& name, const Val& value, bool onlyIfMissing);
    bool deleteVariable(Activation& a, const std::string& name);
    bool resolveTarget(Activation& a, const Val& start, const std::string& path, Val& out);
    Val targetOf(Activation& a, const Val& target);
    void setTarget(Activation& a, const Val& clip);

    // -- Running

    bool run(const Actions& actions, const Val& clip, const std::string& where);
    void execute(Activation& a);
    AVM1_NOINLINE bool leaveBlocks(Activation& a, size_t& pc);
    Val pop(Activation& a);
    AVM1_NOINLINE bool opStack(Activation& a, uint8_t op, size_t start, size_t next);
    AVM1_NOINLINE void opMath(Activation& a, uint8_t op);
    AVM1_NOINLINE bool opFlow(Activation& a, uint8_t op, size_t start, size_t next, size_t& pc);
    AVM1_NOINLINE void opObjects(Activation& a, uint8_t op);
    AVM1_NOINLINE void opDefine(Activation& a, uint8_t op, size_t start, size_t next, size_t& pc);
    AVM1_NOINLINE void opCall(Activation& a, uint8_t op);
    AVM1_NOINLINE void opMovie(Activation& a, uint8_t op, size_t start, size_t next, size_t& pc);
    bool unwindReturn(Activation& a, size_t& pc);
    bool handleThrow(Activation& a, const Val& value, size_t& pc);
    void report(const std::string& where, const std::string& message);
    void step() {
        if (++steps > kStepLimit) throw Halt{"a script ran too long and was stopped"};
    }

private:
    void setupGlobals();
};

// -- ObjectData ---------------------------------------------------------------

ValuePtr ObjectData::get(const std::string& name) {
    Machine* m = machine ? *machine : nullptr;
    ValuePtr object = self.lock();
    if (!m || !object) return nullptr;
    const Val receiver(object);
    const auto accessor = accessors.find(name);
    if (accessor != accessors.end()) {
        return m->callValue(accessor->second.first, receiver, {});
    }
    if (object->properties.count(name)) return nullptr;
    if (name == "__proto__") return protoSet ? ValuePtr(proto) : nullptr;
    // Along the prototypes: their accessors and properties.
    Val at = m->protoOf(receiver);
    for (int guard = 0; at.isObject() && guard < kDepthLimit; ++guard) {
        if (ObjectData* d = m->data(at)) {
            const auto found = d->accessors.find(name);
            if (found != d->accessors.end()) {
                return m->callValue(found->second.first, receiver, {});
            }
        }
        if (const Val* found = at->properties.lookup(name)) return *found;
        at = m->protoOf(at);
    }
    return nullptr;
}

bool ObjectData::set(const std::string& name, ValuePtr value) {
    Machine* m = machine ? *machine : nullptr;
    ValuePtr object = self.lock();
    if (!m || !object) return false;
    if (name == "__proto__") {
        protoSet = true;
        proto = Val(value);
        return true;
    }
    const Val receiver(object);
    Val at = receiver;
    for (int guard = 0; at.isObject() && guard < kDepthLimit; ++guard) {
        if (ObjectData* d = m->data(at)) {
            const auto found = d->accessors.find(name);
            if (found != d->accessors.end()) {
                // One without a setter is read-only.
                if (found->second.second.isFunction()) {
                    m->callValue(found->second.second, receiver, {Val(value)});
                }
                return true;
            }
        }
        if (at.ref != object && at->properties.count(name)) break;
        at = m->protoOf(at);
    }
    return false;
}

ValuePtr GlobalData::get(const std::string& name) {
    Machine* m = machine ? *machine : nullptr;
    Val found;
    if (m && m->globalValue(name, found)) return found;
    return nullptr;
}

bool GlobalData::set(const std::string& name, ValuePtr value) {
    Machine* m = machine ? *machine : nullptr;
    if (!m) return false;
    m->vm.setGlobal(name, value);
    return true;
}

// -- Machine ------------------------------------------------------------------

Machine::Machine(VM& vm, Host& host, int version)
    : vm(vm), host(host), version(version), life(std::make_shared<Machine*>(this)) {
    objectProto = newObject(Val::null());
    functionProto = newObject(objectProto);
    const auto constructorOf = [this](const char* name, const Val& proto) {
        Val found;
        if (!globalValue(name, found) || !found.isObject()) return;
        found->properties["prototype"] = proto;
        proto->properties["constructor"] = found;
        hide(proto, "constructor");
    };
    constructorOf("Object", objectProto);
    constructorOf("Function", functionProto);
    globalObject = vm.newObject();
    globalObject->host = std::make_shared<GlobalData>(life);
    noObject = vm.newObject();
    setupGlobals();
}

Machine::~Machine() {
    *life = nullptr;
    // What objects hold of each other through what the VM does not empty.
    for (const std::weak_ptr<Value>& weak : attached) {
        if (ValuePtr value = weak.lock()) {
            if (ObjectData* d = dynamic_cast<ObjectData*>(value->host.get())) d->clear();
        }
    }
    registered.clear();
}

void Machine::setupGlobals() {
    const auto define = [this](const std::string& name, const Val& value) {
        if (vm.getGlobal(name)->isUndefined()) vm.setGlobal(name, value);
    };
    // An array as text: its elements as ActionScript 1 writes them (null as
    // null, undefined as the movie's version has it), a separator between.
    const auto joined = [this](const Val& self, const std::string& separator) -> std::string {
        std::string out;
        if (!self.isObject() || self->type != ValueType::Array) return out;
        for (size_t i = 0; i < self->elements.size(); ++i) {
            if (i) out += separator;
            out += toString(self->elements[i]);
        }
        return out;
    };
    arrayMethods["join"] = native("join", [this, joined](VM&, const Val& self, Args args) -> Val {
        return Val::string(joined(self, args.size() > 0 && !args[0].isUndefined() ? toString(args[0]) : ","));
    }, 1);
    arrayMethods["toString"] = native("toString", [joined](VM&, const Val& self, Args) -> Val {
        return Val::string(joined(self, ","));
    });
    // ASSetPropFlags(object, names, set, clear): bit 1 hides names from
    // for-in, bit 2 keeps them from delete.
    define("ASSetPropFlags", native("ASSetPropFlags", [this](VM&, const Val&, Args args) -> Val {
        ObjectData* d = attach(args[0]);
        if (!d) return Val();
        std::vector<std::string> names;
        const Val& list = args[1];
        if (list.isNullish()) {
            for (const auto& [name, value] : args[0]->properties) names.push_back(name);
            for (const auto& [name, accessor] : d->accessors) names.push_back(name);
        } else if (list.isArray()) {
            for (const Val& item : list->elements) names.push_back(toString(item));
        } else {
            const std::string text = toString(list);
            size_t from = 0;
            while (from <= text.size()) {
                const size_t comma = text.find(',', from);
                names.push_back(text.substr(from, comma == std::string::npos ? std::string::npos : comma - from));
                if (comma == std::string::npos) break;
                from = comma + 1;
            }
        }
        const int set = toInt32(args[2]);
        const int clear = toInt32(args[3]);
        for (const std::string& name : names) {
            if (set & 1) {
                d->hidden.insert(name);
            } else if (clear & 1) {
                d->hidden.erase(name);
            }
            if (set & 2) {
                d->permanent.insert(name);
            } else if (clear & 2) {
                d->permanent.erase(name);
            }
        }
        return Val();
    }, 4));
    // ActionScript 1's escape keeps letters and digits only (ActionScript
    // 3's keeps @-_.*+/ too): each other byte of the text as %XX.
    vm.setGlobal("escape", native("escape", [this](VM&, const Val&, Args args) -> Val {
        static const char* hex = "0123456789ABCDEF";
        const std::string text = args.empty() ? std::string("undefined") : toString(args[0]);
        std::string out;
        for (const unsigned char c : text) {
            if (std::isalnum(c)) {
                out += static_cast<char>(c);
            } else {
                out += '%';
                out += hex[c >> 4];
                out += hex[c & 15];
            }
        }
        return Val::string(out);
    }, 1));
    define("$version", Val::string(kVersionText));
    define("getVersion", native("getVersion", [](VM&, const Val&, Args) { return Val::string(kVersionText); }));
    define("targetPath", native("targetPath", [this](VM&, const Val&, Args args) -> Val {
        if (!host.isClip(args[0])) return Val();
        return Val::string(host.targetPath(args[0]));
    }, 1));
    // A panel's way to the authoring tool holding it: MMExecute runs script
    // there, its value back as text; MMEndCommand ends a command the panel
    // was opened for, which a panel left open has none of.
    define("MMExecute", native("MMExecute", [this](VM&, const Val&, Args args) -> Val {
        return Val::string(host.mmExecute(toString(args[0])));
    }, 1));
    define("MMEndCommand", native("MMEndCommand", [](VM&, const Val&, Args) { return Val(); }, 2));
    // setInterval(function, ms, ...) and setInterval(object, "method", ms,
    // ...), on the VM's timers.
    for (const bool repeat : {true, false}) {
        const std::string name = repeat ? "setInterval" : "setTimeout";
        // In place of the VM's own, which takes only a function.
        vm.setGlobal(name, native(name, [this, repeat](VM& vm, const Val&, Args args) -> Val {
            Val function = args[0];
            size_t at = 1;
            if (!function.isObject()) return Val();
            if (!function.isFunction() || args[1].isString()) {
                const Val object = args[0];
                const std::string method = toString(args[1]);
                function = native("interval", [this, object, method](VM&, const Val&, Args given) {
                    std::vector<Val> list(given.begin(), given.end());
                    return callMethod(object, method, list);
                });
                at = 2;
            } else {
                const Val target = function;
                function = native("interval", [this, target](VM&, const Val&, Args given) {
                    std::vector<Val> list(given.begin(), given.end());
                    return callValue(target, Val(), list);
                });
            }
            std::vector<Val> list{function, Val::number(toNumber(args[at]))};
            for (size_t i = at + 1; i < args.size(); ++i) list.push_back(args[i]);
            const Val timer = vm.findDefinition(repeat ? "flash.utils.setInterval" : "flash.utils.setTimeout");
            return timer.isFunction() ? vm.callFunction(timer, Val(), list) : Val();
        }, 2));
    }
    for (const char* name : {"clearInterval", "clearTimeout"}) {
        const std::string qualified = std::string("flash.utils.") + name;
        define(name, native(name, [qualified](VM& vm, const Val&, Args args) -> Val {
            const Val clear = vm.findDefinition(qualified);
            return clear.isFunction() ? vm.callFunction(clear, Val(), std::vector<Val>{args[0]}) : Val();
        }, 1));
    }
    for (const char* name : {"MovieClip", "Button", "TextField"}) {
        define(name, native(name, [](VM&, const Val&, Args) { return Val(); }));
    }
    // Object.registerClass(linkage, constructor): clips of that symbol are
    // made of the class.
    Val objectClass;
    if (globalValue("Object", objectClass) && objectClass.isObject()) {
        objectClass->properties["registerClass"] =
            native("registerClass", [this](VM&, const Val&, Args args) -> Val {
                const std::string linkage = toString(args[0]);
                if (args[1].isFunction()) {
                    registered[linkage] = args[1];
                } else {
                    registered.erase(linkage);
                }
                return Val::boolean(true);
            }, 2);
    }
    // addProperty(name, getter, setter), and watch, which is not kept.
    objectProto->properties["addProperty"] = native("addProperty", [this](VM&, const Val& self, Args args) -> Val {
        const std::string name = toString(args[0]);
        if (name.empty() || !args[1].isFunction()) return Val::boolean(false);
        ObjectData* d = attach(self);
        if (!d) return Val::boolean(false);
        if (!d->accessors.count(name)) d->accessorOrder.emplace_back(name, self->properties.size());
        d->accessors[name] = {args[1], args[2].isFunction() ? args[2] : Val()};
        self->properties.erase(name);
        return Val::boolean(true);
    }, 3);
    objectProto->properties["watch"] = native("watch", [](VM&, const Val&, Args) { return Val::boolean(false); }, 2);
    objectProto->properties["unwatch"] = native("unwatch", [](VM&, const Val&, Args) { return Val::boolean(false); }, 1);
    for (const char* name : {"addProperty", "watch", "unwatch"}) hide(objectProto, name);
}

// -- Members ------------------------------------------------------------------

Val Machine::protoOf(const Val& value) {
    switch (value.tag) {
        case Val::Tag::String:
        case Val::Tag::Number:
        case Val::Tag::Boolean: {
            Val constructor;
            const char* name = value.isString() ? "String" : value.isNumber() ? "Number" : "Boolean";
            if (globalValue(name, constructor)) return prototypeOf(constructor);
            return objectProto;
        }
        case Val::Tag::Object:
            break;
        default:
            return Val::null();
    }
    if (ObjectData* d = data(value)) {
        if (d->protoSet) return d->proto;
    }
    if (const Val* proto = value->properties.lookup("__proto__")) return *proto;
    if (sameObject(value, objectProto)) return Val::null();
    if (host.isClip(value)) {
        Val constructor;
        if (globalValue(host.className(value), constructor) && (constructor.isFunction() || constructor.isClass())) {
            return prototypeOf(constructor);
        }
        return objectProto;
    }
    if (value->type == ValueType::Function || value->type == ValueType::NativeFunction ||
        value->type == ValueType::Class) {
        return functionProto;
    }
    if (value->cls && value->cls != vm.builtins().object) {
        const Val constructor = vm.classValue(value->cls.get());
        if (constructor.isObject()) return prototypeOf(constructor);
    }
    return objectProto;
}

Val Machine::prototypeOf(const Val& constructor) {
    if (!constructor.isObject()) return Val();
    if (const Val* found = constructor->properties.lookup("prototype")) return *found;
    if (!constructor.isFunction() && !constructor.isClass()) return Val();
    // A native class's or function's: made the first time, from its base
    // class's.
    Val base = objectProto;
    if (constructor.isClass() && constructor->cls && constructor->cls->base &&
        constructor->cls->base != vm.builtins().object) {
        base = prototypeOf(vm.classValue(constructor->cls->base.get()));
    }
    Val proto = newObject(base);
    proto->properties["constructor"] = constructor;
    hide(proto, "constructor");
    constructor->properties["prototype"] = proto;
    return proto;
}

bool Machine::vmHas(const Val& object, const std::string& name) {
    if (!object.isObject()) return false;
    Value* value = object.get();
    if (value->type == ValueType::Class && value->cls && value->cls->findStatic(name)) return true;
    for (const ClassInfo* cls = vm.classOf(object); cls; cls = cls->base.get()) {
        if (cls->findTrait(name) || cls->prototype.count(name)) return true;
    }
    return false;
}

Val Machine::vmGet(const Val& object, const std::string& name) {
    try {
        return vm.getProperty(object, name);
    } catch (const ScriptException&) {
        return Val();
    }
}

bool Machine::lookupOwn(const Val& at, const std::string& name, Val& out, const Val& receiver, bool vmMembers) {
    if (!at.isObject()) return false;
    Value* value = at.get();
    ObjectData* d = data(at);
    if (d) {
        const auto accessor = d->accessors.find(name);
        if (accessor != d->accessors.end()) {
            const std::pair<const Value*, std::string> running(receiver.isObject() ? receiver.get() : nullptr, name);
            if (runningAccessors.count(running)) {
                out = Val();
                return true;
            }
            runningAccessors.insert(running);
            struct Done {
                std::set<std::pair<const Value*, std::string>>& set;
                std::pair<const Value*, std::string> key;
                ~Done() { set.erase(key); }
            } done{runningAccessors, running};
            out = callValue(accessor->second.first, receiver, {});
            return true;
        }
    }
    size_t index = 0;
    if (value->type == ValueType::Array && isIndex(name, index)) {
        if (index < value->elements.size()) {
            out = value->elements[index];
            return true;
        }
        return false;
    }
    if (const Val* own = value->properties.lookup(name)) {
        out = *own;
        return true;
    }
    if (value->type == ValueType::Array && vmMembers) {
        const auto method = arrayMethods.find(name);
        if (method != arrayMethods.end()) {
            out = method->second;
            return true;
        }
    }
    if (value->host && !d) {
        ValuePtr found = value->host->get(name);
        if (found) {
            out = Val(found);
            return true;
        }
    }
    if (vmMembers && !d && vmHas(at, name)) {
        out = vmGet(at, name);
        return true;
    }
    if (version < 7) {
        // Before SWF 7 names are found whatever their case.
        for (const auto& [key, held] : value->properties) {
            if (sameNameIgnoringCase(key, name)) {
                out = held;
                return true;
            }
        }
        if (d) {
            for (const auto& [key, accessor] : d->accessors) {
                if (sameNameIgnoringCase(key, name)) {
                    out = callValue(accessor.first, receiver, {});
                    return true;
                }
            }
        }
    }
    return false;
}

bool Machine::lookupFrom(const Val& start, const std::string& name, Val& out, const Val& receiver, Val* holder) {
    Val at = start;
    for (int guard = 0; at.isObject() && guard < kDepthLimit; ++guard) {
        if (lookupOwn(at, name, out, receiver, true)) {
            if (holder) *holder = at;
            return true;
        }
        at = protoOf(at);
    }
    return false;
}

bool Machine::lookup(const Val& object, const std::string& name, Val& out, Val* holder) {
    if (object.isNullish()) return false;
    if (!object.isObject()) {
        // A string's, number's or boolean's: its class's, then what scripts
        // added to its prototype.
        out = vmGet(object, name);
        if (!out.isUndefined()) return true;
        return lookupFrom(protoOf(object), name, out, object, holder);
    }
    if (ObjectData* d = data(object)) {
        if (d->isSuper) return lookupFrom(protoOf(d->home), name, out, d->superThis, holder);
    }
    if (lookupFrom(object, name, out, object, holder)) return true;
    // The VM's members of an object ActionScript 1 made (Object's
    // toString...), after what its prototypes have.
    if (data(object) && vmHas(object, name)) {
        out = vmGet(object, name);
        if (holder) *holder = Val();
        return true;
    }
    return false;
}

std::string Machine::slashPath(const Val& clip) {
    std::string path = host.targetPath(clip);
    const size_t dot = path.find('.');
    if (path.rfind("_level", 0) == 0) {
        path = dot == std::string::npos ? std::string() : path.substr(dot + 1);
    }
    std::replace(path.begin(), path.end(), '.', '/');
    return "/" + path;
}

Val Machine::getMember(const Val& object, const std::string& name) {
    if (object.isNullish()) return Val();
    if (object.isObject()) {
        if (name == "__proto__") {
            if (ObjectData* d = data(object); d && d->isSuper) return protoOf(d->home);
            return protoOf(object);
        }
        if (name == "prototype" && (object.isFunction() || object.isClass())) return prototypeOf(object);
        if (name == "_target" && host.isClip(object)) return Val::string(slashPath(object));
    }
    Val out;
    if (lookup(object, name, out)) return out;
    return Val();
}

void Machine::setMember(const Val& object, const std::string& name, const Val& value) {
    if (!object.isObject()) return;
    Value* target = object.get();
    ObjectData* d = data(object);
    if (name == "__proto__") {
        if (d) {
            d->protoSet = true;
            d->proto = value;
        } else {
            target->properties["__proto__"] = value;
        }
        return;
    }
    if (d && d->isSuper) {
        setMember(d->superThis, name, value);
        return;
    }
    {
        // A setter addProperty put on it or its prototypes, called here so
        // that what it throws reaches the script.
        Val at = d ? object : protoOf(object);
        for (int guard = 0; at.isObject() && guard < kDepthLimit; ++guard) {
            if (ObjectData* pd = data(at)) {
                const auto found = pd->accessors.find(name);
                if (found != pd->accessors.end()) {
                    if (found->second.second.isFunction()) callValue(found->second.second, object, {value});
                    return;
                }
            }
            if (at->properties.count(name)) break;
            at = protoOf(at);
        }
    }
    size_t index = 0;
    if (target->type == ValueType::Array && isIndex(name, index)) {
        try {
            vm.setIndex(object, Val::number(static_cast<double>(index)), value);
        } catch (const ScriptException&) {
        }
        return;
    }
    std::string key = name;
    if (version < 7 && !target->properties.count(name)) {
        for (const auto& [own, held] : target->properties) {
            if (sameNameIgnoringCase(own, name)) {
                key = own;
                break;
            }
        }
    }
    try {
        vm.setProperty(object, key, value);
    } catch (const ScriptException&) {
        // One the VM keeps read-only, such as Math.cos: ActionScript 1 can
        // put its own over it, which is found first.
        target->properties[key] = value;
    }
}

bool Machine::deleteMember(const Val& object, const std::string& name) {
    if (!object.isObject()) return false;
    if (ObjectData* d = data(object)) {
        if (d->permanent.count(name)) return false;
        if (d->accessors.erase(name)) {
            d->accessorOrder.erase(std::remove_if(d->accessorOrder.begin(), d->accessorOrder.end(),
                                                  [&name](const auto& each) { return each.first == name; }),
                                   d->accessorOrder.end());
            return true;
        }
    }
    return object->properties.erase(name) > 0;
}

std::vector<std::string> Machine::enumerate(const Val& object) {
    // As the player does it: each object's names (the object's own, then
    // its prototypes') pushed in the order they were made, then taken off
    // the top, so the last made comes first, a prototype's before the
    // object's own, and an array's last element first.
    std::vector<std::string> pushed;
    std::set<std::string> seen;
    const auto add = [&](const std::string& name, const ObjectData* d) {
        if (name == "__proto__" || name == "__constructor__") return;
        if (d && d->hidden.count(name)) {
            seen.insert(name);
            return;
        }
        if (seen.insert(name).second) pushed.push_back(name);
    };
    Val at = object;
    for (int guard = 0; at.isObject() && guard < kDepthLimit; ++guard) {
        const ObjectData* d = data(at);
        if (at->type == ValueType::Array) {
            for (size_t i = 0; i < at->elements.size(); ++i) add(std::to_string(i), d);
        }
        if (guard == 0 && host.isClip(at)) {
            std::vector<std::string> children;
            host.childNames(at, children);
            for (const std::string& name : children) add(name, d);
        }
        if (at->host && !d) {
            std::vector<Val> keys;
            if (at->host->enumerate(keys)) {
                for (const Val& key : keys) add(toString(key), d);
            }
        }
        // The dynamic properties in order, each accessor where it was added.
        size_t placed = 0;
        size_t index = 0;
        const auto accessorsUpTo = [&](size_t count) {
            if (!d) return;
            for (; placed < d->accessorOrder.size() && d->accessorOrder[placed].second <= count; ++placed) {
                add(d->accessorOrder[placed].first, d);
            }
        };
        for (const auto& [name, value] : at->properties) {
            accessorsUpTo(index);
            add(name, d);
            ++index;
        }
        accessorsUpTo(static_cast<size_t>(-1));
        at = protoOf(at);
    }
    return std::vector<std::string>(pushed.rbegin(), pushed.rend());
}

// -- Conversions --------------------------------------------------------------

Val Machine::toPrimitive(const Val& value, bool preferString) {
    if (!value.isObject()) return value;
    if (host.isClip(value)) return Val::string(host.targetPath(value));
    if (value.isFunction() && !value.isClass()) return Val::string("[type Function]");
    for (int pass = 0; pass < 2; ++pass) {
        const bool useString = (pass == 0) == preferString;
        const Val method = getMember(value, useString ? "toString" : "valueOf");
        if (method.isFunction()) {
            const Val result = callValue(method, value, {});
            if (!result.isObject()) return result;
        }
    }
    return Val::string(value.isClass() ? "[type Function]" : "[object Object]");
}

std::string Machine::toString(const Val& value) {
    switch (value.tag) {
        case Val::Tag::Undefined: return version >= 7 ? "undefined" : "";
        case Val::Tag::Null: return "null";
        case Val::Tag::Boolean: return value.flag ? "true" : "false";
        case Val::Tag::Number: return numberText(value.num);
        case Val::Tag::String: return value.str();
        case Val::Tag::Object: break;
    }
    const Val primitive = toPrimitive(value, true);
    if (primitive.isObject()) return "[object Object]";
    return toString(primitive);
}

double Machine::toNumber(const Val& value) {
    switch (value.tag) {
        case Val::Tag::Undefined:
        case Val::Tag::Null: return version >= 7 ? NAN : 0;
        case Val::Tag::Boolean: return value.flag ? 1 : 0;
        case Val::Tag::Number: return value.num;
        case Val::Tag::String: return parseNumber(value.str());
        case Val::Tag::Object: break;
    }
    if (host.isClip(value) || (value.isFunction() && !value.isClass())) return NAN;
    const Val primitive = toPrimitive(value, false);
    if (primitive.isObject()) return NAN;
    return toNumber(primitive);
}

bool Machine::toBoolean(const Val& value) {
    switch (value.tag) {
        case Val::Tag::Undefined:
        case Val::Tag::Null: return false;
        case Val::Tag::Boolean: return value.flag;
        case Val::Tag::Number: return value.num != 0 && !std::isnan(value.num);
        case Val::Tag::String: {
            if (version >= 7) return !value.str().empty();
            const double number = parseNumber(value.str());
            return number != 0 && !std::isnan(number);
        }
        case Val::Tag::Object: return true;
    }
    return false;
}

bool Machine::looseEquals(const Val& a, const Val& b) {
    if (a.tag == b.tag) {
        if (a.isObject()) return a.ref == b.ref;
        // Two numbers that are not numbers are equal here, as the player
        // (7 and later) has them; a string that reads as NaN is not.
        if (a.isNumber() && std::isnan(a.num) && std::isnan(b.num)) return true;
        return a.strictEquals(b);
    }
    if (a.isNullish() && b.isNullish()) return true;
    if (a.isNullish() || b.isNullish()) return false;
    if (a.isBoolean()) return looseEquals(Val::number(a.flag ? 1 : 0), b);
    if (b.isBoolean()) return looseEquals(a, Val::number(b.flag ? 1 : 0));
    if (a.isNumber() && b.isString()) return a.num == parseNumber(b.str());
    if (a.isString() && b.isNumber()) return parseNumber(a.str()) == b.num;
    if (a.isObject() && !b.isObject()) {
        const Val primitive = toPrimitive(a, false);
        return !primitive.isObject() && looseEquals(primitive, b);
    }
    if (b.isObject() && !a.isObject()) {
        const Val primitive = toPrimitive(b, false);
        return !primitive.isObject() && looseEquals(a, primitive);
    }
    return false;
}

Val Machine::lessThan(const Val& a, const Val& b) {
    const Val x = toPrimitive(a, false);
    const Val y = toPrimitive(b, false);
    if (x.isString() && y.isString()) return Val::boolean(toUtf16(x.str()) < toUtf16(y.str()));
    const double p = toNumber(x);
    const double q = toNumber(y);
    if (std::isnan(p) || std::isnan(q)) return Val();
    return Val::boolean(p < q);
}

std::string Machine::typeOf(const Val& value) {
    switch (value.tag) {
        case Val::Tag::Undefined: return "undefined";
        case Val::Tag::Null: return "null";
        case Val::Tag::Boolean: return "boolean";
        case Val::Tag::Number: return "number";
        case Val::Tag::String: return "string";
        case Val::Tag::Object: break;
    }
    if (host.isClip(value)) return host.className(value) == "TextField" ? "object" : "movieclip";
    if (value.isFunction() || value.isClass()) return "function";
    return "object";
}

bool Machine::implementsInterface(const Val& proto, const Val& constructor, int depthLeft) {
    const ObjectData* d = data(proto);
    if (!d || depthLeft <= 0) return false;
    for (const Val& implemented : d->interfaces) {
        if (sameObject(implemented, constructor)) return true;
        // An interface that extends another.
        Val at = prototypeOf(implemented);
        for (int guard = 0; at.isObject() && guard < kDepthLimit; ++guard) {
            if (implementsInterface(at, constructor, depthLeft - 1)) return true;
            if (sameObject(at, prototypeOf(constructor))) return true;
            at = protoOf(at);
        }
    }
    return false;
}

bool Machine::instanceOf(const Val& value, const Val& constructor) {
    if (!value.isObject() || !(constructor.isFunction() || constructor.isClass())) return false;
    const Val proto = prototypeOf(constructor);
    Val at = protoOf(value);
    for (int guard = 0; at.isObject() && guard < kDepthLimit; ++guard) {
        if (sameObject(at, proto)) return true;
        if (implementsInterface(at, constructor, 8)) return true;
        at = protoOf(at);
    }
    if (constructor.isClass() && !host.isClip(value)) {
        try {
            return vm.isType(value, constructor);
        } catch (const ScriptException&) {
        }
    }
    return false;
}

// -- Calls --------------------------------------------------------------------

Val Machine::callValue(const Val& function, const Val& self, const std::vector<Val>& args, const Val& home) {
    if (FunctionData* f = functionData(function)) return callAvm1(*f, function, self, args, home);
    if (function.isClass() && function->cls) {
        // Conversions as the movie's version has them.
        const VM::Builtins& core = vm.builtins();
        const ClassInfo* cls = function->cls.get();
        const Val given = args.empty() ? Val() : args[0];
        if (cls == core.boolean.get()) return Val::boolean(toBoolean(given));
        if (cls == core.number.get()) return Val::number(args.empty() ? 0 : toNumber(given));
        if (cls == core.string.get()) return Val::string(args.empty() ? std::string() : toString(given));
    }
    if (function.isFunction() || function.isClass()) {
        return vm.callFunction(function, self, Args(args.data(), args.size()));
    }
    return Val();
}

Val Machine::makeFunction(FunctionData* made) {
    std::shared_ptr<Machine*> token = life;
    auto holder = std::make_shared<std::weak_ptr<Value>>();
    Val function = vm.makeFunction(made->name, [token, holder](VM& vm, const Val& self, Args args) -> Val {
        Machine* m = *token;
        ValuePtr callee = holder->lock();
        if (!m || !callee) return Val();
        FunctionData* f = m->functionData(Val(callee));
        if (!f) return Val();
        std::vector<Val> list(args.begin(), args.end());
        try {
            return m->callAvm1(*f, Val(callee), self, list, Val());
        } catch (const Halt& halt) {
            vm.throwError("Error", halt.message);
        }
    }, static_cast<int>(made->params.size()));
    vm.track(function);
    *holder = function.ref;
    return function;
}

Val Machine::superObject(const Activation& a) {
    Val object = vm.newObject();
    auto d = std::make_shared<ObjectData>(life);
    d->self = object.ref;
    d->isSuper = true;
    d->superThis = a.self;
    d->home = a.home.isObject() ? a.home : protoOf(a.self);
    object->host = d;
    attached.push_back(object.ref);
    return object;
}

Val Machine::argumentsArray(const Activation& a) {
    Val list = vm.newArray(a.args);
    list->properties["callee"] = a.callee;
    hide(list, "callee");
    return list;
}

Val Machine::callAvm1(FunctionData& function, const Val& callee, const Val& self, const std::vector<Val>& args,
                      const Val& home) {
    if (depth >= kDepthLimit) throw Halt{"256 levels of recursion were exceeded"};    struct Depth {
        int& value;
        explicit Depth(int& value) : value(value) { ++value; }
        ~Depth() { --value; }
    } guard(depth);
    Activation a;
    a.code = function.code;
    a.bytes = function.code ? function.code->data() : nullptr;
    a.start = function.start;
    a.end = function.end;
    a.pool = function.pool;
    a.version = function.version;
    a.self = self;
    // SWF 6 and later look names up where the function was defined; before,
    // on the timeline that calls it.
    if (function.version >= 6 || !current) {
        a.base = function.base;
        a.scope = function.scope;
    } else {
        a.base = current->target;
        a.scope = std::make_shared<Scope>(Scope::Target, current->target,
                                          std::make_shared<Scope>(Scope::Global, globalObject, nullptr));
    }
    a.target = a.base;
    a.inFunction = true;
    a.locals = vm.newObject();
    a.scope = std::make_shared<Scope>(Scope::Local, a.locals, a.scope);
    a.function = &function;
    a.callee = callee;
    a.args = args;
    a.home = home;
    a.where = function.where;
    if (function.two) {
        a.registers.resize(std::max<size_t>(function.registerCount, 1));
        size_t next = 1;
        const auto preload = [&](const Val& value) {
            if (next < a.registers.size()) a.registers[next] = value;
            ++next;
        };
        if (function.flags & PreloadThis) preload(self);
        if (function.flags & PreloadArguments) preload(argumentsArray(a));
        if (function.flags & PreloadSuper) preload(superObject(a));
        if (function.flags & PreloadRoot) preload(host.root(a.base));
        if (function.flags & PreloadParent) preload(host.parent(a.base));
        if (function.flags & PreloadGlobal) preload(globalObject);
    } else {
        a.registers.resize(4);
    }
    for (size_t i = 0; i < function.params.size(); ++i) {
        const Val value = i < args.size() ? args[i] : Val();
        const uint8_t reg = function.params[i].first;
        if (reg != 0 && reg < a.registers.size()) {
            a.registers[reg] = value;
        } else {
            a.locals->properties[function.params[i].second] = value;
        }
    }
    Activation* was = current;
    current = &a;
    struct Restore {
        Activation*& slot;
        Activation* was;
        ~Restore() { slot = was; }
    } restore{current, was};
    execute(a);
    return a.returned;
}

Val Machine::callMethod(const Val& object, const std::string& name, const std::vector<Val>& args) {
    if (ObjectData* d = data(object); d && d->isSuper) {
        if (name.empty()) {
            // super(...): the constructor the class extends, on this.
            Val constructor;
            lookupFrom(d->home, "__constructor__", constructor, d->superThis, nullptr);
            return callValue(constructor, d->superThis, args, prototypeOf(constructor));
        }
        Val method;
        Val holder;
        if (!lookupFrom(protoOf(d->home), name, method, d->superThis, &holder)) return Val();
        return callValue(method, d->superThis, args, holder);
    }
    if (name.empty()) return callValue(object, Val(), args);
    if (object.isNullish()) return Val();
    Val method;
    Val holder;
    if (!lookup(object, name, method, &holder) || !(method.isFunction() || method.isClass())) return Val();
    return callValue(method, object, args, holder.isObject() && !sameObject(holder, object) ? holder : protoOf(object));
}

Val Machine::construct(const Val& constructor, const std::vector<Val>& args) {
    if (FunctionData* f = functionData(constructor)) {
        const Val proto = prototypeOf(constructor);
        Val made = newObject(proto.isObject() ? proto : objectProto);
        made->properties["__constructor__"] = constructor;
        hide(made, "__constructor__");
        if (version < 7) {
            made->properties["constructor"] = constructor;
            hide(made, "constructor");
        }
        const Val result = callAvm1(*f, constructor, made, args, proto);
        if (result.isObject() && result.ref == noObject.ref) return Val();
        return result.isObject() ? result : made;
    }
    Val objectClass;
    if (globalValue("Object", objectClass) && sameObject(constructor, objectClass)) {
        if (!args.empty() && args[0].isObject()) return args[0];
        return newObject(objectProto);
    }
    if (constructor.isFunction() || constructor.isClass()) {
        return vm.construct(constructor, Args(args.data(), args.size()));
    }
    return Val();
}

// -- Variables ----------------------------------------------------------------

bool Machine::resolveTarget(Activation& a, const Val& start, const std::string& path, Val& out) {
    Val at = start;
    size_t i = 0;
    if (path.empty()) {
        out = start;
        return start.isObject();
    }
    if (path[0] == '/') {
        at = host.root(start.isObject() ? start : a.base);
        i = 1;
    }
    bool first = true;
    while (i < path.size()) {
        if (path.compare(i, 2, "..") == 0 && (i + 2 == path.size() || path[i + 2] == '/')) {
            at = host.parent(at);
            if (!at.isObject()) return false;
            i += 3;
            first = false;
            continue;
        }
        const size_t stop = path.find_first_of("/.:", i);
        const std::string part = path.substr(i, stop == std::string::npos ? std::string::npos : stop - i);
        i = stop == std::string::npos ? path.size() : stop + 1;
        if (part.empty()) continue;
        Val next;
        if (part == "_root") {
            next = host.root(at.isObject() ? at : a.base);
        } else if (part == "_parent") {
            next = host.parent(at);
        } else if (part == "this") {
            next = first ? a.self : at;
            if (!next.isObject()) next = at;
        } else if (part == "_global") {
            next = globalObject;
        } else if (part.rfind("_level", 0) == 0 && part.size() > 6 &&
                   std::all_of(part.begin() + 6, part.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            next = host.level(std::atoi(part.c_str() + 6));
        } else {
            if (host.isClip(at)) next = host.child(at, part);
            if (!next.isObject()) next = getMember(at, part);
            if (!next.isObject() && first) findVariable(a, part, next, nullptr);
        }
        if (!next.isObject()) return false;
        at = next;
        first = false;
    }
    out = at;
    return at.isObject();
}

Val Machine::targetOf(Activation& a, const Val& target) {
    if (target.isObject()) return target;
    const std::string path = toString(target);
    if (path.empty()) return a.target.isObject() ? a.target : a.base;
    Val found;
    if (resolveTarget(a, a.target.isObject() ? a.target : a.base, path, found)) return found;
    return Val();
}

bool Machine::findVariable(Activation& a, const std::string& name, Val& out, Val* self) {
    if (self) *self = Val();
    if (name == "this") {
        out = a.self;
        return true;
    }
    if (name == "_global") {
        out = globalObject;
        return true;
    }
    if (name == "_root" || name == "_level0") {
        out = name == "_root" ? host.root(a.target.isObject() ? a.target : a.base) : host.level(0);
        return true;
    }
    if (a.inFunction && a.function) {
        const uint16_t flags = a.function->two ? a.function->flags : 0;
        if (name == "arguments" && !(flags & (PreloadArguments | SuppressArguments)) &&
            !a.locals->properties.count(name)) {
            a.locals->properties[name] = argumentsArray(a);
        } else if (name == "super" && !(flags & (PreloadSuper | SuppressSuper)) && !a.locals->properties.count(name)) {
            a.locals->properties[name] = superObject(a);
        }
    }
    for (const Scope* scope = a.scope.get(); scope; scope = scope->parent.get()) {
        switch (scope->kind) {
            case Scope::Global:
                if (globalValue(name, out)) return true;
                break;
            case Scope::Local:
                if (const Val* found = scope->object->properties.lookup(name)) {
                    out = *found;
                    return true;
                }
                break;
            default:
                if (lookup(scope->object, name, out)) {
                    if (self) *self = scope->object;
                    return true;
                }
                break;
        }
    }
    if (name.rfind("_level", 0) == 0 && name.size() > 6) {
        out = host.level(std::atoi(name.c_str() + 6));
        return out.isObject();
    }
    return false;
}

Val Machine::getVariable(Activation& a, const std::string& name) {
    if (name.find_first_of("/:.") != std::string::npos) {
        const Val start = a.target.isObject() ? a.target : a.base;
        const size_t split = variableSplit(name);
        if (split != std::string::npos) {
            Val object;
            if (resolveTarget(a, start, name.substr(0, split), object)) return getMember(object, name.substr(split + 1));
            return Val();
        }
        Val object;
        if (resolveTarget(a, start, name, object)) return object;
        return Val();
    }
    Val out;
    if (findVariable(a, name, out, nullptr)) return out;
    return Val();
}

void Machine::setVariable(Activation& a, const std::string& name, const Val& value) {
    const size_t split = variableSplit(name);
    if (split != std::string::npos) {
        const Val start = a.target.isObject() ? a.target : a.base;
        Val object;
        if (resolveTarget(a, start, name.substr(0, split), object)) setMember(object, name.substr(split + 1), value);
        return;
    }
    for (const Scope* scope = a.scope.get(); scope; scope = scope->parent.get()) {
        switch (scope->kind) {
            case Scope::Global:
                break;
            case Scope::Local:
                if (scope->object->properties.count(name)) {
                    scope->object->properties[name] = value;
                    return;
                }
                break;
            case Scope::With: {
                Val ignored;
                if (lookup(scope->object, name, ignored)) {
                    setMember(scope->object, name, value);
                    return;
                }
                break;
            }
            case Scope::Target:
                setMember(scope->object, name, value);
                return;
        }
    }
    setMember(a.base, name, value);
}

void Machine::defineLocal(Activation& a, const std::string& name, const Val& value, bool onlyIfMissing) {
    if (a.inFunction) {
        if (!onlyIfMissing || !a.locals->properties.count(name)) a.locals->properties[name] = value;
        return;
    }
    // Outside a function: on the timeline (tellTarget's, if it changed it).
    for (const Scope* scope = a.scope.get(); scope; scope = scope->parent.get()) {
        if (scope->kind == Scope::Target) {
            Val existing;
            if (!onlyIfMissing || !lookup(scope->object, name, existing)) setMember(scope->object, name, value);
            return;
        }
    }
}

bool Machine::deleteVariable(Activation& a, const std::string& name) {
    for (const Scope* scope = a.scope.get(); scope; scope = scope->parent.get()) {
        if (scope->kind == Scope::Global) {
            continue;
        }
        if (deleteMember(scope->object, name)) return true;
    }
    return false;
}

void Machine::setTarget(Activation& a, const Val& clip) {
    if (!a.targetSet) {
        a.beforeTarget = a.scope;
        a.targetSet = true;
    }
    if (!clip.isObject() || sameObject(clip, a.base)) {
        a.target = clip.isObject() ? clip : Val();
        a.scope = a.beforeTarget;
        a.targetSet = false;
        if (!clip.isObject() && !a.target.isObject()) a.target = Val();
    } else {
        a.target = clip;
        a.scope = std::make_shared<Scope>(Scope::Target, clip, a.beforeTarget);
    }
    host.targetChanged(a.target.isObject() ? a.target : a.base);
}

// -- Running ------------------------------------------------------------------

void Machine::report(const std::string& where, const std::string& message) {
    host.error(where.empty() ? message : where + ": " + message);
}

bool Machine::run(const Actions& actions, const Val& clip, const std::string& where) {
    if (!actions || actions->empty()) return true;
    if (runs == 0 && depth == 0) steps = 0;
    ++runs;
    struct Runs {
        int& value;
        ~Runs() { --value; }
    } count{runs};
    Activation a;
    a.code = actions;
    a.bytes = actions->data();
    a.start = 0;
    a.end = actions->size();
    a.pool = std::make_shared<const std::vector<std::string>>();
    a.self = clip;
    a.target = clip;
    a.base = clip;
    a.registers.resize(4);
    a.scope = std::make_shared<Scope>(Scope::Target, clip, std::make_shared<Scope>(Scope::Global, globalObject, nullptr));
    a.version = version;
    a.where = where;
    Activation* was = current;
    current = &a;
    bool ok = true;
    try {
        execute(a);
    } catch (const ScriptException& thrown) {
        std::string text;
        try {
            text = toString(thrown.value);
        } catch (...) {
            text = "an error";
        }
        report(where, "uncaught exception: " + text);
        ok = false;
    } catch (const Halt& halt) {
        report(where, halt.message);
        ok = false;
    } catch (const std::exception& failed) {
        report(where, failed.what());
        ok = false;
    }
    current = was;
    if (a.targetSet) host.targetChanged(a.base);
    return ok;
}

bool Machine::unwindReturn(Activation& a, size_t& pc) {
    while (!a.blocks.empty()) {
        Block block = std::move(a.blocks.back());
        a.blocks.pop_back();
        if (block.kind == Block::WithBlock) {
            a.scope = block.saved;
            continue;
        }
        if ((block.kind == Block::TryBlock || block.kind == Block::CatchBlock) && block.hasFinally) {
            Block finally = block;
            finally.kind = Block::FinallyBlock;
            finally.start = block.finallyStart;
            finally.end = block.finallyEnd;
            finally.after = Block::Return;
            finally.pending = a.returned;
            a.blocks.push_back(std::move(finally));
            pc = block.finallyStart;
            return true;
        }
    }
    return false;
}

bool Machine::handleThrow(Activation& a, const Val& value, size_t& pc) {
    while (!a.blocks.empty()) {
        Block block = std::move(a.blocks.back());
        a.blocks.pop_back();
        if (block.kind == Block::WithBlock) {
            a.scope = block.saved;
            continue;
        }
        if (block.kind == Block::TryBlock && block.hasCatch) {
            if (block.inRegister) {
                if (block.reg < a.registers.size()) a.registers[block.reg] = value;
            } else {
                defineLocal(a, block.name, value, false);
            }
            Block caught = block;
            caught.kind = Block::CatchBlock;
            caught.start = block.catchStart;
            caught.end = block.catchEnd;
            a.blocks.push_back(std::move(caught));
            pc = block.catchStart;
            return true;
        }
        if ((block.kind == Block::TryBlock || block.kind == Block::CatchBlock) && block.hasFinally) {
            Block finally = block;
            finally.kind = Block::FinallyBlock;
            finally.start = block.finallyStart;
            finally.end = block.finallyEnd;
            finally.after = Block::Rethrow;
            finally.pending = value;
            a.blocks.push_back(std::move(finally));
            pc = block.finallyStart;
            return true;
        }
    }
    return false;
}

namespace {

uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

std::string readString(const uint8_t* p, size_t& at, size_t end) {
    const size_t from = at;
    while (at < end && p[at]) ++at;
    std::string out(reinterpret_cast<const char*>(p + from), at - from);
    if (at < end) ++at;
    return out;
}

// The arguments a call pops: a count, then the first argument.
std::vector<Val> popArgs(std::vector<Val>& stack, Machine& m) {
    Val count;
    if (!stack.empty()) {
        count = std::move(stack.back());
        stack.pop_back();
    }
    double n = m.toNumber(count);
    if (std::isnan(n) || n < 0) n = 0;
    const size_t wanted = std::min(static_cast<size_t>(n), stack.size());
    std::vector<Val> args;
    args.reserve(wanted);
    for (size_t i = 0; i < wanted; ++i) {
        args.push_back(std::move(stack.back()));
        stack.pop_back();
    }
    return args;
}

} // namespace

// Each kind of action has a function of its own, kept out of execute: a
// call's actions run in an execute of their own, so what execute keeps on
// the C++ stack is what each level of calls costs.
void Machine::execute(Activation& a) {
    const uint8_t* p = a.bytes;
    if (!p) return;
    const size_t end = a.end;
    size_t pc = a.start;
    while (true) {
        // Leaving blocks: a with block's scope goes; a try or catch block
        // run to its end goes on to its finally block.
        if (!a.blocks.empty() && !leaveBlocks(a, pc)) return;
        if (pc < a.start || pc >= end) return;
        step();
        const uint8_t op = p[pc];
        size_t start = pc + 1;
        size_t next = start;
        if (op >= 0x80) {
            if (pc + 3 > end) return;
            start = pc + 3;
            next = start + readU16(p + pc + 1);
            if (next > end) return;
        }
        pc = next;
        try {
            bool goOn = true;
            switch (op) {
                case End:
                    return;
                case ConstantPool:
                case Push:
                case Pop:
                case PushDuplicate:
                case StackSwap:
                case StoreRegister:
                    goOn = opStack(a, op, start, next);
                    break;
                case Jump:
                case If:
                case Return:
                case Throw:
                case With:
                case Try:
                    goOn = opFlow(a, op, start, next, pc);
                    break;
                case GetVariable:
                case SetVariable:
                case DefineLocal:
                case DefineLocal2:
                case Delete:
                case Delete2:
                case GetMember:
                case SetMember:
                case InitArray:
                case InitObject:
                case TypeOf:
                case InstanceOf:
                case CastOp:
                case ImplementsOp:
                case Extends:
                case Enumerate:
                case Enumerate2:
                case TargetPath:
                    opObjects(a, op);
                    break;
                case DefineFunction:
                case DefineFunction2:
                    opDefine(a, op, start, next, pc);
                    break;
                case CallFunction:
                case CallMethod:
                case NewObject:
                case NewMethod:
                    opCall(a, op);
                    break;
                case Play:
                case Stop:
                case NextFrame:
                case PrevFrame:
                case GotoFrame:
                case GotoLabel:
                case GotoFrame2:
                case SetTarget:
                case SetTarget2:
                case WaitForFrame:
                case WaitForFrame2:
                case Call:
                case GetProperty:
                case SetProperty:
                case CloneSprite:
                case RemoveSprite:
                case StartDrag:
                case EndDrag:
                case GetURL:
                case GetURL2:
                case ToggleQuality:
                case StopSounds:
                case Trace:
                case GetTime:
                case RandomNumber:
                    opMovie(a, op, start, next, pc);
                    break;
                default:
                    if (op < 0x80) {
                        opMath(a, op);
                    }
                    // An action the player does not know: passed over.
                    break;
            }
            if (!goOn) return;
        } catch (const ScriptException& thrown) {
            if (!handleThrow(a, thrown.value, pc)) throw;
        }
    }
}

bool Machine::leaveBlocks(Activation& a, size_t& pc) {
    while (!a.blocks.empty()) {
        Block& inner = a.blocks.back();
        if (pc >= inner.start && pc < inner.end) break;
        Block done = std::move(inner);
        a.blocks.pop_back();
        if (done.kind == Block::WithBlock) {
            a.scope = done.saved;
        } else if ((done.kind == Block::TryBlock || done.kind == Block::CatchBlock) && pc == done.end &&
                   done.hasFinally) {
            Block finally = done;
            finally.kind = Block::FinallyBlock;
            finally.start = done.finallyStart;
            finally.end = done.finallyEnd;
            finally.after = Block::Continue;
            a.blocks.push_back(std::move(finally));
            pc = done.finallyStart;
        } else if (done.kind == Block::TryBlock && pc == done.end && done.hasCatch) {
            pc = done.catchEnd;
        } else if (done.kind == Block::FinallyBlock) {
            if (done.after == Block::Rethrow) {
                if (!handleThrow(a, done.pending, pc)) throw ScriptException{done.pending};
            } else if (done.after == Block::Return) {
                a.returned = done.pending;
                if (!unwindReturn(a, pc)) return false;
            }
        }
    }
    return true;
}

Val Machine::pop(Activation& a) {
    if (a.stack.empty()) return Val();
    Val v = std::move(a.stack.back());
    a.stack.pop_back();
    return v;
}

bool Machine::opStack(Activation& a, uint8_t op, size_t start, size_t next) {
    const uint8_t* p = a.bytes;
    std::vector<Val>& stack = a.stack;
    switch (op) {
        case ConstantPool: {
            auto pool = std::make_shared<std::vector<std::string>>();
            if (next >= start + 2) {
                const size_t count = readU16(p + start);
                size_t at = start + 2;
                for (size_t i = 0; i < count && at < next; ++i) pool->push_back(readString(p, at, next));
            }
            a.pool = pool;
            break;
        }
        case Push: {
            size_t at = start;
            while (at < next) {
                const uint8_t type = p[at++];
                switch (type) {
                    case PushString: stack.push_back(Val::string(readString(p, at, next))); break;
                    case PushFloat: {
                        if (at + 4 > next) return false;
                        uint32_t bits = 0;
                        for (int i = 0; i < 4; ++i) bits |= static_cast<uint32_t>(p[at++]) << (8 * i);
                        float f = 0;
                        std::memcpy(&f, &bits, sizeof(f));
                        stack.push_back(Val::number(f));
                        break;
                    }
                    case PushNull: stack.push_back(Val::null()); break;
                    case PushUndefined: stack.push_back(Val()); break;
                    case PushRegister: {
                        if (at >= next) return false;
                        const uint8_t r = p[at++];
                        stack.push_back(r < a.registers.size() ? a.registers[r] : Val());
                        break;
                    }
                    case PushBoolean: {
                        if (at >= next) return false;
                        stack.push_back(Val::boolean(p[at++] != 0));
                        break;
                    }
                    case PushDouble: {
                        if (at + 8 > next) return false;
                        uint32_t high = 0, low = 0;
                        for (int i = 0; i < 4; ++i) high |= static_cast<uint32_t>(p[at++]) << (8 * i);
                        for (int i = 0; i < 4; ++i) low |= static_cast<uint32_t>(p[at++]) << (8 * i);
                        const uint64_t bits = (static_cast<uint64_t>(high) << 32) | low;
                        double d = 0;
                        std::memcpy(&d, &bits, sizeof(d));
                        stack.push_back(Val::number(d));
                        break;
                    }
                    case PushInteger: {
                        if (at + 4 > next) return false;
                        uint32_t bits = 0;
                        for (int i = 0; i < 4; ++i) bits |= static_cast<uint32_t>(p[at++]) << (8 * i);
                        stack.push_back(Val::number(static_cast<int32_t>(bits)));
                        break;
                    }
                    case PushConstant8:
                    case PushConstant16: {
                        size_t index = 0;
                        if (type == PushConstant8) {
                            if (at >= next) return false;
                            index = p[at++];
                        } else {
                            if (at + 2 > next) return false;
                            index = readU16(p + at);
                            at += 2;
                        }
                        stack.push_back(index < a.pool->size() ? Val::string((*a.pool)[index]) : Val());
                        break;
                    }
                    default:
                        at = next;
                        break;
                }
            }
            break;
        }
        case Pop: pop(a); break;
        case PushDuplicate: stack.push_back(stack.empty() ? Val() : stack.back()); break;
        case StackSwap: {
            Val b = pop(a);
            Val c = pop(a);
            stack.push_back(std::move(b));
            stack.push_back(std::move(c));
            break;
        }
        case StoreRegister: {
            if (start < next) {
                const uint8_t r = p[start];
                if (r < a.registers.size()) a.registers[r] = stack.empty() ? Val() : stack.back();
            }
            break;
        }
        default: break;
    }
    return true;
}

void Machine::opMath(Activation& a, uint8_t op) {
    std::vector<Val>& stack = a.stack;
    switch (op) {
        case Add: {
            const double b = toNumber(pop(a));
            stack.push_back(Val::number(toNumber(pop(a)) + b));
            break;
        }
        case Add2: {
            const Val b = toPrimitive(pop(a), false);
            const Val c = toPrimitive(pop(a), false);
            if (c.isString() || b.isString()) {
                stack.push_back(Val::string(toString(c) + toString(b)));
            } else {
                stack.push_back(Val::number(toNumber(c) + toNumber(b)));
            }
            break;
        }
        case Subtract: {
            const double b = toNumber(pop(a));
            stack.push_back(Val::number(toNumber(pop(a)) - b));
            break;
        }
        case Multiply: {
            const double b = toNumber(pop(a));
            stack.push_back(Val::number(toNumber(pop(a)) * b));
            break;
        }
        case Divide: {
            const double b = toNumber(pop(a));
            const double c = toNumber(pop(a));
            if (b == 0 && a.version < 5) {
                stack.push_back(Val::string("#ERROR#"));
            } else {
                stack.push_back(Val::number(c / b));
            }
            break;
        }
        case Modulo: {
            const double b = toNumber(pop(a));
            stack.push_back(Val::number(std::fmod(toNumber(pop(a)), b)));
            break;
        }
        case Increment: stack.push_back(Val::number(toNumber(pop(a)) + 1)); break;
        case Decrement: stack.push_back(Val::number(toNumber(pop(a)) - 1)); break;
        case BitAnd:
        case BitOr:
        case BitXor:
        case BitLShift:
        case BitRShift:
        case BitURShift: {
            const int32_t b = toInt32(pop(a));
            const int32_t c = toInt32(pop(a));
            double r = 0;
            switch (op) {
                case BitAnd: r = c & b; break;
                case BitOr: r = c | b; break;
                case BitXor: r = c ^ b; break;
                case BitLShift: r = static_cast<int32_t>(static_cast<uint32_t>(c) << (b & 31)); break;
                case BitRShift: r = c >> (b & 31); break;
                default: r = static_cast<uint32_t>(c) >> (b & 31); break;
            }
            stack.push_back(Val::number(r));
            break;
        }
        case ToInteger: {
            const double d = toNumber(pop(a));
            stack.push_back(Val::number(std::isnan(d) ? 0.0 : std::trunc(d)));
            break;
        }
        case ToNumber: stack.push_back(Val::number(toNumber(pop(a)))); break;
        case ToString: stack.push_back(Val::string(toString(pop(a)))); break;
        case Equals: {
            const double b = toNumber(pop(a));
            stack.push_back(result(toNumber(pop(a)) == b, a.version));
            break;
        }
        case Less: {
            const double b = toNumber(pop(a));
            stack.push_back(result(toNumber(pop(a)) < b, a.version));
            break;
        }
        case And: {
            const bool b = toBoolean(pop(a));
            const bool c = toBoolean(pop(a));
            stack.push_back(result(b && c, a.version));
            break;
        }
        case Or: {
            const bool b = toBoolean(pop(a));
            const bool c = toBoolean(pop(a));
            stack.push_back(result(b || c, a.version));
            break;
        }
        case Not: stack.push_back(result(!toBoolean(pop(a)), a.version)); break;
        case Equals2: {
            const Val b = pop(a);
            stack.push_back(Val::boolean(looseEquals(pop(a), b)));
            break;
        }
        case StrictEquals: {
            const Val b = pop(a);
            const Val c = pop(a);
            stack.push_back(Val::boolean(c.isObject() ? sameObject(c, b) : c.strictEquals(b)));
            break;
        }
        case Less2: {
            const Val b = pop(a);
            stack.push_back(lessThan(pop(a), b));
            break;
        }
        case Greater: {
            const Val b = pop(a);
            const Val c = pop(a);
            stack.push_back(lessThan(b, c));
            break;
        }
        case StringEquals: {
            const std::string b = toString(pop(a));
            stack.push_back(result(toString(pop(a)) == b, a.version));
            break;
        }
        case StringLess: {
            const std::u16string b = toUtf16(toString(pop(a)));
            stack.push_back(result(toUtf16(toString(pop(a))) < b, a.version));
            break;
        }
        case StringGreater: {
            const std::u16string b = toUtf16(toString(pop(a)));
            stack.push_back(result(toUtf16(toString(pop(a))) > b, a.version));
            break;
        }
        case StringAdd: {
            const std::string b = toString(pop(a));
            stack.push_back(Val::string(toString(pop(a)) + b));
            break;
        }
        case StringLength:
        case MBStringLength: {
            const std::string text = toString(pop(a));
            const bool units = op == MBStringLength || a.version >= 6;
            stack.push_back(Val::number(static_cast<double>(units ? toUtf16(text).size() : text.size())));
            break;
        }
        case StringExtract:
        case MBStringExtract: {
            const double count = toNumber(pop(a));
            const double index = toNumber(pop(a));
            const std::u16string text = toUtf16(toString(pop(a)));
            size_t from = std::isnan(index) || index < 1 ? 0 : static_cast<size_t>(index) - 1;
            from = std::min(from, text.size());
            const size_t length = std::isnan(count) || count < 0 ? text.size() - from
                                                                 : std::min(static_cast<size_t>(count), text.size() - from);
            stack.push_back(Val::string(fromUtf16(text.substr(from, length))));
            break;
        }
        case CharToAscii:
        case MBCharToAscii: {
            const std::u16string text = toUtf16(toString(pop(a)));
            if (text.empty()) {
                stack.push_back(Val::number(0));
            } else {
                uint32_t code = text[0];
                if (op == MBCharToAscii && code >= 0xD800 && code < 0xDC00 && text.size() > 1) {
                    code = 0x10000 + ((code - 0xD800) << 10) + (text[1] - 0xDC00);
                }
                stack.push_back(Val::number(code));
            }
            break;
        }
        case AsciiToChar:
        case MBAsciiToChar: {
            const double code = toNumber(pop(a));
            std::string out;
            if (!std::isnan(code) && code > 0) appendUtf8(out, static_cast<uint32_t>(code) & 0x1FFFFF);
            stack.push_back(Val::string(out));
            break;
        }
        default: break;
    }
}

bool Machine::opFlow(Activation& a, uint8_t op, size_t start, size_t next, size_t& pc) {
    const uint8_t* p = a.bytes;
    switch (op) {
        case Jump:
            if (start + 2 <= next) {
                pc = static_cast<size_t>(static_cast<long long>(next) + static_cast<int16_t>(readU16(p + start)));
            }
            break;
        case If: {
            const bool taken = toBoolean(pop(a));
            if (taken && start + 2 <= next) {
                pc = static_cast<size_t>(static_cast<long long>(next) + static_cast<int16_t>(readU16(p + start)));
            }
            break;
        }
        case Return:
            a.returned = pop(a);
            return unwindReturn(a, pc);
        case Throw: throw ScriptException{pop(a)};
        case With: {
            const size_t size = start + 2 <= next ? readU16(p + start) : 0;
            Val object = pop(a);
            if (object.isNullish()) object = newObject(objectProto);
            Block block;
            block.kind = Block::WithBlock;
            block.start = next;
            block.end = std::min(next + size, a.end);
            block.saved = a.scope;
            a.blocks.push_back(std::move(block));
            a.scope = std::make_shared<Scope>(Scope::With, object, a.scope);
            break;
        }
        case Try: {
            if (start + 7 > next) break;
            size_t at = start;
            const uint8_t flags = p[at++];
            const size_t trySize = readU16(p + at);
            const size_t catchSize = readU16(p + at + 2);
            const size_t finallySize = readU16(p + at + 4);
            at += 6;
            Block block;
            block.kind = Block::TryBlock;
            block.start = next;
            block.end = next + trySize;
            block.hasCatch = (flags & 0x01) != 0;
            block.hasFinally = (flags & 0x02) != 0;
            block.inRegister = (flags & 0x04) != 0;
            if (block.inRegister) {
                block.reg = at < next ? p[at] : 0;
            } else {
                block.name = readString(p, at, next);
            }
            block.catchStart = block.end;
            block.catchEnd = block.catchStart + catchSize;
            block.finallyStart = block.catchEnd;
            block.finallyEnd = block.finallyStart + finallySize;
            a.blocks.push_back(std::move(block));
            break;
        }
        default: break;
    }
    return true;
}

void Machine::opObjects(Activation& a, uint8_t op) {
    std::vector<Val>& stack = a.stack;
    switch (op) {
        case GetVariable: stack.push_back(getVariable(a, toString(pop(a)))); break;
        case SetVariable: {
            const Val value = pop(a);
            setVariable(a, toString(pop(a)), value);
            break;
        }
        case DefineLocal: {
            const Val value = pop(a);
            defineLocal(a, toString(pop(a)), value, false);
            break;
        }
        case DefineLocal2: defineLocal(a, toString(pop(a)), Val(), true); break;
        case Delete: {
            const std::string name = toString(pop(a));
            const Val object = pop(a);
            stack.push_back(Val::boolean(deleteMember(object, name)));
            break;
        }
        case Delete2: stack.push_back(Val::boolean(deleteVariable(a, toString(pop(a))))); break;
        case GetMember: {
            const Val key = pop(a);
            const Val object = pop(a);
            stack.push_back(getMember(object, key.isString() ? key.str() : toString(key)));
            break;
        }
        case SetMember: {
            const Val value = pop(a);
            const Val key = pop(a);
            const Val object = pop(a);
            setMember(object, key.isString() ? key.str() : toString(key), value);
            break;
        }
        case InitArray: {
            double n = toNumber(pop(a));
            if (std::isnan(n) || n < 0) n = 0;
            std::vector<Val> elements;
            for (size_t i = 0; i < static_cast<size_t>(n) && !stack.empty(); ++i) elements.push_back(pop(a));
            stack.push_back(vm.newArray(std::move(elements)));
            break;
        }
        case InitObject: {
            double n = toNumber(pop(a));
            if (std::isnan(n) || n < 0) n = 0;
            Val object = newObject(objectProto);
            for (size_t i = 0; i < static_cast<size_t>(n) && !stack.empty(); ++i) {
                const Val value = pop(a);
                setMember(object, toString(pop(a)), value);
            }
            stack.push_back(object);
            break;
        }
        case TypeOf: stack.push_back(Val::string(typeOf(pop(a)))); break;
        case InstanceOf: {
            const Val constructor = pop(a);
            const Val object = pop(a);
            stack.push_back(Val::boolean(instanceOf(object, constructor)));
            break;
        }
        case CastOp: {
            const Val object = pop(a);
            const Val constructor = pop(a);
            stack.push_back(instanceOf(object, constructor) ? object : Val::null());
            break;
        }
        case ImplementsOp: {
            const Val constructor = pop(a);
            double n = toNumber(pop(a));
            if (std::isnan(n) || n < 0) n = 0;
            ObjectData* d = attach(prototypeOf(constructor));
            for (size_t i = 0; i < static_cast<size_t>(n) && !stack.empty(); ++i) {
                const Val implemented = pop(a);
                if (d) d->interfaces.push_back(implemented);
            }
            break;
        }
        case Extends: {
            const Val superClass = pop(a);
            const Val subClass = pop(a);
            if (!subClass.isObject()) break;
            const Val superProto = prototypeOf(superClass);
            Val proto = newObject(superProto.isObject() ? superProto : objectProto);
            proto->properties["__constructor__"] = superClass;
            hide(proto, "__constructor__");
            subClass->properties["prototype"] = proto;
            break;
        }
        case Enumerate:
        case Enumerate2: {
            Val object = pop(a);
            if (op == Enumerate) object = getVariable(a, toString(object));
            stack.push_back(Val::null());
            if (object.isObject()) {
                const std::vector<std::string> names = enumerate(object);
                for (auto name = names.rbegin(); name != names.rend(); ++name) stack.push_back(Val::string(*name));
            }
            break;
        }
        case TargetPath: {
            const Val clip = pop(a);
            stack.push_back(host.isClip(clip) ? Val::string(host.targetPath(clip)) : Val());
            break;
        }
        default: break;
    }
}

void Machine::opDefine(Activation& a, uint8_t op, size_t start, size_t next, size_t& pc) {
    const uint8_t* p = a.bytes;
    size_t at = start;
    auto made = std::make_shared<FunctionData>(life);
    made->name = readString(p, at, next);
    if (at + 2 > next) return;
    const size_t count = readU16(p + at);
    at += 2;
    if (op == DefineFunction2) {
        if (at + 3 > next) return;
        made->two = true;
        made->registerCount = p[at++];
        made->flags = readU16(p + at);
        at += 2;
        for (size_t i = 0; i < count && at < next; ++i) {
            const uint8_t reg = p[at++];
            made->params.emplace_back(reg, readString(p, at, next));
        }
    } else {
        for (size_t i = 0; i < count && at < next; ++i) made->params.emplace_back(0, readString(p, at, next));
    }
    if (at + 2 > next) return;
    const size_t size = readU16(p + at);
    made->code = a.code;
    made->start = next;
    made->end = std::min(next + size, a.end);
    made->scope = a.scope;
    made->pool = a.pool;
    made->base = a.target.isObject() ? a.target : a.base;
    made->version = a.version;
    made->where = a.where;
    pc = made->end;
    Val function = makeFunction(made.get());
    made->self = function.ref;
    function->host = made;
    attached.push_back(function.ref);
    Val proto = newObject(objectProto);
    proto->properties["constructor"] = function;
    hide(proto, "constructor");
    function->properties["prototype"] = proto;
    if (made->name.empty()) {
        a.stack.push_back(function);
    } else {
        defineLocal(a, made->name, function, false);
    }
}

void Machine::opCall(Activation& a, uint8_t op) {
    const Val name = pop(a);
    switch (op) {
        case CallFunction: {
            const std::string text = toString(name);
            const std::vector<Val> args = popArgs(a.stack, *this);
            Val function;
            Val self;
            if (text.find_first_of(":/.") != std::string::npos) {
                function = getVariable(a, text);
            } else {
                findVariable(a, text, function, &self);
            }
            if (!self.isObject()) self = a.base;
            a.stack.push_back(function.isFunction() || function.isClass() ? callValue(function, self, args) : Val());
            break;
        }
        case CallMethod: {
            const Val object = pop(a);
            const std::vector<Val> args = popArgs(a.stack, *this);
            a.stack.push_back(callMethod(object, name.isUndefined() ? std::string() : toString(name), args));
            break;
        }
        case NewObject: {
            const std::vector<Val> args = popArgs(a.stack, *this);
            a.stack.push_back(construct(getVariable(a, toString(name)), args));
            break;
        }
        default: {
            const Val object = pop(a);
            const std::vector<Val> args = popArgs(a.stack, *this);
            const std::string method = name.isUndefined() ? std::string() : toString(name);
            a.stack.push_back(construct(method.empty() ? object : getMember(object, method), args));
            break;
        }
    }
}

void Machine::opMovie(Activation& a, uint8_t op, size_t start, size_t next, size_t& pc) {
    const uint8_t* p = a.bytes;
    std::vector<Val>& stack = a.stack;
    const Val clip = a.target.isObject() ? a.target : Val();
    switch (op) {
        case Play:
            if (clip.isObject()) host.setPlaying(clip, true);
            break;
        case Stop:
            if (clip.isObject()) host.setPlaying(clip, false);
            break;
        case NextFrame:
            if (clip.isObject()) host.nextFrame(clip);
            break;
        case PrevFrame:
            if (clip.isObject()) host.prevFrame(clip);
            break;
        case GotoFrame:
            if (start + 2 <= next && clip.isObject()) host.gotoFrame(clip, readU16(p + start), false);
            break;
        case GotoLabel: {
            size_t at = start;
            const std::string label = readString(p, at, next);
            if (clip.isObject()) host.gotoLabel(clip, label, false);
            break;
        }
        case GotoFrame2: {
            const uint8_t flags = start < next ? p[start] : 0;
            const int bias = (flags & 0x02) && start + 3 <= next ? readU16(p + start + 1) : 0;
            const bool play = (flags & 0x01) != 0;
            const Val frame = pop(a);
            Val on = clip;
            if (frame.isNumber()) {
                if (on.isObject() && !std::isnan(frame.num)) host.gotoFrame(on, static_cast<int>(frame.num) - 1 + bias, play);
                break;
            }
            // "label", "12", or "path:frame".
            std::string text = toString(frame);
            const size_t colon = text.rfind(':');
            if (colon != std::string::npos) {
                Val found;
                on = resolveTarget(a, on.isObject() ? on : a.base, text.substr(0, colon), found) ? found : Val();
                text = text.substr(colon + 1);
            }
            if (!on.isObject()) break;
            const double number = parseNumber(text);
            if (!std::isnan(number)) {
                host.gotoFrame(on, static_cast<int>(number) - 1 + bias, play);
            } else {
                host.gotoLabel(on, text, play);
            }
            break;
        }
        case SetTarget: {
            size_t at = start;
            const std::string path = readString(p, at, next);
            setTarget(a, path.empty() ? a.base : targetOf(a, Val::string(path)));
            break;
        }
        case SetTarget2: {
            const Val target = pop(a);
            const bool empty = target.isString() && target.str().empty();
            setTarget(a, empty ? a.base : targetOf(a, target));
            break;
        }
        case WaitForFrame:
        case WaitForFrame2: {
            int count = 0;
            int frame = 0;
            bool known = true;
            if (op == WaitForFrame) {
                frame = start + 2 <= next ? readU16(p + start) : 0;
                count = start + 3 <= next ? p[start + 2] : 0;
            } else {
                count = start < next ? p[start] : 0;
                const Val wanted = pop(a);
                known = wanted.isNumber();
                frame = known ? static_cast<int>(wanted.num) - 1 : 0;
            }
            if (known && clip.isObject() && frame >= host.framesLoaded(clip)) {
                // Not loaded: the actions after it are passed over.
                for (int i = 0; i < count && pc < a.end; ++i) {
                    pc += p[pc] >= 0x80 && pc + 3 <= a.end ? 3 + readU16(p + pc + 1) : 1;
                }
            }
            break;
        }
        case Call: {
            const Val frame = pop(a);
            Val on = clip;
            Val which = frame;
            if (frame.isString()) {
                const std::string text = frame.str();
                const size_t colon = text.rfind(':');
                if (colon != std::string::npos) {
                    Val found;
                    on = resolveTarget(a, on.isObject() ? on : a.base, text.substr(0, colon), found) ? found : Val();
                    which = Val::string(text.substr(colon + 1));
                }
            }
            if (on.isObject()) host.callFrame(on, which);
            break;
        }
        case GetProperty: {
            const Val index = pop(a);
            const Val on = targetOf(a, pop(a));
            const double n = toNumber(index);
            if (!on.isObject() || std::isnan(n) || n < 0 || n >= kPropertyCount) {
                stack.push_back(Val());
                break;
            }
            const std::string name = kPropertyNames[static_cast<int>(n)];
            Val value = host.getProperty(on, name);
            if (value.isUndefined()) {
                if (name == "_target") {
                    value = Val::string(slashPath(on));
                } else if (name == "_quality") {
                    value = Val::string("HIGH");
                } else if (name == "_highquality" || name == "_focusrect") {
                    value = Val::number(1);
                } else if (name == "_soundbuftime") {
                    value = Val::number(5);
                } else {
                    value = getMember(on, name);
                }
            }
            stack.push_back(value);
            break;
        }
        case SetProperty: {
            const Val value = pop(a);
            const Val index = pop(a);
            const Val on = targetOf(a, pop(a));
            const double n = toNumber(index);
            if (!on.isObject() || std::isnan(n) || n < 0 || n >= kPropertyCount) break;
            const std::string name = kPropertyNames[static_cast<int>(n)];
            if (!host.setProperty(on, name, value)) setMember(on, name, value);
            break;
        }
        case CloneSprite: {
            const double depthNumber = toNumber(pop(a));
            const std::string name = toString(pop(a));
            const Val on = targetOf(a, pop(a));
            if (on.isObject()) {
                host.cloneSprite(on, name, std::isnan(depthNumber) ? 0 : static_cast<int>(depthNumber) - 16384);
            }
            break;
        }
        case RemoveSprite: {
            const Val on = targetOf(a, pop(a));
            if (on.isObject()) host.removeSprite(on);
            break;
        }
        case StartDrag: {
            const Val on = targetOf(a, pop(a));
            const bool lock = toBoolean(pop(a));
            const bool constrain = toBoolean(pop(a));
            double left = 0, top = 0, right = 0, bottom = 0;
            if (constrain) {
                bottom = toNumber(pop(a));
                right = toNumber(pop(a));
                top = toNumber(pop(a));
                left = toNumber(pop(a));
            }
            if (on.isObject()) host.startDrag(on, lock, constrain, left, top, right, bottom);
            break;
        }
        case EndDrag: host.endDrag(); break;
        case GetURL: {
            size_t at = start;
            const std::string url = readString(p, at, next);
            const std::string window = readString(p, at, next);
            host.getURL(url, window, 0, false, false);
            break;
        }
        case GetURL2: {
            const uint8_t flags = start < next ? p[start] : 0;
            const std::string window = toString(pop(a));
            const std::string url = toString(pop(a));
            host.getURL(url, window, flags >> 6, (flags & 0x02) != 0, (flags & 0x01) != 0);
            break;
        }
        case ToggleQuality: host.toggleQuality(); break;
        case StopSounds: host.stopSounds(); break;
        case Trace: {
            const Val value = pop(a);
            host.trace(value.isUndefined() ? std::string("undefined") : toString(value));
            break;
        }
        case GetTime: stack.push_back(Val::number(std::floor(host.time()))); break;
        case RandomNumber: {
            const double most = toNumber(pop(a));
            if (std::isnan(most) || most < 1) {
                stack.push_back(Val::number(0));
            } else {
                std::uniform_int_distribution<long long> pick(0, static_cast<long long>(most) - 1);
                stack.push_back(Val::number(static_cast<double>(pick(random))));
            }
            break;
        }
        default: break;
    }
}

} // namespace avm1 (Machine)

// -- Interpreter --------------------------------------------------------------

namespace avm1 {

Interpreter::Interpreter(VM& vm, Host& host, int version) : _machine(std::make_unique<Machine>(vm, host, version)) {}

Interpreter::~Interpreter() = default;

int Interpreter::version() const { return _machine->version; }

void Interpreter::setVersion(int version) { _machine->version = version; }

bool Interpreter::run(const Actions& actions, const Val& clip, const std::string& where) {
    AS3_ZONE("AVM1 run");
    AS3_ZONE_TEXT(where);
    return _machine->run(actions, clip, where);
}

Val Interpreter::handler(const Actions& actions, const std::string& where) {
    std::shared_ptr<Machine*> token = _machine->life;
    return _machine->native("handler", [token, actions, where](VM&, const Val& self, Args) -> Val {
        if (Machine* m = *token) m->run(actions, self, where);
        return Val();
    });
}

Val Interpreter::global() { return _machine->globalObject; }

Val Interpreter::noObject() { return _machine->noObject; }

Val Interpreter::prototypeOf(const Val& constructor) { return _machine->prototypeOf(constructor); }

Val Interpreter::registeredClass(const std::string& linkage) {
    const auto found = _machine->registered.find(linkage);
    return found == _machine->registered.end() ? Val() : found->second;
}

void Interpreter::applyClass(const Val& clip, const Val& constructor) {
    Machine& m = *_machine;
    if (!clip.isObject() || !constructor.isFunction()) return;
    const Val proto = m.prototypeOf(constructor);
    m.setMember(clip, "__proto__", proto);
    call(constructor, clip, {}, "class " + (constructor->function ? constructor->function->name : std::string()));
}

std::vector<std::string> Interpreter::forInNames(const Val& object) {
    try {
        return _machine->enumerate(object);
    } catch (const ScriptException&) {
        return {};
    }
}

bool Interpreter::isAccessor(const Val& object, const std::string& name) {
    Val at = object;
    for (int guard = 0; at.isObject() && guard < kDepthLimit; ++guard) {
        if (const ObjectData* d = _machine->data(at)) {
            if (d->accessors.count(name)) return true;
        }
        if (at->properties.count(name)) return false;
        at = _machine->protoOf(at);
    }
    return false;
}

bool Interpreter::hasMember(const Val& object, const std::string& name) {
    Val out;
    try {
        return _machine->lookup(object, name, out, nullptr);
    } catch (const ScriptException&) {
        return false;
    }
}

Val Interpreter::getMember(const Val& object, const std::string& name) {
    try {
        return _machine->getMember(object, name);
    } catch (const ScriptException&) {
        return Val();
    } catch (const detail::Halt&) {
        return Val();
    }
}

void Interpreter::setMember(const Val& object, const std::string& name, const Val& value) {
    try {
        _machine->setMember(object, name, value);
    } catch (const ScriptException&) {
    } catch (const detail::Halt&) {
    }
}

Val Interpreter::call(const Val& function, const Val& self, const std::vector<Val>& args, const std::string& where) {
    AS3_ZONE("AVM1 call");
    AS3_ZONE_TEXT(where);
    Machine& m = *_machine;
    if (m.runs == 0 && m.depth == 0) m.steps = 0;
    ++m.runs;
    struct Runs {
        int& value;
        ~Runs() { --value; }
    } count{m.runs};
    try {
        return m.callValue(function, self, args, self.isObject() ? m.protoOf(self) : Val());
    } catch (const ScriptException& thrown) {
        std::string text;
        try {
            text = m.toString(thrown.value);
        } catch (...) {
            text = "an error";
        }
        m.report(where, "uncaught exception: " + text);
    } catch (const detail::Halt& halt) {
        m.report(where, halt.message);
    } catch (const std::exception& failed) {
        m.report(where, failed.what());
    }
    return Val();
}

Val Interpreter::callMethod(const Val& object, const std::string& name, const std::vector<Val>& args,
                            const std::string& where) {
    AS3_ZONE("AVM1 call method");
    AS3_ZONE_TEXT(name);
    const Val function = getMember(object, name);
    if (!function.isFunction()) return Val();
    return call(function, object, args, where);
}

std::string Interpreter::toString(const Val& value) {
    try {
        return _machine->toString(value);
    } catch (...) {
        return std::string();
    }
}

double Interpreter::toNumber(const Val& value) {
    try {
        return _machine->toNumber(value);
    } catch (...) {
        return NAN;
    }
}

bool Interpreter::toBoolean(const Val& value) { return _machine->toBoolean(value); }

} // namespace avm1
} // namespace as3
