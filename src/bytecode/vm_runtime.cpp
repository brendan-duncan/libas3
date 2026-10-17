// The VM's object model: properties and traits, names, classes (script
// and native), conversions and errors.

#include "vm.h"
#include "compiler.h"
#include "../parser/parser.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace as3 {

namespace {

std::string shortNameOf(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(dot + 1);
}

// a.b::C as a.b.C.
std::string normalizeName(const std::string& name) {
    const size_t colons = name.find("::");
    if (colons == std::string::npos) {
        return name;
    }
    return name.substr(0, colons) + "." + name.substr(colons + 2);
}

// An array index written as a key: "12" or 12.
bool arrayIndex(const Val& key, size_t& index) {
    if (key.tag == Val::Tag::Number) {
        const double n = key.num;
        if (n >= 0 && n < 4294967295.0 && n == std::floor(n)) {
            index = static_cast<size_t>(n);
            return true;
        }
        return false;
    }
    if (key.tag == Val::Tag::String) {
        const std::string& text = key.str();
        if (text.empty() || text.size() > 10 || (text.size() > 1 && text[0] == '0')) {
            return false;
        }
        size_t value = 0;
        for (char c : text) {
            if (c < '0' || c > '9') return false;
            value = value * 10 + static_cast<size_t>(c - '0');
        }
        if (value >= 4294967295ULL) return false;
        index = value;
        return true;
    }
    return false;
}

} // namespace

// Making values

Val VM::allocate(const std::shared_ptr<ClassInfo>& cls) {
    auto value = std::make_shared<Value>();
    value->type = cls ? cls->instanceType : ValueType::Object;
    value->cls = cls;
    if (cls) {
        value->slots = cls->slotDefaults;
    }
    track(value);
    return Val(value);
}

Val VM::newArray(std::vector<Val> elements) {
    Val array = allocate(core.array);
    array->type = ValueType::Array;
    array->elements = std::move(elements);
    return array;
}

Val VM::newObject() {
    return allocate(core.object);
}

Val VM::makeClosure(const std::shared_ptr<BytecodeFunction>& code, const std::shared_ptr<ClassInfo>& owner) {
    auto value = std::make_shared<Value>();
    value->type = ValueType::Function;
    value->cls = core.function;
    value->function = std::make_shared<FunctionInfo>();
    value->function->code = code;
    value->function->owner = owner;
    value->function->name = code ? code->name : std::string();
    value->function->length = code ? code->arity : 0;
    value->function->isMethod = code && code->isMethod;
    track(value);
    return Val(value);
}

Val VM::makeFunction(const std::string& name, NativeMethod function, int length) {
    auto value = std::make_shared<Value>();
    value->type = ValueType::Function;
    value->cls = core.function;
    value->function = std::make_shared<FunctionInfo>();
    value->function->native = std::move(function);
    value->function->name = name;
    value->function->length = length;
    return Val(value);
}

Val VM::classValue(const ClassInfo* cls) {
    if (!cls) {
        return Val::null();
    }
    if (ValuePtr object = cls->classObject.lock()) {
        return Val(object);
    }
    return Val::null();
}

// Errors

Val VM::makeError(const std::string& className, const std::string& message, int id) {
    Val cls = lookupQualified(className, false);
    if (!cls.isClass()) {
        cls = classValue(core.error.get());
    }
    if (!cls.isClass()) {
        return Val::string(message);
    }
    return construct(cls, std::vector<Val>{Val::string(message), Val::number(id)});
}

void VM::throwError(const std::string& className, const std::string& message, int id) {
    ScriptException exception{makeError(className, message, id)};
    throw exception;
}

void VM::nullError(const Val& object, const std::string&) {
    if (object.isNull()) {
        throwError("TypeError", "Error #1009: Cannot access a property or method of a null object reference.", 1009);
    }
    throwError("TypeError", "Error #1010: A term is undefined and has no properties.", 1010);
}

std::string VM::stackTrace() {
    std::string trace;
    for (size_t i = frames.size(); i-- > 0;) {
        const CallFrame& frame = frames[i];
        if (!frame.code) continue;
        std::string name = frame.code->qualifiedName.empty() ? frame.code->name : frame.code->qualifiedName;
        if (name.empty()) name = "Function/<anonymous>";
        if (frame.code->isScript) name = "global$init";
        const size_t offset = static_cast<size_t>(frame.ip - frame.codeStart);
        const int line = frame.code->chunk.getLine(offset > 0 ? offset - 1 : 0);
        trace += "\tat " + name + "()";
        const std::string file = frame.code->unit ? frame.code->unit->fileName : std::string();
        if (!file.empty() || line > 0) {
            trace += "[" + file + ":" + std::to_string(line) + "]";
        }
        trace += "\n";
    }
    return trace;
}

// Types

ClassInfo* VM::classOf(const Val& value) {
    switch (value.tag) {
        case Val::Tag::Number:
            return core.number.get();
        case Val::Tag::String:
            return core.string.get();
        case Val::Tag::Boolean:
            return core.boolean.get();
        case Val::Tag::Object: {
            Value* object = value.ref.get();
            switch (object->type) {
                case ValueType::Class:
                    return core.klass.get();
                case ValueType::Function:
                case ValueType::NativeFunction:
                    return object->cls ? object->cls.get() : core.function.get();
                case ValueType::Array:
                    return object->cls ? object->cls.get() : core.array.get();
                default:
                    return object->cls ? object->cls.get() : core.object.get();
            }
        }
        default:
            return nullptr;
    }
}

std::string VM::typeOf(const Val& value) {
    switch (value.tag) {
        case Val::Tag::Undefined: return "undefined";
        case Val::Tag::Null: return "object";
        case Val::Tag::Boolean: return "boolean";
        case Val::Tag::Number: return "number";
        case Val::Tag::String: return "string";
        default:
            return value->isFunction() ? "function" : "object";
    }
}

bool VM::isType(const Val& value, const Val& type) {
    if (type.isNullish()) {
        return true;
    }
    if (!type.isClass()) {
        if (type.isFunction()) {
            return false;
        }
        throwError("TypeError", "Error #1041: The right-hand side of operator must be a class.", 1041);
    }
    const ClassInfo* target = type->cls.get();
    if (value.isNullish()) {
        return false;
    }
    if (target == core.object.get()) {
        return true;
    }
    if (target == core.number.get()) {
        return value.isNumber();
    }
    if (target == core.intClass.get()) {
        return value.isNumber() && value.num == std::floor(value.num) && value.num >= -2147483648.0 &&
               value.num <= 2147483647.0;
    }
    if (target == core.uintClass.get()) {
        return value.isNumber() && value.num == std::floor(value.num) && value.num >= 0 && value.num <= 4294967295.0;
    }
    if (target == core.string.get()) {
        return value.isString();
    }
    if (target == core.boolean.get()) {
        return value.isBoolean();
    }
    if (!value.isObject()) {
        return false;
    }
    const ClassInfo* cls = classOf(value);
    return cls && cls->isSubclassOf(target);
}

