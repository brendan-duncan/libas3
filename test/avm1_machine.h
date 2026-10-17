#pragma once

// A small AVM1 machine for the tests: it runs the action bytecode the AVM1
// compiler makes, as Flash Player would for the parts tests use, so that a
// script's compiled code can be checked by what it does (what it traces,
// the frames it goes to) and not only by its instructions. It is not a
// player: there is no display list, and only a few built-in classes.

#include "../src/avm1/avm1_actions.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace avm1test {

using namespace as3::avm1;

struct Object;
using ObjectPtr = std::shared_ptr<Object>;

struct Val {
    enum Type { Undefined, Null, Boolean, Number, String, Obj } type = Undefined;
    bool boolean = false;
    double number = 0;
    std::string text;
    ObjectPtr object;

    static Val undefined() { return Val(); }
    static Val null() { Val v; v.type = Null; return v; }
    static Val of(bool b) { Val v; v.type = Boolean; v.boolean = b; return v; }
    static Val of(double d) { Val v; v.type = Number; v.number = d; return v; }
    static Val of(const std::string& s) { Val v; v.type = String; v.text = s; return v; }
    static Val of(const ObjectPtr& o) {
        Val v;
        if (!o) return v;
        v.type = Obj;
        v.object = o;
        return v;
    }
};

class Machine;
using Native = std::function<Val(Machine&, const Val& self, const std::vector<Val>& args)>;

// What a function defined by the code is: its body, where it was defined
// (the scopes it sees and the constant pool), its parameters and flags.
struct Function {
    const uint8_t* code = nullptr;
    size_t size = 0;
    std::vector<std::pair<uint8_t, std::string>> params;
    int registers = 4;
    uint16_t flags = 0;
    std::vector<ObjectPtr> scopes;
    std::shared_ptr<std::vector<std::string>> pool;
    Native native;
};

struct Object {
    std::map<std::string, Val> properties;
    std::vector<std::string> order;
    ObjectPtr proto;
    std::shared_ptr<Function> function;
    bool array = false;
    // addProperty: a property's getter and setter.
    std::map<std::string, std::pair<Val, Val>> accessors;
    // A super object: the prototype to look in and this.
    bool isSuper = false;
    ObjectPtr superProto;
    Val superThis;

    void set(const std::string& name, const Val& value) {
        if (!properties.count(name)) order.push_back(name);
        properties[name] = value;
    }
    bool has(const std::string& name) const { return properties.count(name) > 0; }
    void remove(const std::string& name) {
        properties.erase(name);
        order.erase(std::remove(order.begin(), order.end(), name), order.end());
    }
};

// A script's error, thrown with a value.
struct Thrown {
    Val value;
};

class Machine {
public:
    // What the code traced, a line each, and what it asked of the movie
    // ("gotoFrame 4", "play", "getURL page.html _blank"...).
    std::string traced;
    std::vector<std::string> log;
    ObjectPtr global = std::make_shared<Object>();
    // The timeline the code runs on.
    ObjectPtr root = std::make_shared<Object>();
    ObjectPtr objectProto = std::make_shared<Object>();
    ObjectPtr arrayProto = std::make_shared<Object>();
    ObjectPtr functionProto = std::make_shared<Object>();

