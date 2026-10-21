// flash.utils: Dictionary, getDefinitionByName, getQualifiedClassName,
// getQualifiedSuperclassName, getTimer and the timers. The host may
// redefine any of them (VM::defineFunction) to drive them itself.

#include "builtins.h"
#include <cmath>
#include <unordered_map>

namespace as3 {

namespace {

// A flash.utils.Proxy: each name its class does not declare is asked of
// the overridden flash_proxy methods (getProperty, callProperty...), named
// by a QName.
struct ProxyHost : HostObject {
    VM& vm;
    std::weak_ptr<Value> self;
    explicit ProxyHost(VM& machine) : vm(machine) {}

    Val object() const { return Val(ValuePtr(self.lock())); }
    Val nameOf(const std::string& name) const {
        return vm.construct(Val(vm.getGlobal("QName")), std::vector<Val>{Val::string(""), Val::string(name)});
    }
    // Its flash_proxy method (an override of one written without the
    // namespace, as a class compiled from source has it, if there is one).
    Val ask(const char* method, std::vector<Val> args) {
        const Val it = object();
        if (!it.isObject()) return Val();
        const ClassInfo* cls = vm.classOf(it);
        const Trait* plain = cls ? cls->findTrait(method) : nullptr;
        const std::string key = plain && plain->kind == TraitKind::Method ? std::string(method)
                                                                           : std::string("flash_proxy::") + method;
        return vm.callMethod(it, key, Args(args.data(), args.size()));
    }

    bool answersEveryName() const override { return true; }
    ValuePtr get(const std::string& name) override {
        return ValuePtr(ask("getProperty", {nameOf(name)}));
    }
    bool set(const std::string& name, ValuePtr value) override {
        ask("setProperty", {nameOf(name), Val(value)});
        return true;
    }
    // A key as the methods are given it: a QName as it is, anything else
    // as a QName of its text.
    Val keyOf(const Val& key) const {
        if (key.isObject() && key->cls && key->cls->name == "QName") return key;
        return nameOf(key.isString() ? key.str() : vm.toString(key));
    }
    static bool isQName(const Val& key) { return key.isObject() && key->cls && key->cls->name == "QName"; }
    bool getIndex(const Val& key, Val& out) override {
        if (!isQName(key)) return false;
        out = ask("getProperty", {key});
        return true;
    }
    bool setIndex(const Val& key, const Val& value) override {
        if (!isQName(key)) return false;
        ask("setProperty", {key, value});
        return true;
    }
    bool deleteIndex(const Val& key) override {
        ask("deleteProperty", {keyOf(key)});
        return true;
    }
    // Asked as given: "x" in p asks of a String.
    bool has(const Val& key) override {
        return ask("hasProperty", {key.isString() ? key : Val::string(vm.toString(key))}).toBoolean();
    }
    bool call(const std::string& name, Args args, Val& out) override {
        std::vector<Val> list = {nameOf(name)};
        list.insert(list.end(), args.begin(), args.end());
        out = ask("callProperty", std::move(list));
        return true;
    }
    double nextIndex(double index) override { return vm.toNumber(ask("nextNameIndex", {Val::number(index)})); }
    Val nextName(double index) override { return ask("nextName", {Val::number(index)}); }
    Val nextValue(double index) override { return ask("nextValue", {Val::number(index)}); }
    bool enumerate(std::vector<Val>& keys) override {
        for (double at = nextIndex(0); at != 0 && keys.size() < 1000000; at = nextIndex(at)) {
            keys.push_back(nextName(at));
        }
        return true;
    }
};

// A Dictionary's entries: objects as keys by identity, other keys by
// their text.
struct DictionaryState : HostObject {
    struct Entry {
        Val key;
        Val value;
    };
    std::unordered_map<const Value*, Entry> objects;
    std::unordered_map<std::string, Entry> named;
    std::vector<Val> order;

    static bool isObjectKey(const Val& key) { return key.isObject(); }