// Conversions

Val VM::toPrimitive(const Val& value, bool preferNumber) {
    if (value.tag != Val::Tag::Object) {
        return value;
    }
    Value* object = value.ref.get();
    if (object->type == ValueType::Class || object->isFunction()) {
        return Val::string(toString(value));
    }
    const ClassInfo* cls = classOf(value);
    const bool isDate = core.date && cls && cls->isSubclassOf(core.date.get());
    if (preferNumber || !isDate) {
        // valueOf, when the object has its own.
        Val valueOf;
        const Trait* trait = cls ? cls->findTrait("valueOf") : nullptr;
        if (trait && trait->kind == TraitKind::Method) {
            valueOf = trait->method;
        } else if (const Val* own = object->properties.lookup("valueOf")) {
            valueOf = *own;
        }
        if (valueOf.isFunction()) {
            Val result = callFunction(valueOf, value, Args());
            if (!result.isObject()) {
                return result;
            }
        }
    }
    return Val::string(toString(value));
}

double VM::toNumber(const Val& value) {
    if (value.tag != Val::Tag::Object) {
        return value.toNumber();
    }
    return toPrimitive(value, true).toNumber();
}

std::string VM::toString(const Val& value) {
    if (value.tag != Val::Tag::Object) {
        return value.toString();
    }
    Value* object = value.ref.get();
    if (object->type == ValueType::Class) {
        return "[class " + (object->cls ? object->cls->shortName : std::string()) + "]";
    }
    if (object->isFunction()) {
        return "function Function() {}";
    }
    Val method = getProp(value, "toString", nullptr);
    if (method.isFunction()) {
        Val result = callFunction(method, value, Args());
        return result.isObject() ? result->toString() : result.toString();
    }
    return object->toString();
}

Val VM::add(const Val& a, const Val& b) {
    if (a.tag == Val::Tag::Number && b.tag == Val::Tag::Number) {
        return Val::number(a.num + b.num);
    }
    if (a.tag == Val::Tag::String && b.tag == Val::Tag::String) {
        std::string joined;
        joined.reserve(a.str().size() + b.str().size());
        joined += a.str();
        joined += b.str();
        return Val::string(std::move(joined));
    }
    const Val x = toPrimitive(a, false);
    const Val y = toPrimitive(b, false);
    if (x.isString() || y.isString()) {
        return Val::string(x.toString() + y.toString());
    }
    return Val::number(x.toNumber() + y.toNumber());
}

bool VM::lessThan(const Val& a, const Val& b, bool& undefinedResult) {
    const Val x = toPrimitive(a, true);
    const Val y = toPrimitive(b, true);
    if (x.isString() && y.isString()) {
        return x.str() < y.str();
    }
    const double m = x.toNumber();
    const double n = y.toNumber();
    if (std::isnan(m) || std::isnan(n)) {
        undefinedResult = true;
        return false;
    }
    return m < n;
}

// Traits

Val VM::boundMethod(const Val& object, const Trait& trait) {
    if (!trait.method.isObject()) {
        return Val();
    }
    if (object.tag != Val::Tag::Object) {
        // On a primitive (a string's method): bound, not kept.
        auto bound = std::make_shared<Value>();
        bound->type = ValueType::Function;
        bound->cls = core.function;
        bound->function = std::make_shared<FunctionInfo>(*trait.method->function);
        bound->function->bound = true;
        bound->function->boundThis = object;
        return Val(bound);
    }
    Value* self = object.ref.get();
    if (!self->boundMethods) {
        self->boundMethods = std::make_unique<std::unordered_map<const void*, ValuePtr>>();
    }
    const void* key = trait.method.ref.get();
    auto found = self->boundMethods->find(key);
    if (found != self->boundMethods->end()) {
        return Val(found->second);
    }
    auto bound = std::make_shared<Value>();
    bound->type = ValueType::Function;
    bound->cls = core.function;
    bound->function = std::make_shared<FunctionInfo>(*trait.method->function);
    bound->function->bound = true;
    bound->function->boundThis = object;
    (*self->boundMethods)[key] = bound;
    track(bound);
    return Val(bound);
}

Val VM::readTrait(const Val& object, const Trait& trait, bool isStatic) {
    switch (trait.kind) {
        case TraitKind::Slot: {
            if (isStatic) {
                ClassInfo* cls = object.isObject() ? object->cls.get() : nullptr;
                if (cls && trait.slot < cls->staticSlots.size()) return cls->staticSlots[trait.slot];
                return Val();
            }
            if (object.isObject() && trait.slot < object->slots.size()) return object->slots[trait.slot];
            return Val();
        }
        case TraitKind::Method:
            return boundMethod(object, trait);
        case TraitKind::Accessor:
            if (trait.getter.isObject()) {
                const FunctionInfo& info = *trait.getter->function;
                if (info.native && !info.code) {
                    return info.native(*this, object, Args());
                }
                return callFunction(trait.getter, object, Args());
            }
            throwError("ReferenceError", "Error #1077: Illegal read of write-only property.", 1077);
    }
    return Val();
}

void VM::writeTrait(const Val& object, const Trait& trait, const Val& value, const std::string& name, bool isStatic) {
    switch (trait.kind) {
        case TraitKind::Slot: {
            std::vector<Val>* slots = nullptr;
            if (isStatic) {
                if (object.isObject() && object->cls) slots = &object->cls->staticSlots;
            } else if (object.isObject()) {
                slots = &object->slots;
            }
            if (slots && trait.slot < slots->size()) {
                (*slots)[trait.slot] = trait.coerce == Coerce::None ? value : coerceValue(value, trait.coerce);
            }
            return;
        }
        case TraitKind::Accessor:
            if (trait.setter.isObject()) {
                const FunctionInfo& info = *trait.setter->function;
                if (info.native && !info.code) {
                    Val argument = value;
                    info.native(*this, object, Args(&argument, 1));
                    return;
                }
                Val argument = value;
                callFunction(trait.setter, object, Args(&argument, 1));
                return;
            }
            throwError("ReferenceError",
                       "Error #1074: Illegal write to read-only property " + name + " on " +
                           (trait.declaredBy ? trait.declaredBy->name : std::string("Object")) + ".",
                       1074);
        case TraitKind::Method:
            throwError("ReferenceError",
                       "Error #1037: Cannot assign to a method " + name + " on " +
                           (trait.declaredBy ? trait.declaredBy->name : std::string("Object")) + ".",
                       1037);
    }
}

// Properties