    Machine() {
        arrayProto->proto = objectProto;
        functionProto->proto = objectProto;
        root->proto = objectProto;
        global->proto = objectProto;
        native(objectProto, "addProperty", [](Machine&, const Val& self, const std::vector<Val>& args) {
            if (self.type == Val::Obj && args.size() >= 3) {
                self.object->accessors[toString(args[0])] = {args[1], args[2]};
            }
            return Val::of(true);
        });
        native(objectProto, "hasOwnProperty", [](Machine&, const Val& self, const std::vector<Val>& args) {
            return Val::of(self.type == Val::Obj && !args.empty() && self.object->has(toString(args[0])));
        });
        native(arrayProto, "push", [](Machine& m, const Val& self, const std::vector<Val>& args) {
            if (self.type != Val::Obj) return Val::undefined();
            int length = static_cast<int>(m.toNumber(m.getMember(self, "length")));
            for (const Val& arg : args) self.object->set(std::to_string(length++), arg);
            self.object->set("length", Val::of(static_cast<double>(length)));
            return Val::of(static_cast<double>(length));
        });
        native(arrayProto, "pop", [](Machine& m, const Val& self, const std::vector<Val>&) {
            if (self.type != Val::Obj) return Val::undefined();
            int length = static_cast<int>(m.toNumber(m.getMember(self, "length")));
            if (length == 0) return Val::undefined();
            const std::string key = std::to_string(length - 1);
            Val last = m.getMember(self, key);
            self.object->remove(key);
            self.object->set("length", Val::of(static_cast<double>(length - 1)));
            return last;
        });
        native(arrayProto, "join", [](Machine& m, const Val& self, const std::vector<Val>& args) {
            const std::string separator = args.empty() ? "," : toString(args[0]);
            const int length = static_cast<int>(m.toNumber(m.getMember(self, "length")));
            std::string out;
            for (int i = 0; i < length; ++i) {
                if (i) out += separator;
                const Val item = m.getMember(self, std::to_string(i));
                if (item.type != Val::Undefined && item.type != Val::Null) out += m.stringOf(item);
            }
            return Val::of(out);
        });
        native(arrayProto, "toString", [](Machine& m, const Val& self, const std::vector<Val>&) {
            return m.callMethod(self, "join", {});
        });
        ObjectPtr math = std::make_shared<Object>();
        math->proto = objectProto;
        native(math, "floor", [](Machine& m, const Val&, const std::vector<Val>& a) {
            return Val::of(std::floor(a.empty() ? NAN : m.toNumber(a[0])));
        });
        native(math, "abs", [](Machine& m, const Val&, const std::vector<Val>& a) {
            return Val::of(std::fabs(a.empty() ? NAN : m.toNumber(a[0])));
        });
        native(math, "max", [](Machine& m, const Val&, const std::vector<Val>& a) {
            return Val::of(a.size() < 2 ? NAN : std::max(m.toNumber(a[0]), m.toNumber(a[1])));
        });
        global->set("Math", Val::of(math));
        ObjectPtr objectClass = makeNative([](Machine& m, const Val&, const std::vector<Val>&) {
            return Val::of(m.newObject());
        });
        objectClass->set("prototype", Val::of(objectProto));
        global->set("Object", Val::of(objectClass));
        ObjectPtr arrayClass = makeNative([](Machine& m, const Val&, const std::vector<Val>& args) {
            ObjectPtr array = m.newArray();
            if (args.size() == 1 && args[0].type == Val::Number) {
                array->set("length", args[0]);
            } else {
                for (size_t i = 0; i < args.size(); ++i) array->set(std::to_string(i), args[i]);
                array->set("length", Val::of(static_cast<double>(args.size())));
            }
            return Val::of(array);
        });
        arrayClass->set("prototype", Val::of(arrayProto));
        global->set("Array", Val::of(arrayClass));
        ObjectPtr errorProto = newObject();
        ObjectPtr errorClass = makeNative([errorProto](Machine& m, const Val&, const std::vector<Val>& args) {
            ObjectPtr error = m.newObject();
            error->proto = errorProto;
            error->set("message", args.empty() ? Val::of(std::string()) : args[0]);
            return Val::of(error);
        });
        errorClass->set("prototype", Val::of(errorProto));
        global->set("Error", Val::of(errorClass));
        global->set("ASSetPropFlags", Val::of(makeNative([](Machine&, const Val&, const std::vector<Val>&) {
            return Val::undefined();
        })));
        global->set("isNaN", Val::of(makeNative([](Machine& m, const Val&, const std::vector<Val>& a) {
            return Val::of(a.empty() || std::isnan(m.toNumber(a[0])));
        })));
    }

    // Runs a DoAction's actions on the root timeline.
    void run(const std::vector<uint8_t>& actions) {
        _keep.push_back(std::make_shared<std::vector<uint8_t>>(actions));
        const std::vector<uint8_t>& code = *_keep.back();
        auto pool = std::make_shared<std::vector<std::string>>();
        std::vector<Val> registers(4);
        std::vector<ObjectPtr> scopes = {global, root};
        try {
            Val ignored;
            execute(code.data(), code.size(), pool, scopes, registers, Val::of(root), ignored);
        } catch (const Thrown& thrown) {
            traced += "uncaught " + stringOf(thrown.value) + "\n";
        }
    }

    // ---- Values

    static bool toBoolean(const Val& v) {
        switch (v.type) {
            case Val::Boolean: return v.boolean;
            case Val::Number: return v.number != 0 && !std::isnan(v.number);
            case Val::String: return !v.text.empty();
            case Val::Obj: return true;
            default: return false;
        }
    }

    double toNumber(const Val& v) {
        switch (v.type) {
            case Val::Boolean: return v.boolean ? 1 : 0;
            case Val::Number: return v.number;
            case Val::String: {
                if (v.text.empty()) return NAN;
                char* end = nullptr;
                const double d = std::strtod(v.text.c_str(), &end);
                return end && *end == 0 ? d : NAN;
            }
            case Val::Null: return 0;
            case Val::Obj: return toNumber(primitive(v));
            default: return NAN;
        }
    }

    static std::string numberString(double d) {
        if (std::isnan(d)) return "NaN";
        if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
        if (d == std::floor(d) && std::fabs(d) < 1e15) {
            std::ostringstream out;
            out << static_cast<long long>(d);
            return out.str();
        }
        std::ostringstream out;
        out.precision(15);
        out << d;
        return out.str();
    }

    static std::string toString(const Val& v) {
        switch (v.type) {
            case Val::Undefined: return "undefined";
            case Val::Null: return "null";
            case Val::Boolean: return v.boolean ? "true" : "false";
            case Val::Number: return numberString(v.number);
            case Val::String: return v.text;
            default: return "[object Object]";
        }
    }

    // A value as text, an object by its toString.
    std::string stringOf(const Val& v) {
        if (v.type == Val::Obj) {
            if (v.object->function) return "[type Function]";
            return toString(primitive(v));
        }
        return toString(v);
    }

    Val primitive(const Val& v) {
        if (v.type != Val::Obj) return v;
        const Val method = getMember(v, "toString");
        if (method.type == Val::Obj && method.object->function) {
            const Val result = call(method, v, {});
            if (result.type != Val::Obj) return result;
        }
        return Val::of(std::string("[object Object]"));
    }

    bool looseEquals(const Val& a, const Val& b) {
        if ((a.type == Val::Undefined || a.type == Val::Null) && (b.type == Val::Undefined || b.type == Val::Null)) {
            return true;
        }
        if (a.type == Val::Undefined || a.type == Val::Null || b.type == Val::Undefined || b.type == Val::Null) {
            return false;
        }
        if (a.type == b.type) return strictEquals(a, b);
        if (a.type == Val::Obj && b.type == Val::Obj) return a.object == b.object;
        if (a.type == Val::Obj) return looseEquals(primitive(a), b);
        if (b.type == Val::Obj) return looseEquals(a, primitive(b));
        return toNumber(a) == toNumber(b);
    }

