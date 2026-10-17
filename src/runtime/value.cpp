#include "value.h"
#include "../bytecode/chunk.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <atomic>

namespace as3 {

namespace {

// Shared values for what has no state of its own.
const ValuePtr& sharedUndefined() {
    static const ValuePtr value = [] {
        auto v = std::make_shared<Value>();
        v->type = ValueType::Undefined;
        return v;
    }();
    return value;
}

const ValuePtr& sharedNull() {
    static const ValuePtr value = [] {
        auto v = std::make_shared<Value>();
        v->type = ValueType::Null;
        return v;
    }();
    return value;
}

const ValuePtr& sharedBoolean(bool flag) {
    static const ValuePtr yes = [] {
        auto v = std::make_shared<Value>();
        v->type = ValueType::Boolean;
        v->boolValue = true;
        return v;
    }();
    static const ValuePtr no = [] {
        auto v = std::make_shared<Value>();
        v->type = ValueType::Boolean;
        return v;
    }();
    return flag ? yes : no;
}

} // namespace

// Val

Val::Val(const ValuePtr& value) : tag(Tag::Undefined), num(0) {
    if (!value) {
        return;
    }
    switch (value->type) {
        case ValueType::Undefined:
            break;
        case ValueType::Null:
            tag = Tag::Null;
            break;
        case ValueType::Boolean:
            tag = Tag::Boolean;
            flag = value->boolValue;
            break;
        case ValueType::Number:
            tag = Tag::Number;
            num = value->numberValue;
            break;
        case ValueType::String:
            tag = Tag::String;
            ref = value;
            break;
        default:
            tag = Tag::Object;
            ref = value;
            break;
    }
}

Val::operator ValuePtr() const {
    switch (tag) {
        case Tag::Undefined:
            return sharedUndefined();
        case Tag::Null:
            return sharedNull();
        case Tag::Boolean:
            return sharedBoolean(flag);
        case Tag::Number:
            return Value::makeNumber(num);
        default:
            return ref;
    }
}

Val Val::string(const std::string& value) {
    Val v;
    v.tag = Tag::String;
    v.ref = std::make_shared<Value>();
    v.ref->type = ValueType::String;
    v.ref->stringValue = value;
    return v;
}

Val Val::string(std::string&& value) {
    Val v;
    v.tag = Tag::String;
    v.ref = std::make_shared<Value>();
    v.ref->type = ValueType::String;
    v.ref->stringValue = std::move(value);
    return v;
}

Val Val::object(ValuePtr value) {
    if (!value) {
        return null();
    }
    return Val(value);
}

bool Val::isFunction() const {
    return tag == Tag::Object && ref->isFunction();
}

bool Val::isArray() const {
    return tag == Tag::Object && ref->type == ValueType::Array;
}

bool Val::isClass() const {
    return tag == Tag::Object && ref->type == ValueType::Class;
}

const std::string& Val::str() const {
    static const std::string none;
    return tag == Tag::String ? ref->stringValue : none;
}

bool Val::toBoolean() const {
    switch (tag) {
        case Tag::Undefined:
        case Tag::Null:
            return false;
        case Tag::Boolean:
            return flag;
        case Tag::Number:
            return num != 0 && !std::isnan(num);
        case Tag::String:
            return !ref->stringValue.empty();
        default:
            return true;
    }
}

double Val::toNumber() const {
    switch (tag) {
        case Tag::Undefined:
            return std::numeric_limits<double>::quiet_NaN();
        case Tag::Null:
            return 0;
        case Tag::Boolean:
            return flag ? 1 : 0;
        case Tag::Number:
            return num;
        case Tag::String:
            return stringToNumber(ref->stringValue);
        default:
            return ref->toNumber();
    }
}

std::string Val::toString() const {
    switch (tag) {
        case Tag::Undefined:
            return "undefined";
        case Tag::Null:
            return "null";
        case Tag::Boolean:
            return flag ? "true" : "false";
        case Tag::Number:
            return numberToString(num);
        case Tag::String:
            return ref->stringValue;
        default:
            return ref->toString();
    }
}

int32_t Val::toInt32() const {
    return tag == Tag::Number ? as3::toInt32(num) : as3::toInt32(toNumber());
}

uint32_t Val::toUint32() const {
    return tag == Tag::Number ? as3::toUint32(num) : as3::toUint32(toNumber());
}

bool Val::strictEquals(const Val& other) const {
    if (tag != other.tag) {
        return false;
    }
    switch (tag) {
        case Tag::Undefined:
        case Tag::Null:
            return true;
        case Tag::Boolean:
            return flag == other.flag;
        case Tag::Number:
            return num == other.num;
        case Tag::String:
            return ref == other.ref || ref->stringValue == other.ref->stringValue;
        default:
            if (ref == other.ref) {
                return true;
            }
            // Two closures of one method on one object are the same.
            if (ref->function && other.ref->function && ref->function->bound && other.ref->function->bound) {
                const FunctionInfo& a = *ref->function;
                const FunctionInfo& b = *other.ref->function;
                return a.code && a.code == b.code && a.boundThis.strictEquals(b.boundThis);
            }
            return false;
    }
}

// Args

const Val& Args::operator[](size_t i) const {
    static const Val none;
    return i < _count ? _data[i] : none;
}

double Args::number(size_t i, double otherwise) const {
    return i < _count ? _data[i].toNumber() : otherwise;
}

int32_t Args::integer(size_t i, int32_t otherwise) const {
    return i < _count ? _data[i].toInt32() : otherwise;
}

std::string Args::string(size_t i, const std::string& otherwise) const {
    return i < _count ? _data[i].toString() : otherwise;
}

bool Args::boolean(size_t i, bool otherwise) const {
    return i < _count ? _data[i].toBoolean() : otherwise;
}

// Numbers

int32_t toInt32(double value) {
    if (value >= -2147483648.0 && value <= 2147483647.0) {
        return static_cast<int32_t>(value);
    }
    if (std::isnan(value) || std::isinf(value)) {
        return 0;
    }
    double wrapped = std::fmod(std::trunc(value), 4294967296.0);
    if (wrapped < 0) {
        wrapped += 4294967296.0;
    }
    return static_cast<int32_t>(static_cast<uint32_t>(wrapped));
}

uint32_t toUint32(double value) {
    if (value >= 0 && value <= 4294967295.0) {
        return static_cast<uint32_t>(value);
    }
    if (std::isnan(value) || std::isinf(value)) {
        return 0;
    }
    double wrapped = std::fmod(std::trunc(value), 4294967296.0);
    if (wrapped < 0) {
        wrapped += 4294967296.0;
    }
    return static_cast<uint32_t>(wrapped);
}

std::string numberToString(double value) {
    if (std::isnan(value)) return "NaN";
    if (value == 0) return "0";
    if (std::isinf(value)) return value > 0 ? "Infinity" : "-Infinity";
    // Whole numbers that fit print as they are.
    if (value == std::trunc(value) && std::fabs(value) < 1e21) {
        if (std::fabs(value) < 9007199254740992.0) {
            char text[32];
            std::snprintf(text, sizeof(text), "%lld", static_cast<long long>(value));
            return text;
        }
    }
    const bool negative = value < 0;
    const double magnitude = std::fabs(value);
    // The fewest digits that read back as the same number.
    char buffer[40];
    int precision = 1;
    for (; precision <= 17; ++precision) {
        std::snprintf(buffer, sizeof(buffer), "%.*e", precision - 1, magnitude);
        if (std::strtod(buffer, nullptr) == magnitude) {
            break;
        }
    }
    // buffer is d.ddde[+-]xx: the digits and the exponent.
    std::string digits;
    const char* p = buffer;
    for (; *p && *p != 'e'; ++p) {
        if (*p >= '0' && *p <= '9') {
            digits += *p;
        }
    }
    const int exponent = std::atoi(p + 1);
    while (digits.size() > 1 && digits.back() == '0') {
        digits.pop_back();
    }
    const int k = static_cast<int>(digits.size());
    const int n = exponent + 1;
    std::string out = negative ? "-" : "";
    if (k <= n && n <= 21) {
        out += digits + std::string(static_cast<size_t>(n - k), '0');
    } else if (0 < n && n <= 21) {
        out += digits.substr(0, static_cast<size_t>(n)) + "." + digits.substr(static_cast<size_t>(n));
    } else if (-6 < n && n <= 0) {
        out += "0." + std::string(static_cast<size_t>(-n), '0') + digits;
    } else {
        out += digits.substr(0, 1);
        if (k > 1) {
            out += "." + digits.substr(1);
        }
        out += "e";
        out += (n - 1 >= 0) ? "+" : "-";
        out += std::to_string(std::abs(n - 1));
    }
    return out;
}

double stringToNumber(const std::string& text) {
    const char* spaces = " \t\r\n\f\v";
    const size_t start = text.find_first_not_of(spaces);
    if (start == std::string::npos) {
        return 0;
    }
    const size_t end = text.find_last_not_of(spaces) + 1;
    const std::string body = text.substr(start, end - start);
    const char* s = body.c_str();
    // 0x hex, with an optional sign.
    size_t at = 0;
    bool negative = false;
    if (s[0] == '+' || s[0] == '-') {
        negative = s[0] == '-';
        at = 1;
    }
    if (s[at] == '0' && (s[at + 1] == 'x' || s[at + 1] == 'X')) {
        double value = 0;
        size_t i = at + 2;
        if (!s[i]) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        for (; s[i]; ++i) {
            const char c = s[i];
            int digit;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            else return std::numeric_limits<double>::quiet_NaN();
            value = value * 16 + digit;
        }
        return negative ? -value : value;
    }
    if (std::strcmp(s + at, "Infinity") == 0) {
        return negative ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
    }
    // Digits, a point and an exponent only: strtod would take "inf" or
    // "nan" too.
    for (size_t i = at; s[i]; ++i) {
        const char c = s[i];
        if (!((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')) {
            return std::numeric_limits<double>::quiet_NaN();
        }
    }
    char* used = nullptr;
    const double value = std::strtod(s, &used);
    if (used == s || *used != '\0') {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return value;
}

Coerce coerceForType(const std::string& typeName) {
    if (typeName.empty() || typeName == "*" || typeName == "void") return Coerce::None;
    if (typeName == "int") return Coerce::Int;
    if (typeName == "uint") return Coerce::Uint;
    if (typeName == "Number") return Coerce::Number;
    if (typeName == "String") return Coerce::String;
    if (typeName == "Boolean") return Coerce::Boolean;
    return Coerce::Nullable;
}

Val coerceValue(const Val& value, Coerce coerce) {
    switch (coerce) {
        case Coerce::None:
            return value;
        case Coerce::Int:
            if (value.tag == Val::Tag::Number) {
                const double n = value.num;
                if (n >= -2147483648.0 && n <= 2147483647.0 && n == static_cast<double>(static_cast<int32_t>(n))) {
                    return value;
                }
            }
            return Val::number(value.toInt32());
        case Coerce::Uint:
            return Val::number(value.toUint32());
        case Coerce::Number:
            return value.tag == Val::Tag::Number ? value : Val::number(value.toNumber());
        case Coerce::String:
            if (value.tag == Val::Tag::String || value.isNull()) return value;
            if (value.isUndefined()) return Val::null();
            return Val::string(value.toString());
        case Coerce::Boolean:
            return value.tag == Val::Tag::Boolean ? value : Val::boolean(value.toBoolean());
        case Coerce::Nullable:
            return value.isUndefined() ? Val::null() : value;
    }
    return value;
}

Val defaultForCoerce(Coerce coerce) {
    switch (coerce) {
        case Coerce::Int:
        case Coerce::Uint:
            return Val::number(0);
        case Coerce::Number:
            return Val::number(std::numeric_limits<double>::quiet_NaN());
        case Coerce::Boolean:
            return Val::boolean(false);
        case Coerce::String:
        case Coerce::Nullable:
            return Val::null();
        default:
            return Val();
    }
}

// ClassInfo

ClassInfo::ClassInfo() {
    static std::atomic<uint64_t> next{1};
    id = next++;
}

bool ClassInfo::isSubclassOf(const ClassInfo* other) const {
    if (!other) {
        return false;
    }
    for (const ClassInfo* at = this; at; at = at->base.get()) {
        if (at == other) {
            return true;
        }
        for (const auto& face : at->interfaces) {
            if (face && face->isSubclassOf(other)) {
                return true;
            }
        }
    }
    return false;
}

// Value

ValuePtr Value::makeUndefined() {
    auto v = std::make_shared<Value>();
    v->type = ValueType::Undefined;
    return v;
}

ValuePtr Value::makeNull() {
    auto v = std::make_shared<Value>();
    v->type = ValueType::Null;
    return v;
}

ValuePtr Value::makeBoolean(bool value) {
    auto v = std::make_shared<Value>();
    v->type = ValueType::Boolean;
    v->boolValue = value;
    return v;
}

ValuePtr Value::makeNumber(double value) {
    auto v = std::make_shared<Value>();
    v->type = ValueType::Number;
    v->numberValue = value;
    return v;
}

ValuePtr Value::makeString(const std::string& value) {
    auto v = std::make_shared<Value>();
    v->type = ValueType::String;
    v->stringValue = value;
    return v;
}

ValuePtr Value::makeObject() {
    auto v = std::make_shared<Value>();
    v->type = ValueType::Object;
    return v;
}

ValuePtr Value::makeArray() {
    auto v = std::make_shared<Value>();
    v->type = ValueType::Array;
    return v;
}

ValuePtr Value::makeFunction(std::shared_ptr<BytecodeFunction> func) {
    auto v = std::make_shared<Value>();
    v->type = ValueType::Function;
    v->function = std::make_shared<FunctionInfo>();
    if (func) {
        v->function->name = func->name;
        v->function->length = func->arity;
    }
    v->function->code = std::move(func);
    return v;
}

ValuePtr Value::makeNativeFunction(NativeFunction func) {
    auto v = std::make_shared<Value>();
    v->type = ValueType::NativeFunction;
    v->nativeFunc = func;
    return v;
}

ValuePtr Value::makeClass(const std::string& name) {
    auto v = std::make_shared<Value>();
    v->type = ValueType::Class;
    v->cls = std::make_shared<ClassInfo>();
    v->cls->name = name;
    const size_t dot = name.find_last_of('.');
    v->cls->shortName = dot == std::string::npos ? name : name.substr(dot + 1);
    v->cls->packageName = dot == std::string::npos ? std::string() : name.substr(0, dot);
    v->cls->classObject = v;
    return v;
}

ValuePtr Value::makeHostObject(std::shared_ptr<HostObject> host) {
    auto v = std::make_shared<Value>();
    v->type = ValueType::Object;
    v->host = std::move(host);
    return v;
}

bool Value::toBoolean() const {
    switch (type) {
        case ValueType::Undefined:
        case ValueType::Null:
            return false;
        case ValueType::Boolean:
            return boolValue;
        case ValueType::Number:
            return numberValue != 0 && !std::isnan(numberValue);
        case ValueType::String:
            return !stringValue.empty();
        default:
            return true;
    }
}

double Value::toNumber() const {
    switch (type) {
        case ValueType::Undefined:
            return std::numeric_limits<double>::quiet_NaN();
        case ValueType::Null:
            return 0;
        case ValueType::Boolean:
            return boolValue ? 1 : 0;
        case ValueType::Number:
            return numberValue;
        case ValueType::String:
            return stringToNumber(stringValue);
        case ValueType::Array:
            // [] is 0, [5] is 5: as its text.
            return stringToNumber(toString());
        default:
            return std::numeric_limits<double>::quiet_NaN();
    }
}

std::string Value::toString() const {
    switch (type) {
        case ValueType::Undefined:
            return "undefined";
        case ValueType::Null:
            return "null";
        case ValueType::Boolean:
            return boolValue ? "true" : "false";
        case ValueType::Number:
            return numberToString(numberValue);
        case ValueType::String:
            return stringValue;
        case ValueType::Array: {
            std::string result;
            for (size_t i = 0; i < elements.size(); i++) {
                if (i > 0) result += ",";
                if (!elements[i].isNullish()) {
                    result += elements[i].toString();
                }
            }
            return result;
        }
        case ValueType::Object:
            return "[object " + (cls ? cls->shortName : std::string("Object")) + "]";
        case ValueType::Function:
        case ValueType::NativeFunction:
            return "function Function() {}";
        case ValueType::Class:
            return "[class " + (cls ? cls->shortName : std::string()) + "]";
        default:
            return "";
    }
}

ValuePtr Value::getProperty(const std::string& name) {
    if (type == ValueType::Array && name == "length") {
        return makeNumber(static_cast<double>(elements.size()));
    }
    if (cls && type != ValueType::Class) {
        if (const Trait* trait = cls->findTrait(name)) {
            if (trait->kind == TraitKind::Slot && trait->slot < slots.size()) {
                return slots[trait->slot];
            }
            if (trait->kind == TraitKind::Method) {
                return trait->method;
            }
        }
    }
    if (host) {
        if (ValuePtr value = host->get(name)) {
            return value;
        }
    }

    if (const Val* found = properties.lookup(name)) {
        return *found;
    }
    return makeUndefined();
}

void Value::setProperty(const std::string& name, ValuePtr value) {
    if (cls && type != ValueType::Class) {
        if (const Trait* trait = cls->findTrait(name)) {
            if (trait->kind == TraitKind::Slot && trait->slot < slots.size()) {
                slots[trait->slot] = coerceValue(Val(value), trait->coerce);
                return;
            }
        }
    }
    if (host && host->set(name, value)) {
        return;
    }
    properties[name] = Val(value);
}

ValuePtr Value::getElement(size_t index) {
    if (index < elements.size()) {
        return elements[index];
    }
    return makeUndefined();
}

void Value::setElement(size_t index, ValuePtr value) {
    if (index >= elements.size()) {
        elements.resize(index + 1);
    }
    elements[index] = Val(value);
}

std::string Value::typeName() const {
    switch (type) {
        case ValueType::Undefined: return "undefined";
        case ValueType::Null: return "null";
        case ValueType::Boolean: return "boolean";
        case ValueType::Number: return "number";
        case ValueType::String: return "string";
        case ValueType::Object: return "object";
        case ValueType::Array: return "array";
        case ValueType::Function:
        case ValueType::NativeFunction: return "function";
        case ValueType::Class: return "class";
        default: return "unknown";
    }
}

} // namespace as3