bool VM::findOnObject(const Val& object, const std::string& name, Val& value, bool traitsOnly) {
    Value* self = object.ref.get();
    if (self->type == ValueType::Class) {
        if (traitsOnly) {
            return false;
        }
        if (self->cls) {
            if (const Trait* trait = self->cls->findStatic(name)) {
                value = readTrait(object, *trait, true);
                return true;
            }
        }
        if (const Val* found = self->properties.lookup(name)) {
            value = *found;
            return true;
        }
        return false;
    }
    const ClassInfo* cls = classOf(object);
    if (cls) {
        if (const Trait* trait = cls->findTrait(name)) {
            value = readTrait(object, *trait, false);
            return true;
        }
    }
    if (traitsOnly) {
        return false;
    }
    if (self->host) {
        if (ValuePtr found = self->host->get(name)) {
            value = Val(found);
            return true;
        }
    }
    if (const Val* found = self->properties.lookup(name)) {
        value = *found;
        return true;
    }
    return false;
}

bool VM::setOnObject(const Val& object, const std::string& name, const Val& value, bool traitsOnly) {
    Value* self = object.ref.get();
    if (self->type == ValueType::Class) {
        if (traitsOnly) {
            return false;
        }
        if (self->cls) {
            if (const Trait* trait = self->cls->findStatic(name)) {
                writeTrait(object, *trait, value, name, true);
                return true;
            }
        }
        if (self->properties.count(name)) {
            self->properties[name] = value;
            return true;
        }
        return false;
    }
    const ClassInfo* cls = classOf(object);
    if (cls) {
        if (const Trait* trait = cls->findTrait(name)) {
            writeTrait(object, *trait, value, name, false);
            return true;
        }
    }
    if (traitsOnly) {
        return false;
    }
    if (self->host && self->host->set(name, value)) {
        return true;
    }
    if (self->properties.count(name)) {
        self->properties[name] = value;
        return true;
    }
    return false;
}

// A private member's key ("pkg.Class::name") on an object not of that
// class: the name alone.
static const char* plainName(const std::string& name) {
    const size_t colons = name.rfind("::");
    return colons == std::string::npos ? nullptr : name.c_str() + colons + 2;
}

Val VM::getProp(const Val& object, const std::string& name, PropertyCache* cache) {
    switch (object.tag) {
        case Val::Tag::Undefined:
        case Val::Tag::Null:
            nullError(object, name);
        case Val::Tag::Object:
            break;
        default: {
            const ClassInfo* cls = classOf(object);
            if (const Trait* trait = cls->findTrait(name)) {
                return readTrait(object, *trait, false);
            }
            for (const ClassInfo* at = cls; at; at = at->base.get()) {
                auto found = at->prototype.find(name);
                if (found != at->prototype.end()) return found->second;
            }
            if (name == "constructor") {
                return classValue(cls);
            }
            if (const char* plain = plainName(name)) {
                return getProp(object, plain, nullptr);
            }
            return Val();
        }
    }
    Value* self = object.ref.get();
    if (self->type == ValueType::Class) {
        ClassInfo* cls = self->cls.get();
        if (cls) {
            if (const Trait* trait = cls->findStatic(name)) {
                if (cache) {
                    cache->classId = cls->id;
                    cache->trait = trait;
                    cache->onClass = true;
                }
                return readTrait(object, *trait, true);
            }
        }
        if (const Val* found = self->properties.lookup(name)) {
            return *found;
        }
        for (const ClassInfo* at = core.klass.get(); at; at = at->base.get()) {
            if (const Trait* trait = at->findTrait(name)) return readTrait(object, *trait, false);
            auto found = at->prototype.find(name);
            if (found != at->prototype.end()) return found->second;
        }
        if (const char* plain = plainName(name)) {
            return getProp(object, plain, nullptr);
        }
        return Val();
    }
    const ClassInfo* cls = classOf(object);
    if (cls) {
        if (const Trait* trait = cls->findTrait(name)) {
            if (cache && self->cls.get() == cls) {
                cache->classId = cls->id;
                cache->trait = trait;
                cache->onClass = false;
            }
            return readTrait(object, *trait, false);
        }
    }
    if (self->host) {
        if (ValuePtr found = self->host->get(name)) {
            return Val(found);
        }
    }
    if (const Val* found = self->properties.lookup(name)) {
        return *found;
    }
    for (const ClassInfo* at = cls; at; at = at->base.get()) {
        auto found = at->prototype.find(name);
        if (found != at->prototype.end()) return found->second;
    }
    if (name == "constructor") {
        return classValue(cls);
    }
    if (const char* plain = plainName(name)) {
        return getProp(object, plain, nullptr);
    }
    return Val();
}

void VM::setProp(const Val& object, const std::string& name, const Val& value, PropertyCache* cache) {
    switch (object.tag) {
        case Val::Tag::Undefined:
        case Val::Tag::Null:
            nullError(object, name);
        case Val::Tag::Object:
            break;
        default:
            return;
    }
    Value* self = object.ref.get();
    if (self->type == ValueType::Class) {
        ClassInfo* cls = self->cls.get();
        if (cls) {
            if (const Trait* trait = cls->findStatic(name)) {
                if (cache) {
                    cache->classId = cls->id;
                    cache->trait = trait;
                    cache->onClass = true;
                }
                writeTrait(object, *trait, value, name, true);
                return;
            }
        }
        if (const char* plain = plainName(name)) {
            setProp(object, plain, value, nullptr);
            return;
        }
        self->properties[name] = value;
        return;
    }
    const ClassInfo* cls = classOf(object);
    if (cls) {
        if (const Trait* trait = cls->findTrait(name)) {
            if (cache && self->cls.get() == cls) {
                cache->classId = cls->id;
                cache->trait = trait;
                cache->onClass = false;
            }
            writeTrait(object, *trait, value, name, false);
            return;
        }
    }
    if (const char* plain = plainName(name)) {
        setProp(object, plain, value, nullptr);
        return;
    }
    if (self->host && self->host->set(name, value)) {
        return;
    }
    self->properties[name] = value;
}