    static bool strictEquals(const Val& a, const Val& b) {
        if (a.type != b.type) return false;
        switch (a.type) {
            case Val::Undefined:
            case Val::Null: return true;
            case Val::Boolean: return a.boolean == b.boolean;
            case Val::Number: return a.number == b.number;
            case Val::String: return a.text == b.text;
            default: return a.object == b.object;
        }
    }

    ObjectPtr newObject() {
        ObjectPtr o = std::make_shared<Object>();
        o->proto = objectProto;
        return o;
    }

    ObjectPtr newArray() {
        ObjectPtr o = std::make_shared<Object>();
        o->proto = arrayProto;
        o->array = true;
        o->set("length", Val::of(0.0));
        return o;
    }

    ObjectPtr makeNative(Native function) {
        ObjectPtr o = std::make_shared<Object>();
        o->proto = functionProto;
        o->function = std::make_shared<Function>();
        o->function->native = std::move(function);
        return o;
    }

    void native(const ObjectPtr& on, const std::string& name, Native function) {
        on->set(name, Val::of(makeNative(std::move(function))));
    }

    // ---- Members

    Val getMember(const Val& target, const std::string& name) {
        if (target.type == Val::String) {
            if (name == "length") return Val::of(static_cast<double>(target.text.size()));
            if (name == "toUpperCase") {
                return Val::of(makeNative([](Machine&, const Val& self, const std::vector<Val>&) {
                    std::string s = toString(self);
                    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                    return Val::of(s);
                }));
            }
            if (name == "charAt") {
                return Val::of(makeNative([](Machine& m, const Val& self, const std::vector<Val>& a) {
                    const std::string s = toString(self);
                    const int i = a.empty() ? 0 : static_cast<int>(m.toNumber(a[0]));
                    return Val::of(i >= 0 && i < static_cast<int>(s.size()) ? s.substr(i, 1) : std::string());
                }));
            }
            return Val::undefined();
        }
        if (target.type != Val::Obj) return Val::undefined();
        if (target.object->isSuper) {
            return getFrom(target.object->superProto, name, target.object->superThis);
        }
        return getFrom(target.object, name, target);
    }

    // \a name looked up from \a object along its prototypes, a getter called
    // on \a self.
    Val getFrom(const ObjectPtr& object, const std::string& name, const Val& self) {
        int guard = 0;
        for (ObjectPtr at = object; at && guard < 100; at = at->proto, ++guard) {
            const auto accessor = at->accessors.find(name);
            if (accessor != at->accessors.end()) {
                return accessor->second.first.type == Val::Obj ? call(accessor->second.first, self, {})
                                                               : Val::undefined();
            }
            const auto found = at->properties.find(name);
            if (found != at->properties.end()) return found->second;
            if (name == "__proto__") return Val::of(at->proto);
        }
        return Val::undefined();
    }

    // The object on \a object's prototype chain that has \a name.
    ObjectPtr holder(const ObjectPtr& object, const std::string& name) {
        int guard = 0;
        for (ObjectPtr at = object; at && guard < 100; at = at->proto, ++guard) {
            if (at->has(name) || at->accessors.count(name)) return at;
        }
        return nullptr;
    }

    void setMember(const Val& target, const std::string& name, const Val& value) {
        if (target.type != Val::Obj) return;
        ObjectPtr object = target.object;
        if (name == "__proto__") {
            object->proto = value.type == Val::Obj ? value.object : nullptr;
            return;
        }
        // A setter on the way is called.
        int guard = 0;
        for (ObjectPtr at = object; at && guard < 100; at = at->proto, ++guard) {
            const auto accessor = at->accessors.find(name);
            if (accessor != at->accessors.end()) {
                if (accessor->second.second.type == Val::Obj) call(accessor->second.second, target, {value});
                return;
            }
        }
        object->set(name, value);
        if (object->array) {
            char* end = nullptr;
            const long index = std::strtol(name.c_str(), &end, 10);
            if (end && *end == 0 && !name.empty() && index >= 0 &&
                index >= static_cast<long>(toNumber(object->properties["length"]))) {
                object->set("length", Val::of(static_cast<double>(index + 1)));
            }
        }
    }

    // ---- Calls

