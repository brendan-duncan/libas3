// flash.utils: Dictionary, getDefinitionByName, getQualifiedClassName,
// getQualifiedSuperclassName, getTimer and the timers. The host may
// redefine any of them (VM::defineFunction) to drive them itself.

#include "builtins.h"
#include <cmath>
#include <unordered_map>

namespace as3 {

namespace {

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