bool VM::callProp(const Val& object, const std::string& name, Val* args, uint32_t argc, Val* result,
                  PropertyCache* cache) {
    Val function;
    if (object.isNullish()) {
        nullError(object, name);
    }
    const Val self = object;
    if (self.tag == Val::Tag::Object) {
        Value* value = self.ref.get();
        if (value->type == ValueType::Class) {
            ClassInfo* cls = value->cls.get();
            const Trait* trait = cls ? cls->findStatic(name) : nullptr;
            if (trait && trait->kind == TraitKind::Method) {
                if (cache) {
                    cache->classId = cls->id;
                    cache->trait = trait;
                    cache->onClass = true;
                }
                function = trait->method;
            } else {
                function = getProp(self, name, nullptr);
            }
        } else {
            const ClassInfo* cls = classOf(self);
            const Trait* trait = cls ? cls->findTrait(name) : nullptr;
            if (trait && trait->kind == TraitKind::Method) {
                if (cache && value->cls.get() == cls) {
                    cache->classId = cls->id;
                    cache->trait = trait;
                    cache->onClass = false;
                }
                function = trait->method;
            } else {
                function = getProp(self, name, nullptr);
            }
        }
    } else {
        const ClassInfo* cls = classOf(self);
        const Trait* trait = cls ? cls->findTrait(name) : nullptr;
        function = trait && trait->kind == TraitKind::Method ? trait->method : getProp(self, name, nullptr);
    }
    if (!function.isFunction() && !function.isClass()) {
        throwError("TypeError", "Error #1006: " + name + " is not a function.", 1006);
    }
    return invoke(function, self, args, argc, result);
}

bool VM::deleteProp(const Val& object, const std::string& name) {
    if (object.isNullish()) {
        nullError(object, name);
    }
    if (!object.isObject()) {
        return false;
    }
    Value* self = object.ref.get();
    size_t index;
    if (self->type == ValueType::Array && arrayIndex(Val::string(name), index)) {
        if (index < self->elements.size()) self->elements[index] = Val();
        return true;
    }
    if (self->host && self->host->deleteIndex(Val::string(name))) {
        return true;
    }
    return self->properties.erase(name) > 0 || !(classOf(object) && classOf(object)->findTrait(name));
}

Val VM::getIndexOf(const Val& object, const Val& key) {
    if (object.isNullish()) {
        nullError(object, key.toString());
    }
    if (object.isObject()) {
        Value* self = object.ref.get();
        size_t index;
        if (self->type == ValueType::Array && arrayIndex(key, index)) {
            return index < self->elements.size() ? self->elements[index] : Val();
        }
        if (self->host) {
            Val out;
            if (self->host->getIndex(key, out)) {
                return out;
            }
        }
    }
    if (key.isString()) {
        return getProp(object, key.str(), nullptr);
    }
    return getProp(object, key.isObject() ? toString(key) : key.toString(), nullptr);
}

void VM::setIndexOf(const Val& object, const Val& key, const Val& value) {
    if (object.isNullish()) {
        nullError(object, key.toString());
    }
    if (object.isObject()) {
        Value* self = object.ref.get();
        size_t index;
        if (self->type == ValueType::Array && arrayIndex(key, index)) {
            const ClassInfo* cls = self->cls.get();
            if (cls && cls->isVector) {
                const bool fixed = self->boolValue;
                if (index > self->elements.size() || (fixed && index >= self->elements.size())) {
                    throwError("RangeError",
                               "Error #1125: The index " + std::to_string(index) + " is out of range " +
                                   std::to_string(self->elements.size()) + ".",
                               1125);
                }
                if (index == self->elements.size()) {
                    self->elements.push_back(coerceValue(value, cls->elementCoerce));
                } else {
                    self->elements[index] = coerceValue(value, cls->elementCoerce);
                }
                return;
            }
            if (index >= self->elements.size()) {
                if (index > 50000000) {
                    throwError("RangeError", "Error #1005: Array index is not a positive integer (" +
                                                 std::to_string(index) + ").", 1005);
                }
                self->elements.resize(index + 1);
            }
            self->elements[index] = value;
            return;
        }
        if (self->host && self->host->setIndex(key, value)) {
            return;
        }
    }
    setProp(object, key.isString() ? key.str() : (key.isObject() ? toString(key) : key.toString()), value, nullptr);
}

bool VM::deleteIndex(const Val& object, const Val& key) {
    if (object.isNullish()) {
        nullError(object, key.toString());
    }
    if (!object.isObject()) {
        return false;
    }
    Value* self = object.ref.get();
    size_t index;
    if (self->type == ValueType::Array && arrayIndex(key, index)) {
        if (index < self->elements.size()) self->elements[index] = Val();
        return true;
    }
    if (self->host && self->host->deleteIndex(key)) {
        return true;
    }
    return deleteProp(object, key.toString());
}

bool VM::hasKey(const Val& object, const Val& key) {
    if (!object.isObject()) {
        return false;
    }
    Value* self = object.ref.get();
    size_t index;
    if (self->type == ValueType::Array && arrayIndex(key, index)) {
        return index < self->elements.size();
    }
    if (self->host && self->host->hasIndex(key)) {
        return true;
    }
    const std::string name = key.isObject() ? toString(key) : key.toString();
    if (self->properties.count(name)) {
        return true;
    }
    if (self->type == ValueType::Class) {
        return self->cls && self->cls->findStatic(name);
    }
    const ClassInfo* cls = classOf(object);
    if (cls && cls->findTrait(name)) {
        return true;
    }
    if (self->host && self->host->get(name)) {
        return true;
    }
    for (const ClassInfo* at = cls; at; at = at->base.get()) {
        if (at->prototype.count(name)) return true;
    }
    return false;
}

// Public property access

Val VM::getProperty(const Val& object, const std::string& name) {
    return getProp(object, name, nullptr);
}

void VM::setProperty(const Val& object, const std::string& name, const Val& value) {
    setProp(object, name, value, nullptr);
}

bool VM::hasProperty(const Val& object, const std::string& name) {
    return hasKey(object, Val::string(name));
}

Val VM::getIndex(const Val& object, const Val& key) {
    return getIndexOf(object, key);
}

void VM::setIndex(const Val& object, const Val& key, const Val& value) {
    setIndexOf(object, key, value);
}

Val VM::callMethod(const Val& object, const std::string& name, Args args) {
    if (object.isNullish()) {
        nullError(object, name);
    }
    Val function;
    const ClassInfo* cls = object.isObject() && object->type == ValueType::Class ? nullptr : classOf(object);
    const Trait* trait = cls ? cls->findTrait(name) : nullptr;
    if (trait && trait->kind == TraitKind::Method) {
        function = trait->method;
    } else {
        function = getProp(object, name, nullptr);
    }
    if (!function.isFunction() && !function.isClass()) {
        throwError("TypeError", "Error #1006: " + name + " is not a function.", 1006);
    }
    return callFunction(function, object, args);
}

Val VM::callAsFunction(ClassInfo* cls, const Val& classValue, Args args) {
    if (cls->callHandler) {
        return cls->callHandler(*this, classValue, args);
    }
    // A cast: the value if it is one, else an error.
    if (args.size() == 1) {
        if (args[0].isNullish() || isType(args[0], classValue)) {
            return args[0].isUndefined() ? Val::null() : args[0];
        }
        throwError("TypeError",
                   "Error #1034: Type Coercion failed: cannot convert " + toString(args[0]) + " to " + cls->name + ".",
                   1034);
    }
    throwError("ArgumentError",
               "Error #1112: Argument count mismatch on class coercion.  Expected 1, got " +
                   std::to_string(args.size()) + ".",
               1112);
}