    Val call(const Val& function, const Val& self, const std::vector<Val>& args, ObjectPtr home = nullptr) {
        if (function.type != Val::Obj || !function.object->function) {
            throw Thrown{Val::of(std::string("not a function"))};
        }
        const Function& f = *function.object->function;
        if (f.native) return f.native(*this, self, args);
        ObjectPtr activation = std::make_shared<Object>();
        std::vector<ObjectPtr> scopes = f.scopes;
        scopes.push_back(activation);
        std::vector<Val> registers(static_cast<size_t>(std::max(f.registers, 1)));
        int next = 1;
        const auto preload = [&](uint16_t flag, const Val& value, const std::string& name) {
            if (f.flags & flag) {
                if (next < static_cast<int>(registers.size())) registers[static_cast<size_t>(next)] = value;
                ++next;
            } else if (!(f.flags & (flag << 1)) && !name.empty()) {
                activation->set(name, value);
            }
        };
        ObjectPtr argumentsArray = newArray();
        for (size_t i = 0; i < args.size(); ++i) argumentsArray->set(std::to_string(i), args[i]);
        argumentsArray->set("length", Val::of(static_cast<double>(args.size())));
        preload(PreloadThis, self, "this");
        preload(PreloadArguments, Val::of(argumentsArray), "arguments");
        // super: the prototype above the one the function was found on.
        ObjectPtr superObject = std::make_shared<Object>();
        superObject->isSuper = true;
        superObject->superThis = self;
        if (home) {
            superObject->superProto = home->proto;
            const Val constructor = home->has("__constructor__") ? home->properties["__constructor__"] : Val();
            if (constructor.type == Val::Obj) superObject->function = constructor.object->function;
            superObject->set("__constructor__", constructor);
        }
        preload(PreloadSuper, Val::of(superObject), "super");
        if (f.flags & PreloadRoot) registers[static_cast<size_t>(next++)] = Val::of(root);
        if (f.flags & PreloadParent) registers[static_cast<size_t>(next++)] = Val::undefined();
        if (f.flags & PreloadGlobal) registers[static_cast<size_t>(next++)] = Val::of(global);
        for (size_t i = 0; i < f.params.size(); ++i) {
            const Val value = i < args.size() ? args[i] : Val::undefined();
            if (f.params[i].first) {
                if (f.params[i].first < registers.size()) registers[f.params[i].first] = value;
            } else {
                activation->set(f.params[i].second, value);
            }
        }
        Val returned;
        execute(f.code, f.size, f.pool, scopes, registers, self, returned);
        return returned;
    }

    Val callMethod(const Val& target, const std::string& name, const std::vector<Val>& args) {
        if (target.type == Val::Obj && target.object->isSuper) {
            const ObjectPtr proto = target.object->superProto;
            if (name.empty()) {
                // super(...): the class extended, on this.
                const Val constructor = target.object->properties["__constructor__"];
                ObjectPtr parentProto = constructor.type == Val::Obj ? getFrom(constructor.object, "prototype", constructor).object
                                                                     : nullptr;
                return call(constructor, target.object->superThis, args, parentProto);
            }
            const Val method = getFrom(proto, name, target.object->superThis);
            return call(method, target.object->superThis, args, holder(proto, name));
        }
        if (name.empty()) return call(target, Val::undefined(), args);
        const Val method = getMember(target, name);
        ObjectPtr home = target.type == Val::Obj ? holder(target.object, name) : nullptr;
        return call(method, target, args, home);
    }

    Val construct(const Val& constructor, const std::vector<Val>& args) {
        if (constructor.type != Val::Obj || !constructor.object->function) {
            throw Thrown{Val::of(std::string("not a constructor"))};
        }
        if (constructor.object->function->native) return call(constructor, Val::undefined(), args);
        ObjectPtr made = newObject();
        const Val proto = getMember(constructor, "prototype");
        if (proto.type == Val::Obj) made->proto = proto.object;
        made->set("__constructor__", constructor);
        const Val result = call(constructor, Val::of(made), args, made->proto);
        return result.type == Val::Obj ? result : Val::of(made);
    }

    // ---- Variables

    Val getVariable(const std::vector<ObjectPtr>& scopes, const std::string& name, const Val& self) {
        if (name == "this") return self;
        if (name == "_global") return Val::of(global);
        if (name == "_root" || name == "_level0") return Val::of(root);
        for (size_t i = scopes.size(); i-- > 0;) {
            if (holder(scopes[i], name)) return getFrom(scopes[i], name, Val::of(scopes[i]));
        }
        return Val::undefined();
    }

    void setVariable(const std::vector<ObjectPtr>& scopes, const std::string& name, const Val& value) {
        for (size_t i = scopes.size(); i-- > 0;) {
            if (scopes[i] != global && scopes[i]->has(name)) {
                setMember(Val::of(scopes[i]), name, value);
                return;
            }
        }
        // Where it is not found it goes on the timeline.
        setMember(Val::of(root), name, value);
    }

private:
    std::vector<std::shared_ptr<std::vector<uint8_t>>> _keep;

    struct Block {
        enum Kind { WithBlock, TryBlock, CatchBlock, FinallyBlock } kind;
        size_t start, end;
        // A try block's parts.
        size_t catchStart = 0, catchEnd = 0, finallyStart = 0, finallyEnd = 0;
        bool hasCatch = false, hasFinally = false, inRegister = false;
        uint8_t reg = 0;
        std::string name;
        bool rethrow = false;
        Val pending;
    };

    static std::string readString(const uint8_t* p, size_t& at, size_t end) {
        std::string out;
        while (at < end && p[at]) out += static_cast<char>(p[at++]);
        if (at < end) ++at;
        return out;
    }