    bool getIndex(const Val& key, Val& out) override {
        if (isObjectKey(key)) {
            auto found = objects.find(key.get());
            out = found == objects.end() ? Val() : found->second.value;
            return true;
        }
        auto found = named.find(key.toString());
        out = found == named.end() ? Val() : found->second.value;
        return true;
    }
    bool setIndex(const Val& key, const Val& value) override {
        if (isObjectKey(key)) {
            objects[key.get()] = Entry{key, value};
        } else {
            named[key.toString()] = Entry{key, value};
        }
        return true;
    }
    bool deleteIndex(const Val& key) override {
        if (isObjectKey(key)) {
            objects.erase(key.get());
        } else {
            named.erase(key.toString());
        }
        return true;
    }
    bool hasIndex(const Val& key) override {
        if (isObjectKey(key)) return objects.count(key.get()) > 0;
        return named.count(key.toString()) > 0;
    }
    bool enumerate(std::vector<Val>& keys) override {
        for (const auto& entry : objects) keys.push_back(entry.second.key);
        for (const auto& entry : named) keys.push_back(entry.second.key);
        return true;
    }
    // dict.name and dict.name = value.
    ValuePtr get(const std::string& name) override {
        auto found = named.find(name);
        if (found == named.end()) return nullptr;
        return found->second.value;
    }
    bool set(const std::string& name, ValuePtr value) override {
        named[name] = Entry{Val::string(name), Val(value)};
        return true;
    }
};

std::string qualifiedClassName(const ClassInfo* cls) {
    if (!cls) return "null";
    if (cls->packageName.empty()) return cls->name;
    return cls->packageName + "::" + cls->shortName;
}

std::string qualifiedNameOf(VM& vm, const Val& value) {
    switch (value.tag) {
        case Val::Tag::Undefined:
            return "void";
        case Val::Tag::Null:
            return "null";
        case Val::Tag::Number: {
            const double n = value.num;
            if (n == std::floor(n) && n >= -2147483648.0 && n <= 2147483647.0) return "int";
            return "Number";
        }
        case Val::Tag::String:
            return "String";
        case Val::Tag::Boolean:
            return "Boolean";
        default:
            break;
    }
    if (value.isClass()) return qualifiedClassName(value->cls.get());
    return qualifiedClassName(vm.classOf(value));
}

Val scheduleTimer(VM& vm, Args args, bool repeat) {
    if (!args[0].isFunction()) {
        vm.throwError("TypeError", "Error #1034: Type Coercion failed: Function expected.", 1034);
    }
    VM::TimerEntry entry;
    entry.id = vm.newTimerId();
    entry.function = args[0];
    entry.interval = repeat ? std::max(1.0, vm.toNumber(args[1])) : 0;
    entry.due = vm.now() + std::max(0.0, vm.toNumber(args[1]));
    for (size_t i = 2; i < args.size(); ++i) entry.args.push_back(args[i]);
    vm.timerList().push_back(entry);
    return Val::number(entry.id);
}

Val clearTimer(VM& vm, const Val&, Args args) {
    const int id = args.integer(0, 0);
    auto& timers = vm.timerList();
    for (auto it = timers.begin(); it != timers.end(); ++it) {
        if (it->id == id) {
            timers.erase(it);
            break;
        }
    }
    return Val();
}

} // namespace

void defineUtilsBuiltins(VM& vm) {
    // flash.utils.Proxy, and the namespace its methods are in. Each method
    // not overridden throws.
    const auto unimplemented = [](const char* method, int id) {
        return [method, id](VM& vm, const Val&, Args) -> Val {
            vm.throwError("Error", "Error #" + std::to_string(id) + ": The Proxy class does not implement " + method +
                                       ". It must be overridden by a subclass.", id);
        };
    };
    vm.defineNativeClass("flash.utils.Proxy")
        .dynamic()
        .constructor([](VM& vm, const Val& self, Args) {
            auto host = std::make_shared<ProxyHost>(vm);
            host->self = self.ref;
            self->host = host;
            return Val();
        })
        .method("flash_proxy::getProperty", unimplemented("getProperty", 2088), 1)
        .method("flash_proxy::setProperty", unimplemented("setProperty", 2089), 2)
        .method("flash_proxy::callProperty", unimplemented("callProperty", 2090), 1)
        .method("flash_proxy::hasProperty", unimplemented("hasProperty", 2091), 1)
        .method("flash_proxy::deleteProperty", unimplemented("deleteProperty", 2092), 1)
        .method("flash_proxy::getDescendants", unimplemented("getDescendants", 2093), 1)
        .method("flash_proxy::nextNameIndex", unimplemented("nextNameIndex", 2105), 1)
        .method("flash_proxy::nextName", unimplemented("nextName", 2106), 1)
        .method("flash_proxy::nextValue", unimplemented("nextValue", 2107), 1)
        .method("flash_proxy::isAttribute", [](VM&, const Val&, Args) { return Val::boolean(false); }, 1)
        // Its own hasOwnProperty asks hasProperty, of the name as given.
        .method("hasOwnProperty", [](VM&, const Val& self, Args args) {
            ProxyHost* proxy = self.host<ProxyHost>();
            if (!proxy) return Val::boolean(false);
            return Val::boolean(proxy->has(args[0]));
        }, 1)
        .define();
    vm.defineValue("flash.utils.flash_proxy", makeNamespaceValue(vm, "http://www.adobe.com/2006/actionscript/flash/proxy"));

    vm.builtins().dictionary =
        vm.defineNativeClass("flash.utils.Dictionary")
            .dynamic()
            .constructor([](VM&, const Val& self, Args) {
                self->host = std::make_shared<DictionaryState>();
                return Val();
            })
            .define()
            ->cls;

    vm.defineFunction("flash.utils.getDefinitionByName", [](VM& vm, const Val&, Args args) {
        const std::string name = vm.toString(args[0]);
        Val found = vm.findDefinition(name);
        if (found.isUndefined()) {
            vm.throwError("ReferenceError", "Error #1065: Variable " + name + " is not defined.", 1065);
        }
        return found;
    }, 1);
    vm.defineFunction("flash.utils.getQualifiedClassName", [](VM& vm, const Val&, Args args) {
        return Val::string(qualifiedNameOf(vm, args[0]));
    }, 1);
    vm.defineFunction("flash.utils.getQualifiedSuperclassName", [](VM& vm, const Val&, Args args) {
        const Val& value = args[0];
        if (value.isNullish()) return Val::null();
        const ClassInfo* cls = value.isClass() ? value->cls.get() : vm.classOf(value);
        if (!cls || !cls->base) return Val::null();
        return Val::string(qualifiedClassName(cls->base.get()));
    }, 1);
    vm.defineFunction("flash.utils.getTimer", [](VM& vm, const Val&, Args) {
        return Val::number(std::floor(vm.now()));
    });
    vm.defineFunction("flash.utils.setTimeout", [](VM& vm, const Val&, Args args) {
        return scheduleTimer(vm, args, false);
    }, 2);
    vm.defineFunction("flash.utils.setInterval", [](VM& vm, const Val&, Args args) {
        return scheduleTimer(vm, args, true);
    }, 2);
    vm.defineFunction("flash.utils.clearTimeout", clearTimer, 1);
    vm.defineFunction("flash.utils.clearInterval", clearTimer, 1);
}

} // namespace as3