// Names

bool VM::findGlobal(const std::string& name, Val& value) {
    for (VM* vm = this; vm; vm = vm->parent) {
        auto found = vm->globals.find(name);
        if (found != vm->globals.end()) {
            value = found->second;
            return true;
        }
    }
    return false;
}

Val* VM::globalSlot(const std::string& name) {
    for (VM* vm = this; vm; vm = vm->parent) {
        auto found = vm->globals.find(name);
        if (found != vm->globals.end()) {
            return &found->second;
        }
    }
    return nullptr;
}

bool VM::lookupName(CallFrame& frame, const std::string& name, Val& value, Val* receiver, PropertyCache* cache) {
    const Val& self = frame.scopeSelf();
    // The instance's members (its class's, native ones too), then the
    // class's statics.
    if (self.isObject() && findOnObject(self, name, value, true)) {
        if (receiver) *receiver = self;
        return true;
    }
    for (ClassInfo* at = frame.owner; at; at = at->base.get()) {
        if (const Trait* trait = at->findStatic(name)) {
            Val owner = classValue(at);
            value = readTrait(owner, *trait, true);
            if (receiver) *receiver = owner;
            return true;
        }
    }
    if (frame.scope.isObject()) {
        // On a timeline (a frame script, or a function the host runs on
        // one), a global wins over the timeline's own properties: the
        // global gotoAndStop over the clip's.
        if (findGlobal(name, value)) {
            if (receiver) *receiver = Val();
            return true;
        }
        if (self.isObject() && self.ref != frame.scope.ref && findOnObject(self, name, value, false)) {
            if (receiver) *receiver = self;
            return true;
        }
        if (findOnObject(frame.scope, name, value, false)) {
            if (receiver) *receiver = frame.scope;
            return true;
        }
    } else if (self.isObject() && findOnObject(self, name, value, false)) {
        // An instance's dynamic properties, and a host object's.
        if (receiver) *receiver = self;
        return true;
    }
    bool found = false;
    const CompileUnit* unit = frame.code ? frame.code->unit.get() : nullptr;
    if (unit) {
        auto imported = unit->imports.find(name);
        if (imported != unit->imports.end()) {
            value = lookupQualified(imported->second, true);
            found = !value.isUndefined();
        }
        if (!found && !unit->packageName.empty()) {
            value = lookupQualified(unit->packageName + "." + name, true);
            found = !value.isUndefined();
        }
    }
    if (!found) {
        found = findGlobal(name, value);
    }
    if (!found && unit) {
        for (const auto& wildcard : unit->wildcards) {
            value = lookupQualified(wildcard + "." + name, true);
            if (!value.isUndefined()) {
                found = true;
                break;
            }
        }
    }
    if (!found) {
        value = lookupQualified(name, true);
        found = !value.isUndefined();
        // A global that is undefined is still a name.
        if (!found && findGlobal(name, value)) {
            found = true;
        }
    }
    if (!found) {
        // The first part of a package name, as in a.b.C: what it holds.
        value = packageObject(name);
        found = !value.isUndefined();
    }
    if (!found) {
        return false;
    }
    if (receiver) *receiver = Val();
    if (cache) {
        cache->version = namesVersion;
        cache->classId = self.isObject() && self->cls ? self->cls->id : 0;
        cache->value = value;
    }
    return true;
}

void VM::assignName(CallFrame& frame, const std::string& name, const Val& value) {
    const Val& self = frame.scopeSelf();
    if (self.isObject() && setOnObject(self, name, value, true)) {
        return;
    }
    for (ClassInfo* at = frame.owner; at; at = at->base.get()) {
        if (const Trait* trait = at->findStatic(name)) {
            writeTrait(classValue(at), *trait, value, name, true);
            return;
        }
    }
    if (frame.scope.isObject()) {
        // On a timeline: a global already there, else the timeline's.
        if (Val* global = globalSlot(name)) {
            *global = value;
            ++namesVersion;
            return;
        }
        if (self.isObject() && self.ref != frame.scope.ref && setOnObject(self, name, value, false)) {
            return;
        }
        setProp(frame.scope, name, value, nullptr);
        return;
    }
    if (self.isObject() && setOnObject(self, name, value, false)) {
        return;
    }
    if (Val* global = globalSlot(name)) {
        *global = value;
        ++namesVersion;
        return;
    }
    globals[name] = value;
    ++namesVersion;
}

void VM::defineVar(CallFrame& frame, const std::string& name, const Val& value, bool onlyIfMissing) {
    if (name.find('.') != std::string::npos) {
        if (!onlyIfMissing || !definitions.count(name)) {
            registerDefinition(name, value);
        }
        return;
    }
    if (onlyIfMissing) {
        Val existing;
        if (frame.scope.isObject()) {
            if (findOnObject(frame.scope, name, existing)) return;
            setProp(frame.scope, name, value, nullptr);
            return;
        }
        if (globals.count(name)) return;
        globals[name] = value;
        ++namesVersion;
        return;
    }
    // A function: a global already there, else the timeline's.
    if (Val* global = globalSlot(name)) {
        *global = value;
        ++namesVersion;
        return;
    }
    if (frame.scope.isObject()) {
        setProp(frame.scope, name, value, nullptr);
        return;
    }
    globals[name] = value;
    ++namesVersion;
}

// Definitions

namespace {

// A package as a value, as a.b in a.b.C: its members are the definitions
// under it.
class PackageObject : public HostObject {
public:
    PackageObject(VM& vm, std::string name) : _vm(vm), _name(std::move(name)) {}
    ValuePtr get(const std::string& member) override {
        Val found = _vm.findDefinition(_name + "." + member);
        if (!found.isUndefined()) return found;
        Val inner = _vm.packageObject(_name + "." + member);
        if (!inner.isUndefined()) return inner;
        return nullptr;
    }

private:
    VM& _vm;
    std::string _name;
};

} // namespace

Val VM::packageObject(const std::string& name) {
    const std::string prefix = name + ".";
    for (VM* vm = this; vm; vm = vm->parent) {
        for (const auto& definition : vm->definitions) {
            if (definition.first.compare(0, prefix.size(), prefix) == 0) {
                ValuePtr object = Value::makeHostObject(std::make_shared<PackageObject>(*this, name));
                return Val(object);
            }
        }
    }
    return Val();
}

void VM::registerDefinition(const std::string& qualifiedName, const Val& value) {
    const std::string name = normalizeName(qualifiedName);
    definitions[name] = value;
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) {
        globals[name] = value;
    } else if (!globals.count(name.substr(dot + 1))) {
        // By its short name too, unless that is taken (frame scripts and
        // the host find classes so).
        globals[name.substr(dot + 1)] = value;
    }
    missing.erase(name);
    ++namesVersion;
}

