#include "value.h"
#include <sstream>
#include <cmath>

namespace as3 {

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
    v->bytecodeFunc = func;
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
    v->className = name;
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
        case ValueType::Object:
        case ValueType::Array:
        case ValueType::Function:
        case ValueType::NativeFunction:
            return true;
        default:
            return false;
    }
}

double Value::toNumber() const {
    switch (type) {
        case ValueType::Undefined:
            return std::nan("");
        case ValueType::Null:
            return 0;
        case ValueType::Boolean:
            return boolValue ? 1 : 0;
        case ValueType::Number:
            return numberValue;
        case ValueType::String:
            if (stringValue.empty()) return 0;
            try {
                return std::stod(stringValue);
            } catch (...) {
                return std::nan("");
            }
        default:
            return std::nan("");
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
        case ValueType::Number: {
            if (std::isnan(numberValue)) return "NaN";
            if (std::isinf(numberValue)) return numberValue > 0 ? "Infinity" : "-Infinity";
            // Check if it's an integer
            if (numberValue == static_cast<int64_t>(numberValue)) {
                return std::to_string(static_cast<int64_t>(numberValue));
            }
            std::ostringstream oss;
            oss << numberValue;
            return oss.str();
        }
        case ValueType::String:
            return stringValue;
        case ValueType::Array: {
            std::string result = "[";
            for (size_t i = 0; i < elements.size(); i++) {
                if (i > 0) result += ",";
                if (elements[i]) {
                    result += elements[i]->toString();
                }
            }
            result += "]";
            return result;
        }
        case ValueType::Object:
            return "[object Object]";
        case ValueType::Function:
        case ValueType::NativeFunction:
            return "[function]";
        case ValueType::Class:
            return "[class " + className + "]";
        default:
            return "";
    }
}

ValuePtr Value::getProperty(const std::string& name) {
    if (type == ValueType::Array && name == "length") {
        return makeNumber(static_cast<double>(elements.size()));
    }

    auto it = properties.find(name);
    if (it != properties.end()) {
        return it->second;
    }
    return makeUndefined();
}

void Value::setProperty(const std::string& name, ValuePtr value) {
    properties[name] = value;
}

ValuePtr Value::getElement(size_t index) {
    if (index < elements.size()) {
        return elements[index];
    }
    return makeUndefined();
}

void Value::setElement(size_t index, ValuePtr value) {
    if (index >= elements.size()) {
        elements.resize(index + 1, makeUndefined());
    }
    elements[index] = value;
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
        default: return "unknown";
    }
}

} // namespace as3
