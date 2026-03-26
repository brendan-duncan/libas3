#pragma once

#include <string>
#include <memory>
#include <map>
#include <vector>
#include <functional>

namespace as3 {

class Value;
class BytecodeFunction;

using ValuePtr = std::shared_ptr<Value>;
using NativeFunction = std::function<ValuePtr(const std::vector<ValuePtr>&)>;

enum class ValueType {
    Undefined,
    Null,
    Boolean,
    Number,
    String,
    Object,
    Array,
    Function,
    NativeFunction,
    Class
};

class Value {
public:
    ValueType type;

    // Primitive values
    bool boolValue;
    double numberValue;
    std::string stringValue;

    // Object properties
    std::map<std::string, ValuePtr> properties;

    // Array elements
    std::vector<ValuePtr> elements;

    // Function data
    std::shared_ptr<class BytecodeFunction> bytecodeFunc;
    NativeFunction nativeFunc;

    // Class data
    std::string className;
    ValuePtr superClass;      // Parent class for inheritance
    ValuePtr constructor;     // Constructor function

    // Constructors
    Value()
        : type(ValueType::Undefined)
        , boolValue(false)
        , numberValue(0)
        , bytecodeFunc(nullptr) {
    }

    static ValuePtr makeUndefined();
    static ValuePtr makeNull();
    static ValuePtr makeBoolean(bool value);
    static ValuePtr makeNumber(double value);
    static ValuePtr makeString(const std::string& value);
    static ValuePtr makeObject();
    static ValuePtr makeArray();
    static ValuePtr makeFunction(std::shared_ptr<BytecodeFunction> func);
    static ValuePtr makeNativeFunction(NativeFunction func);
    static ValuePtr makeClass(const std::string& name);

    // Type checking
    bool isUndefined() const { return type == ValueType::Undefined; }
    bool isNull() const { return type == ValueType::Null; }
    bool isBoolean() const { return type == ValueType::Boolean; }
    bool isNumber() const { return type == ValueType::Number; }
    bool isString() const { return type == ValueType::String; }
    bool isObject() const { return type == ValueType::Object; }
    bool isArray() const { return type == ValueType::Array; }
    bool isFunction() const { return type == ValueType::Function || type == ValueType::NativeFunction; }
    bool isClass() const { return type == ValueType::Class; }

    // Conversions
    bool toBoolean() const;
    double toNumber() const;
    std::string toString() const;

    // Property access
    ValuePtr getProperty(const std::string& name);
    void setProperty(const std::string& name, ValuePtr value);

    // Array access
    ValuePtr getElement(size_t index);
    void setElement(size_t index, ValuePtr value);

    // Utility
    std::string typeName() const;
};

} // namespace as3