Val VM::lookupQualified(const std::string& qualifiedName, bool askResolver) {
    const std::string name = normalizeName(qualifiedName);
    for (VM* vm = this; vm; vm = vm->parent) {
        auto found = vm->definitions.find(name);
        if (found != vm->definitions.end()) {
            return found->second;
        }
    }
    if (name.find('.') == std::string::npos) {
        Val value;
        if (findGlobal(name, value) && !value.isUndefined()) {
            return value;
        }
    }
    if (!askResolver || !classResolver || missing.count(name) || loading.count(name)) {
        return Val();
    }
    std::optional<ClassSource> source = classResolver(name);
    if (!source) {
        missing.insert(name);
        return Val();
    }
    loading.insert(name);
    struct Loading {
        std::set<std::string>& names;
        const std::string& name;
        ~Loading() { names.erase(name); }
    } guard{loading, name};
    loadSource(*source);
    auto found = definitions.find(name);
    if (found != definitions.end()) {
        return found->second;
    }
    missing.insert(name);
    return Val();
}

void VM::loadSource(const ClassSource& source) {
    Parser parser(source.source, source.fileName);
    if (includeResolver) {
        IncludeResolver resolver = includeResolver;
        const std::string fileName = source.fileName;
        parser.setIncludeHandler([resolver, fileName](const std::string& path) { return resolver(path, fileName); });
    }
    auto program = parser.parse();
    if (!parser.getWarnings().empty()) {
        warnings.push_back(parser.getWarnings());
    }
    if (parser.hasErrors()) {
        std::string first = parser.getErrors();
        first = first.substr(0, first.find('\n'));
        throwError("SyntaxError", first, 1084);
    }
    std::shared_ptr<BytecodeFunction> code;
    try {
        Compiler compiler;
        code = compiler.compile(program.get());
        for (const auto& warning : compiler.getWarnings()) {
            warnings.push_back(warning);
        }
    } catch (const CompileError& failed) {
        std::string message = failed.what();
        if (message.size() > 1 && message.back() == '\n') message.pop_back();
        throwError("SyntaxError", message, 1084);
    }
    Val script = makeClosure(code, nullptr);
    callFunction(script, Val(), Args());
}

Val VM::findDefinition(const std::string& qualifiedName) {
    return lookupQualified(qualifiedName, true);
}

std::shared_ptr<ClassInfo> VM::findClass(const std::string& qualifiedName) {
    Val value = findDefinition(qualifiedName);
    if (value.isClass()) {
        return value->cls;
    }
    return nullptr;
}

void VM::defineFunction(const std::string& qualifiedName, NativeMethod function, int length) {
    registerDefinition(qualifiedName, makeFunction(shortNameOf(qualifiedName), std::move(function), length));
}

void VM::defineValue(const std::string& qualifiedName, const Val& value) {
    registerDefinition(qualifiedName, value);
}

Val VM::resolveTypeName(const std::string& typeName, const CompileUnit* unit) {
    if (typeName.empty() || typeName == "*") {
        return Val::null();
    }
    const size_t open = typeName.find(".<");
    if (open != std::string::npos && typeName.back() == '>') {
        Val base = resolveTypeName(typeName.substr(0, open), unit);
        Val element = resolveTypeName(typeName.substr(open + 2, typeName.size() - open - 3), unit);
        return applyType(base, {element});
    }
    if (typeName.find('.') != std::string::npos) {
        return lookupQualified(typeName, true);
    }
    if (unit) {
        auto imported = unit->imports.find(typeName);
        if (imported != unit->imports.end()) {
            Val value = lookupQualified(imported->second, true);
            if (!value.isUndefined()) return value;
        }
        if (!unit->packageName.empty()) {
            Val value = lookupQualified(unit->packageName + "." + typeName, true);
            if (!value.isUndefined()) return value;
        }
    }
    Val value;
    if (findGlobal(typeName, value) && !value.isUndefined()) {
        return value;
    }
    if (unit) {
        for (const auto& wildcard : unit->wildcards) {
            value = lookupQualified(wildcard + "." + typeName, true);
            if (!value.isUndefined()) return value;
        }
    }
    return lookupQualified(typeName, true);
}

// Classes