    // Runs the actions from \a code for \a size bytes; a Return sets
    // \a returned and ends it.
    void execute(const uint8_t* code, size_t size, std::shared_ptr<std::vector<std::string>> pool,
                 std::vector<ObjectPtr> scopes, std::vector<Val>& registers, const Val& self, Val& returned) {
        std::vector<Val> stack;
        std::vector<Block> blocks;
        size_t pc = 0;
        const auto pop = [&]() {
            if (stack.empty()) return Val::undefined();
            Val v = stack.back();
            stack.pop_back();
            return v;
        };
        const auto push = [&](const Val& v) { stack.push_back(v); };
        int steps = 0;
        while (pc < size) {
            if (++steps > 2000000) throw std::runtime_error("the code runs on and on");
            // Leaving blocks: a with block's scope goes; a try block run to
            // its end goes on to the finally block.
            bool moved = true;
            while (moved && !blocks.empty()) {
                moved = false;
                Block& block = blocks.back();
                if (pc >= block.start && pc < block.end) break;
                moved = true;
                const Block done = block;
                blocks.pop_back();
                if (done.kind == Block::WithBlock) {
                    scopes.pop_back();
                } else if ((done.kind == Block::TryBlock || done.kind == Block::CatchBlock) && done.hasFinally &&
                           pc == done.end) {
                    Block finally = done;
                    finally.kind = Block::FinallyBlock;
                    finally.start = done.finallyStart;
                    finally.end = done.finallyEnd;
                    blocks.push_back(finally);
                    pc = done.finallyStart;
                } else if (done.kind == Block::TryBlock && pc == done.end && done.hasCatch) {
                    // Nothing thrown: past the catch block.
                    pc = done.catchEnd;
                } else if (done.kind == Block::FinallyBlock && done.rethrow) {
                    throw Thrown{done.pending};
                }
                if (pc >= size) break;
            }
            if (pc >= size) break;
            const uint8_t op = code[pc];
            size_t length = 0;
            size_t start = pc + 1;
            if (op >= 0x80) {
                length = static_cast<size_t>(code[pc + 1] | (code[pc + 2] << 8));
                start = pc + 3;
            }
            const size_t next = start + length;
            const uint8_t* p = code;
            try {
                pc = next;
                switch (op) {
                    case End: pc = size; break;
                    case ConstantPool: {
                        auto fresh = std::make_shared<std::vector<std::string>>();
                        size_t at = start + 2;
                        const int count = code[start] | (code[start + 1] << 8);
                        for (int i = 0; i < count; ++i) fresh->push_back(readString(p, at, next));
                        *pool = *fresh;
                        break;
                    }
                    case Push: {
                        size_t at = start;
                        while (at < next) {
                            const uint8_t type = p[at++];
                            switch (type) {
                                case PushString: push(Val::of(readString(p, at, next))); break;
                                case PushNull: push(Val::null()); break;
                                case PushUndefined: push(Val::undefined()); break;
                                case PushRegister: {
                                    const uint8_t r = p[at++];
                                    push(r < registers.size() ? registers[r] : Val::undefined());
                                    break;
                                }
                                case PushBoolean: push(Val::of(p[at++] != 0)); break;
                                case PushInteger: {
                                    uint32_t bits = 0;
                                    for (int i = 0; i < 4; ++i) bits |= static_cast<uint32_t>(p[at++]) << (8 * i);
                                    push(Val::of(static_cast<double>(static_cast<int32_t>(bits))));
                                    break;
                                }
                                case PushDouble: {
                                    uint32_t high = 0, low = 0;
                                    for (int i = 0; i < 4; ++i) high |= static_cast<uint32_t>(p[at++]) << (8 * i);
                                    for (int i = 0; i < 4; ++i) low |= static_cast<uint32_t>(p[at++]) << (8 * i);
                                    const uint64_t bits = (static_cast<uint64_t>(high) << 32) | low;
                                    double d = 0;
                                    std::memcpy(&d, &bits, sizeof(d));
                                    push(Val::of(d));
                                    break;
                                }
                                case PushFloat: {
                                    uint32_t bits = 0;
                                    for (int i = 0; i < 4; ++i) bits |= static_cast<uint32_t>(p[at++]) << (8 * i);
                                    float f = 0;
                                    std::memcpy(&f, &bits, sizeof(f));
                                    push(Val::of(static_cast<double>(f)));
                                    break;
                                }
                                case PushConstant8:
                                case PushConstant16: {
                                    size_t index = p[at++];
                                    if (type == PushConstant16) index |= static_cast<size_t>(p[at++]) << 8;
                                    push(Val::of(index < pool->size() ? (*pool)[index] : std::string()));
                                    break;
                                }
                                default: throw std::runtime_error("bad push type");
                            }
                        }
                        break;
                    }
                    case Pop: pop(); break;
                    case PushDuplicate: {
                        const Val v = stack.empty() ? Val() : stack.back();
                        push(v);
                        break;
                    }
                    case StackSwap: {
                        const Val a = pop();
                        const Val b = pop();
                        push(a);
                        push(b);
                        break;
                    }
                    case StoreRegister: {
                        const uint8_t r = p[start];
                        if (r < registers.size()) registers[r] = stack.empty() ? Val() : stack.back();
                        break;
                    }
                    case Add2: {
                        const Val b = primitive(pop());
                        const Val a = primitive(pop());
                        if (a.type == Val::String || b.type == Val::String) {
                            push(Val::of(toString(a) + toString(b)));
                        } else {
                            push(Val::of(toNumber(a) + toNumber(b)));
                        }
                        break;
                    }
                    case Add: {
                        const double b = toNumber(pop());
                        push(Val::of(toNumber(pop()) + b));
                        break;
                    }
                    case Subtract: {
                        const double b = toNumber(pop());
                        push(Val::of(toNumber(pop()) - b));
                        break;
                    }
                    case Multiply: {
                        const double b = toNumber(pop());
                        push(Val::of(toNumber(pop()) * b));
                        break;
                    }
                    case Divide: {
                        const double b = toNumber(pop());
                        push(Val::of(toNumber(pop()) / b));
                        break;
                    }
                    case Modulo: {
                        const double b = toNumber(pop());
                        push(Val::of(std::fmod(toNumber(pop()), b)));
                        break;
                    }
                    case Increment: push(Val::of(toNumber(pop()) + 1)); break;
                    case Decrement: push(Val::of(toNumber(pop()) - 1)); break;
                    case BitAnd:
                    case BitOr:
                    case BitXor:
                    case BitLShift:
                    case BitRShift:
                    case BitURShift: {
                        const int32_t b = toInt(pop());
                        const int32_t a = toInt(pop());
                        double r = 0;
                        if (op == BitAnd) r = a & b;
                        if (op == BitOr) r = a | b;
                        if (op == BitXor) r = a ^ b;
                        if (op == BitLShift) r = static_cast<int32_t>(static_cast<uint32_t>(a) << (b & 31));
                        if (op == BitRShift) r = a >> (b & 31);
                        if (op == BitURShift) r = static_cast<uint32_t>(a) >> (b & 31);
                        push(Val::of(r));
                        break;
                    }
                    case Equals2: {
                        const Val b = pop();
                        push(Val::of(looseEquals(pop(), b)));
                        break;
                    }
                    case StrictEquals: {
                        const Val b = pop();
                        push(Val::of(strictEquals(pop(), b)));
                        break;
                    }
                    case Less2:
                    case Greater: {
                        const Val b = primitive(pop());
                        const Val a = primitive(pop());
                        bool r;
                        if (a.type == Val::String && b.type == Val::String) {
                            r = op == Less2 ? a.text < b.text : a.text > b.text;
                        } else {
                            const double x = toNumber(a), y = toNumber(b);
                            r = op == Less2 ? x < y : x > y;
                            if (std::isnan(x) || std::isnan(y)) {
                                push(Val::undefined());
                                break;
                            }
                        }
                        push(Val::of(r));
                        break;
                    }
                    case Not: push(Val::of(!toBoolean(pop()))); break;
                    case ToNumber: push(Val::of(toNumber(pop()))); break;
                    case ToString: push(Val::of(stringOf(pop()))); break;
                    case ToInteger: {
                        const double d = toNumber(pop());
                        push(Val::of(std::isnan(d) ? 0.0 : std::trunc(d)));
                        break;
                    }
                    case StringAdd: {
                        const std::string b = stringOf(pop());
                        push(Val::of(stringOf(pop()) + b));
                        break;
                    }
                    case StringLength: push(Val::of(static_cast<double>(stringOf(pop()).size()))); break;
                    case StringExtract: {
                        const int count = static_cast<int>(toNumber(pop()));
                        const int index = static_cast<int>(toNumber(pop()));
                        const std::string s = stringOf(pop());
                        const int from = std::max(0, index - 1);
                        push(Val::of(from < static_cast<int>(s.size()) ? s.substr(static_cast<size_t>(from),
                                                                                  static_cast<size_t>(std::max(0, count)))
                                                                       : std::string()));
                        break;
                    }
                    case AsciiToChar: push(Val::of(std::string(1, static_cast<char>(toNumber(pop()))))); break;
                    case CharToAscii: {
                        const std::string s = stringOf(pop());
                        push(Val::of(s.empty() ? 0.0 : static_cast<double>(static_cast<unsigned char>(s[0]))));
                        break;
                    }
                    case TypeOf: {
                        const Val v = pop();
                        std::string type = "undefined";
                        if (v.type == Val::Null) type = "null";
                        if (v.type == Val::Boolean) type = "boolean";
                        if (v.type == Val::Number) type = "number";
                        if (v.type == Val::String) type = "string";
                        if (v.type == Val::Obj) type = v.object->function ? "function" : "object";
                        push(Val::of(type));
                        break;
                    }
                    case InstanceOf: {
                        const Val constructor = pop();
                        const Val v = pop();
                        bool r = false;
                        const Val proto = getMember(constructor, "prototype");
                        if (v.type == Val::Obj && proto.type == Val::Obj) {
                            for (ObjectPtr at = v.object->proto; at; at = at->proto) {
                                if (at == proto.object) {
                                    r = true;
                                    break;
                                }
                            }
                        }
                        push(Val::of(r));
                        break;
                    }
                    case Jump:
                    case If: {
                        const int16_t offset = static_cast<int16_t>(p[start] | (p[start + 1] << 8));
                        if (op == Jump || toBoolean(pop())) pc = static_cast<size_t>(static_cast<long>(next) + offset);
                        break;
                    }
                    case GetVariable: push(getVariable(scopes, stringOf(pop()), self)); break;
                    case SetVariable: {
                        const Val value = pop();
                        setVariable(scopes, stringOf(pop()), value);
                        break;
                    }
                    case DefineLocal: {
                        const Val value = pop();
                        scopes.back()->set(stringOf(pop()), value);
                        break;
                    }
                    case DefineLocal2: {
                        const std::string name = stringOf(pop());
                        if (!scopes.back()->has(name)) scopes.back()->set(name, Val::undefined());
                        break;
                    }
                    case Delete2: {
                        const std::string name = stringOf(pop());
                        bool removed = false;
                        for (size_t i = scopes.size(); i-- > 0 && !removed;) {
                            if (scopes[i]->has(name)) {
                                scopes[i]->remove(name);
                                removed = true;
                            }
                        }
                        push(Val::of(removed));
                        break;
                    }
                    case Delete: {
                        const std::string name = stringOf(pop());
                        const Val target = pop();
                        bool removed = false;
                        if (target.type == Val::Obj && target.object->has(name)) {
                            target.object->remove(name);
                            removed = true;
                        }
                        push(Val::of(removed));
                        break;
                    }
                    case GetMember: {
                        const Val key = pop();
                        const Val target = pop();
                        push(getMember(target, key.type == Val::String ? key.text : stringOf(key)));
                        break;
                    }
                    case SetMember: {
                        const Val value = pop();
                        const Val key = pop();
                        setMember(pop(), stringOf(key), value);
                        break;
                    }
                    case InitArray: {
                        const int count = static_cast<int>(toNumber(pop()));
                        ObjectPtr array = newArray();
                        for (int i = 0; i < count; ++i) array->set(std::to_string(i), pop());
                        array->set("length", Val::of(static_cast<double>(count)));
                        push(Val::of(array));
                        break;
                    }
                    case InitObject: {
                        const int count = static_cast<int>(toNumber(pop()));
                        ObjectPtr object = newObject();
                        for (int i = 0; i < count; ++i) {
                            const Val value = pop();
                            object->set(stringOf(pop()), value);
                        }
                        push(Val::of(object));
                        break;
                    }
                    case CallFunction: {
                        const std::string name = stringOf(pop());
                        const std::vector<Val> args = popArgs(stack);
                        push(call(getVariable(scopes, name, self), Val::undefined(), args));
                        break;
                    }
                    case CallMethod: {
                        const Val name = pop();
                        const Val target = pop();
                        const std::vector<Val> args = popArgs(stack);
                        const std::string method =
                            name.type == Val::Undefined ? std::string() : stringOf(name);
                        push(callMethod(target, method, args));
                        break;
                    }
                    case NewObject: {
                        const std::string name = stringOf(pop());
                        const std::vector<Val> args = popArgs(stack);
                        push(construct(getVariable(scopes, name, self), args));
                        break;
                    }
                    case NewMethod: {
                        const Val name = pop();
                        const Val target = pop();
                        const std::vector<Val> args = popArgs(stack);
                        const Val constructor = name.type == Val::Undefined || stringOf(name).empty()
                            ? target : getMember(target, stringOf(name));
                        push(construct(constructor, args));
                        break;
                    }
                    case Return: {
                        returned = pop();
                        return;
                    }
                    case Extends: {
                        const Val superClass = pop();
                        const Val subClass = pop();
                        ObjectPtr proto = newObject();
                        const Val superProto = getMember(superClass, "prototype");
                        proto->proto = superProto.type == Val::Obj ? superProto.object : nullptr;
                        proto->set("__constructor__", superClass);
                        setMember(subClass, "prototype", Val::of(proto));
                        break;
                    }
                    case DefineFunction2:
                    case DefineFunction: {
                        size_t at = start;
                        const std::string name = readString(p, at, next);
                        const int count = p[at] | (p[at + 1] << 8);
                        at += 2;
                        auto function = std::make_shared<Function>();
                        if (op == DefineFunction2) {
                            function->registers = p[at++];
                            function->flags = static_cast<uint16_t>(p[at] | (p[at + 1] << 8));
                            at += 2;
                            for (int i = 0; i < count; ++i) {
                                const uint8_t r = p[at++];
                                function->params.emplace_back(r, readString(p, at, next));
                            }
                        } else {
                            for (int i = 0; i < count; ++i) function->params.emplace_back(0, readString(p, at, next));
                        }
                        const size_t bodySize = static_cast<size_t>(p[at] | (p[at + 1] << 8));
                        function->code = p + next;
                        function->size = bodySize;
                        function->scopes = scopes;
                        function->pool = pool;
                        ObjectPtr object = std::make_shared<Object>();
                        object->proto = functionProto;
                        object->function = function;
                        ObjectPtr prototype = newObject();
                        prototype->set("constructor", Val::of(object));
                        object->set("prototype", Val::of(prototype));
                        pc = next + bodySize;
                        if (name.empty()) {
                            push(Val::of(object));
                        } else {
                            scopes.back()->set(name, Val::of(object));
                        }
                        break;
                    }
                    case With: {
                        const size_t blockSize = static_cast<size_t>(p[start] | (p[start + 1] << 8));
                        const Val target = pop();
                        Block block{Block::WithBlock, next, next + blockSize};
                        blocks.push_back(block);
                        scopes.push_back(target.type == Val::Obj ? target.object : newObject());
                        break;
                    }
                    case Try: {
                        size_t at = start;
                        const uint8_t flags = p[at++];
                        const size_t trySize = static_cast<size_t>(p[at] | (p[at + 1] << 8));
                        const size_t catchSize = static_cast<size_t>(p[at + 2] | (p[at + 3] << 8));
                        const size_t finallySize = static_cast<size_t>(p[at + 4] | (p[at + 5] << 8));
                        at += 6;
                        Block block{Block::TryBlock, next, next + trySize};
                        block.hasCatch = (flags & 0x01) != 0;
                        block.hasFinally = (flags & 0x02) != 0;
                        block.inRegister = (flags & 0x04) != 0;
                        if (block.inRegister) {
                            block.reg = p[at];
                        } else {
                            block.name = readString(p, at, next);
                        }
                        block.catchStart = block.end;
                        block.catchEnd = block.catchStart + catchSize;
                        block.finallyStart = block.catchEnd;
                        block.finallyEnd = block.finallyStart + finallySize;
                        blocks.push_back(block);
                        break;
                    }
                    case Throw: throw Thrown{pop()};
                    case Enumerate2: {
                        const Val target = pop();
                        push(Val::null());
                        if (target.type == Val::Obj) {
                            for (const std::string& name : target.object->order) push(Val::of(name));
                        }
                        break;
                    }
                    case Trace: traced += stringOf(pop()) + "\n"; break;
                    case GotoFrame: log.push_back("gotoFrame " + std::to_string(p[start] | (p[start + 1] << 8))); break;
                    case GotoLabel: {
                        size_t at = start;
                        log.push_back("gotoLabel " + readString(p, at, next));
                        break;
                    }
                    case GotoFrame2: {
                        const uint8_t flags = p[start];
                        std::string entry = "gotoFrame2 " + stringOf(pop());
                        if (flags & 0x02) entry += " bias " + std::to_string(p[start + 1] | (p[start + 2] << 8));
                        if (flags & 0x01) entry += " play";
                        log.push_back(entry);
                        break;
                    }
                    case Play: log.push_back("play"); break;
                    case Stop: log.push_back("stop"); break;
                    case NextFrame: log.push_back("nextFrame"); break;
                    case PrevFrame: log.push_back("prevFrame"); break;
                    case StopSounds: log.push_back("stopSounds"); break;
                    case GetURL: {
                        size_t at = start;
                        const std::string url = readString(p, at, next);
                        log.push_back("getURL " + url + " " + readString(p, at, next));
                        break;
                    }
                    case GetURL2: {
                        const std::string target = stringOf(pop());
                        const std::string url = stringOf(pop());
                        log.push_back("getURL2 " + url + " " + target + " " + std::to_string(p[start]));
                        break;
                    }
                    case SetTarget: {
                        size_t at = start;
                        log.push_back("setTarget " + readString(p, at, next));
                        break;
                    }
                    case SetTarget2: log.push_back("setTarget2 " + stringOf(pop())); break;
                    case GetProperty: {
                        const int index = static_cast<int>(toNumber(pop()));
                        const Val target = pop();
                        push(getMember(target.type == Val::String ? getVariable(scopes, target.text, self) : target,
                                       "#" + std::to_string(index)));
                        break;
                    }
                    case SetProperty: {
                        const Val value = pop();
                        const int index = static_cast<int>(toNumber(pop()));
                        const Val target = pop();
                        setMember(target.type == Val::String ? getVariable(scopes, target.text, self) : target,
                                  "#" + std::to_string(index), value);
                        break;
                    }
                    case CloneSprite: {
                        const double depth = toNumber(pop());
                        const std::string name = stringOf(pop());
                        log.push_back("clone " + stringOf(pop()) + " " + name + " " + numberString(depth));
                        break;
                    }
                    case RemoveSprite: log.push_back("remove " + stringOf(pop())); break;
                    case StartDrag: {
                        const std::string target = stringOf(pop());
                        const bool lock = toBoolean(pop());
                        const bool constrain = toBoolean(pop());
                        std::string entry = "drag " + target + (lock ? " lock" : "");
                        if (constrain) {
                            const double y2 = toNumber(pop()), x2 = toNumber(pop()), y1 = toNumber(pop()),
                                         x1 = toNumber(pop());
                            entry += " " + numberString(x1) + " " + numberString(y1) + " " + numberString(x2) + " " +
                                     numberString(y2);
                        }
                        log.push_back(entry);
                        break;
                    }
                    case EndDrag: log.push_back("stopDrag"); break;
                    case GetTime: push(Val::of(1000.0)); break;
                    case RandomNumber: pop(); push(Val::of(0.0)); break;
                    case TargetPath: pop(); push(Val::of(std::string("_level0"))); break;
                    case ToggleQuality: log.push_back("toggleQuality"); break;
                    case Call: log.push_back("call " + stringOf(pop())); break;
                    default:
                        throw std::runtime_error(std::string("the machine has no action ") + std::to_string(op));
                }
            } catch (const Thrown& thrown) {
                // The innermost try block taking it: its catch block, or its
                // finally block and then thrown on.
                bool caught = false;
                while (!blocks.empty()) {
                    Block block = blocks.back();
                    blocks.pop_back();
                    if (block.kind == Block::WithBlock) {
                        scopes.pop_back();
                        continue;
                    }
                    if (block.kind == Block::TryBlock && block.hasCatch) {
                        if (block.inRegister) {
                            if (block.reg < registers.size()) registers[block.reg] = thrown.value;
                        } else {
                            scopes.back()->set(block.name, thrown.value);
                        }
                        Block catchBlock = block;
                        catchBlock.kind = Block::CatchBlock;
                        catchBlock.start = block.catchStart;
                        catchBlock.end = block.catchEnd;
                        blocks.push_back(catchBlock);
                        pc = block.catchStart;
                        caught = true;
                        break;
                    }
                    if ((block.kind == Block::TryBlock || block.kind == Block::CatchBlock) && block.hasFinally) {
                        Block finally = block;
                        finally.kind = Block::FinallyBlock;
                        finally.start = block.finallyStart;
                        finally.end = block.finallyEnd;
                        finally.rethrow = true;
                        finally.pending = thrown.value;
                        blocks.push_back(finally);
                        pc = block.finallyStart;
                        caught = true;
                        break;
                    }
                }
                if (!caught) throw;
            }
        }
    }

    int32_t toInt(const Val& v) {
        const double d = toNumber(v);
        if (std::isnan(d) || std::isinf(d)) return 0;
        return static_cast<int32_t>(static_cast<int64_t>(std::trunc(d)) & 0xFFFFFFFF);
    }

    std::vector<Val> popArgs(std::vector<Val>& stack) {
        const auto pop = [&]() {
            if (stack.empty()) return Val::undefined();
            Val v = stack.back();
            stack.pop_back();
            return v;
        };
        const int count = static_cast<int>(toNumber(pop()));
        std::vector<Val> args;
        for (int i = 0; i < count; ++i) args.push_back(pop());
        return args;
    }
};

} // namespace avm1test