Val VM::defineClassFromTemplate(const std::shared_ptr<ClassTemplate>& klass) {
    const CompileUnit* unit = klass->unit.get();
    std::shared_ptr<ClassInfo> base;
    if (!klass->isInterface) {
        if (klass->superName.empty()) {
            base = core.object;
        } else {
            Val found = resolveTypeName(klass->superName, unit);
            if (found.isClass()) {
                base = found->cls;
            } else {
                warnings.push_back((unit && !unit->fileName.empty() ? unit->fileName + ": " : std::string()) +
                                   "The superclass " + klass->superName + " of " + klass->qualifiedName +
                                   " is not defined; it extends Object instead.");
                base = core.object;
            }
        }
    }

    auto cls = std::make_shared<ClassInfo>();
    cls->name = klass->qualifiedName;
    cls->shortName = klass->name;
    cls->packageName = klass->packageName;
    cls->base = base;
    cls->isInterface = klass->isInterface;
    cls->isDynamic = klass->isDynamic;
    cls->isFinal = klass->isFinal;
    cls->instanceType = base ? base->instanceType : ValueType::Object;
    if (base) {
        cls->traits = base->traits;
        cls->slotDefaults = base->slotDefaults;
        cls->isVector = false;
    }
    for (const auto& faceName : klass->interfaceNames) {
        Val face = resolveTypeName(faceName, unit);
        if (face.isClass()) {
            cls->interfaces.push_back(face->cls);
        } else {
            warnings.push_back("The interface " + faceName + " of " + klass->qualifiedName + " is not defined.");
        }
    }
    if (!klass->metadata.empty()) {
        cls->metadata = std::make_shared<std::vector<Metadata>>(klass->metadata);
    }

    auto object = std::make_shared<Value>();
    object->type = ValueType::Class;
    object->cls = cls;
    cls->classObject = object;
    track(object);
    Val classVal(object);

    const auto privateKey = [&klass](const MemberTemplate& member) {
        return klass->qualifiedName + "::" + member.name;
    };
    for (const MemberTemplate& member : klass->members) {
        const Coerce coerce = coerceForType(member.typeName);
        std::unordered_map<std::string, Trait>& table = member.isStatic ? cls->statics : cls->traits;
        std::shared_ptr<std::vector<Metadata>> metadata;
        if (!member.metadata.empty()) {
            metadata = std::make_shared<std::vector<Metadata>>(member.metadata);
        }
        switch (member.kind) {
            case MemberTemplate::Kind::Var:
            case MemberTemplate::Kind::Const: {
                Trait trait;
                trait.kind = TraitKind::Slot;
                trait.isConst = member.kind == MemberTemplate::Kind::Const;
                trait.coerce = coerce;
                trait.declaredBy = cls.get();
                trait.typeName = member.typeName;
                trait.metadata = metadata;
                const Val initial = member.hasLiteral ? member.literal : defaultForCoerce(coerce);
                if (member.isStatic) {
                    trait.slot = static_cast<uint32_t>(cls->staticSlots.size());
                    cls->staticSlots.push_back(initial);
                } else {
                    trait.slot = static_cast<uint32_t>(cls->slotDefaults.size());
                    cls->slotDefaults.push_back(initial);
                    object->properties[member.name] = initial;
                }
                table[member.name] = trait;
                if (member.isPrivate) table[privateKey(member)] = trait;
                break;
            }
            case MemberTemplate::Kind::Method: {
                if (!member.function) {
                    break;
                }
                Trait trait;
                trait.kind = TraitKind::Method;
                trait.isFinal = member.isFinal;
                trait.declaredBy = cls.get();
                trait.metadata = metadata;
                trait.method = makeClosure(member.function, cls);
                table[member.name] = trait;
                if (member.isPrivate) table[privateKey(member)] = trait;
                if (!member.isStatic) {
                    object->properties[member.name] = trait.method;
                }
                break;
            }
            case MemberTemplate::Kind::Getter:
            case MemberTemplate::Kind::Setter: {
                if (!member.function) {
                    break;
                }
                Trait trait;
                auto existing = table.find(member.name);
                if (existing != table.end() && existing->second.kind == TraitKind::Accessor) {
                    trait = existing->second;
                }
                trait.kind = TraitKind::Accessor;
                trait.declaredBy = cls.get();
                trait.coerce = coerce;
                trait.typeName = member.typeName;
                if (metadata) trait.metadata = metadata;
                Val function = makeClosure(member.function, cls);
                if (member.kind == MemberTemplate::Kind::Getter) {
                    trait.getter = function;
                } else {
                    trait.setter = function;
                }
                table[member.name] = trait;
                if (member.isPrivate) table[privateKey(member)] = trait;
                break;
            }
        }
    }
    if (klass->constructor) {
        cls->constructor = makeClosure(klass->constructor, cls);
        object->properties[klass->name] = cls->constructor;
    }

    registerDefinition(klass->qualifiedName, classVal);
    if (klass->packageName.empty()) {
        globals[klass->name] = classVal;
    }

    if (klass->staticInit) {
        Val init = makeClosure(klass->staticInit, cls);
        callFunction(init, classVal, Args());
    }
    return classVal;
}

// Vectors

std::shared_ptr<ClassInfo> VM::vectorClass(const Val& elementType) {
    const ClassInfo* element = elementType.isClass() ? elementType->cls.get() : nullptr;
    auto found = vectorClasses.find(element);
    if (found != vectorClasses.end()) {
        return found->second;
    }
    const ClassInfo* generic = core.vector.get();
    auto cls = std::make_shared<ClassInfo>();
    const std::string elementName = element ? element->name : std::string("*");
    cls->name = "__AS3__.vec.Vector.<" + elementName + ">";
    cls->shortName = "Vector.<" + elementName + ">";
    cls->packageName = "__AS3__.vec";
    cls->base = core.object;
    cls->isNative = true;
    cls->isFinal = true;
    cls->isVector = true;
    cls->instanceType = ValueType::Array;
    cls->traits = generic->traits;
    cls->prototype = generic->prototype;
    cls->slotDefaults = generic->slotDefaults;
    cls->nativeConstructor = generic->nativeConstructor;
    cls->callHandler = generic->callHandler;
    cls->elementTypeName = elementName;
    cls->elementCoerce = element ? coerceForType(element->name) : Coerce::None;
    if (element) {
        cls->elementClass = element->classObject.lock() ? element->classObject.lock()->cls : nullptr;
    }
    auto object = std::make_shared<Value>();
    object->type = ValueType::Class;
    object->cls = cls;
    cls->classObject = object;
    vectorClasses[element] = cls;
    // Kept by the VM, as its definitions are.
    definitions["__AS3__.vec.Vector.<" + elementName + ">"] = Val(object);
    return cls;
}

Val VM::applyType(const Val& base, const std::vector<Val>& types) {
    if (!base.isClass() || base->cls.get() != core.vector.get()) {
        throwError("TypeError", "Error #1127: Type application attempted on a non-parameterized type.", 1127);
    }
    if (types.size() != 1) {
        throwError("TypeError", "Error #1128: Incorrect number of type parameters.", 1128);
    }
    return classValue(vectorClass(types[0]).get());
}

// Iteration

Val VM::makeIterator(const Val& object, bool each) {
    auto state = std::make_shared<Value>();
    state->type = ValueType::Object;
    state->boolValue = each;
    state->numberValue = 0;
    state->slots.push_back(object);
    if (object.isObject()) {
        Value* self = object.ref.get();
        if (self->type == ValueType::Array) {
            state->elements.reserve(self->elements.size());
            for (size_t i = 0; i < self->elements.size(); ++i) {
                state->elements.push_back(Val::number(static_cast<double>(i)));
            }
        }
        if (self->host) {
            self->host->enumerate(state->elements);
        }
        for (const auto& property : self->properties) {
            state->elements.push_back(Val::string(property.first));
        }
    }
    return Val(state);
}

// Native classes

NativeClassBuilder VM::defineNativeClass(const std::string& qualifiedName) {
    return NativeClassBuilder(*this, qualifiedName);
}

NativeClassBuilder::NativeClassBuilder(VM& vm, const std::string& qualifiedName)
    : _vm(vm)
    , _info(std::make_shared<ClassInfo>()) {
    const std::string name = normalizeName(qualifiedName);
    _info->name = name;
    _info->shortName = shortNameOf(name);
    const size_t dot = name.find_last_of('.');
    _info->packageName = dot == std::string::npos ? std::string() : name.substr(0, dot);
    _info->isNative = true;
}

NativeClassBuilder& NativeClassBuilder::extends(const std::string& baseName) {
    _baseName = baseName;
    _baseGiven = true;
    return *this;
}

NativeClassBuilder& NativeClassBuilder::implements(const std::string& interfaceName) {
    _interfaces.push_back(interfaceName);
    return *this;
}

NativeClassBuilder& NativeClassBuilder::constructor(NativeMethod constructor) {
    _info->nativeConstructor = std::move(constructor);
    return *this;
}

NativeClassBuilder& NativeClassBuilder::method(const std::string& name, NativeMethod method, int length) {
    Trait trait;
    trait.kind = TraitKind::Method;
    trait.method = _vm.makeFunction(name, std::move(method), length);
    _traits.emplace_back(name, trait);
    return *this;
}

namespace {

Trait& accessorIn(std::vector<std::pair<std::string, Trait>>& traits, const std::string& name) {
    for (auto& entry : traits) {
        if (entry.first == name && entry.second.kind == TraitKind::Accessor) {
            return entry.second;
        }
    }
    Trait trait;
    trait.kind = TraitKind::Accessor;
    traits.emplace_back(name, trait);
    return traits.back().second;
}

} // namespace

NativeClassBuilder& NativeClassBuilder::getter(const std::string& name, NativeMethod getter) {
    accessorIn(_traits, name).getter = _vm.makeFunction(name, std::move(getter), 0);
    return *this;
}

NativeClassBuilder& NativeClassBuilder::setter(const std::string& name, NativeMethod setter) {
    accessorIn(_traits, name).setter = _vm.makeFunction(name, std::move(setter), 1);
    return *this;
}

NativeClassBuilder& NativeClassBuilder::property(const std::string& name, NativeMethod getter, NativeMethod setter) {
    if (getter) this->getter(name, std::move(getter));
    if (setter) this->setter(name, std::move(setter));
    return *this;
}

NativeClassBuilder& NativeClassBuilder::field(const std::string& name, const Val& initial, Coerce coerce) {
    _fields.emplace_back(name, coerceValue(initial, coerce));
    Trait trait;
    trait.kind = TraitKind::Slot;
    trait.coerce = coerce;
    _traits.emplace_back(name, trait);
    return *this;
}

NativeClassBuilder& NativeClassBuilder::staticMethod(const std::string& name, NativeMethod method, int length) {
    Trait trait;
    trait.kind = TraitKind::Method;
    trait.method = _vm.makeFunction(name, std::move(method), length);
    _statics.emplace_back(name, trait);
    return *this;
}

NativeClassBuilder& NativeClassBuilder::staticGetter(const std::string& name, NativeMethod getter) {
    accessorIn(_statics, name).getter = _vm.makeFunction(name, std::move(getter), 0);
    return *this;
}

NativeClassBuilder& NativeClassBuilder::staticSetter(const std::string& name, NativeMethod setter) {
    accessorIn(_statics, name).setter = _vm.makeFunction(name, std::move(setter), 1);
    return *this;
}

NativeClassBuilder& NativeClassBuilder::constant(const std::string& name, const Val& value) {
    Trait trait;
    trait.kind = TraitKind::Slot;
    trait.isConst = true;
    trait.slot = static_cast<uint32_t>(_staticValues.size());
    _staticValues.push_back(value);
    _statics.emplace_back(name, trait);
    return *this;
}

NativeClassBuilder& NativeClassBuilder::staticVar(const std::string& name, const Val& value, Coerce coerce) {
    Trait trait;
    trait.kind = TraitKind::Slot;
    trait.coerce = coerce;
    trait.slot = static_cast<uint32_t>(_staticValues.size());
    _staticValues.push_back(coerceValue(value, coerce));
    _statics.emplace_back(name, trait);
    return *this;
}

NativeClassBuilder& NativeClassBuilder::prototypeMethod(const std::string& name, NativeMethod method, int length) {
    _info->prototype[name] = _vm.makeFunction(name, std::move(method), length);
    return *this;
}

NativeClassBuilder& NativeClassBuilder::callHandler(NativeMethod handler) {
    _info->callHandler = std::move(handler);
    return *this;
}

NativeClassBuilder& NativeClassBuilder::dynamic(bool isDynamic) {
    _info->isDynamic = isDynamic;
    return *this;
}

NativeClassBuilder& NativeClassBuilder::final(bool isFinal) {
    _info->isFinal = isFinal;
    return *this;
}

NativeClassBuilder& NativeClassBuilder::interface(bool isInterface) {
    _info->isInterface = isInterface;
    return *this;
}

Val NativeClassBuilder::define() {
    ClassInfo& cls = *_info;
    std::shared_ptr<ClassInfo> base;
    if (_baseGiven) {
        Val found = _vm.findDefinition(_baseName);
        if (!found.isClass()) {
            throw std::runtime_error("The superclass " + _baseName + " of " + cls.name + " is not defined");
        }
        base = found->cls;
    } else if (!cls.isInterface && _vm.core.object && _info != _vm.core.object) {
        base = _vm.core.object;
    }
    cls.base = base;
    if (base) {
        if (cls.instanceType == ValueType::Object) {
            cls.instanceType = base->instanceType;
        }
        cls.traits = base->traits;
        cls.slotDefaults = base->slotDefaults;
    }
    size_t field = 0;
    for (auto& entry : _traits) {
        Trait trait = entry.second;
        trait.declaredBy = _info.get();
        if (trait.kind == TraitKind::Slot) {
            trait.slot = static_cast<uint32_t>(cls.slotDefaults.size());
            cls.slotDefaults.push_back(_fields[field++].second);
        } else if (trait.kind == TraitKind::Accessor) {
            // Overriding one half keeps the superclass's other half.
            auto inherited = cls.traits.find(entry.first);
            if (inherited != cls.traits.end() && inherited->second.kind == TraitKind::Accessor) {
                if (!trait.getter.isObject()) trait.getter = inherited->second.getter;
                if (!trait.setter.isObject()) trait.setter = inherited->second.setter;
            }
        }
        cls.traits[entry.first] = trait;
    }
    cls.staticSlots = _staticValues;
    for (auto& entry : _statics) {
        Trait trait = entry.second;
        trait.declaredBy = _info.get();
        cls.statics[entry.first] = trait;
    }
    for (const auto& faceName : _interfaces) {
        if (auto face = _vm.findClass(faceName)) {
            cls.interfaces.push_back(face);
        }
    }
    auto object = std::make_shared<Value>();
    object->type = ValueType::Class;
    object->cls = _info;
    cls.classObject = object;
    Val classVal(object);
    _vm.registerDefinition(cls.name, classVal);
    return classVal;
}

// Timers

void VM::runTimers(double nowMs) {
    for (;;) {
        auto due = std::min_element(timers.begin(), timers.end(),
                                    [](const TimerEntry& a, const TimerEntry& b) { return a.due < b.due; });
        if (due == timers.end() || due->due > nowMs) {
            return;
        }
        TimerEntry entry = *due;
        if (entry.interval > 0) {
            due->due += std::max(entry.interval, 1.0);
        } else {
            timers.erase(due);
        }
        try {
            callFunction(entry.function, Val(), Args(entry.args.data(), entry.args.size()));
        } catch (ScriptException& exception) {
            reportError(exception);
        } catch (const std::exception& failed) {
            hadError = true;
            errorMessage = failed.what();
        }
    }
}

} // namespace as3
