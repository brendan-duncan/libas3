#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <memory>
#include <map>
#include <unordered_map>
#include <vector>
#include <functional>
#include <typeinfo>

namespace as3 {

class Value;
class Val;
class VM;
class BytecodeFunction;
class HostObject;
struct ClassInfo;
struct FunctionInfo;
struct Metadata;

using ValuePtr = std::shared_ptr<Value>;

// A host function the simple way: the arguments boxed, no 'this'. See
// NativeMethod for the fast way, with the VM and 'this'.
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

// What a typed variable, field, parameter or return value makes of what is
// stored in it: int and uint wrap to 32 bits (ToInt32, ToUint32), Number
// converts, String converts (null and undefined give null), Boolean
// converts, and a class type or Object turns undefined into null.
enum class Coerce : uint8_t { None, Int, Uint, Number, String, Boolean, Nullable };

// The coercion for a type as written ("int", "uint", "Number", "*"...).
Coerce coerceForType(const std::string& typeName);

// A string's or an object's Value as a Val holds it. Vals count their
// holds on the Value itself, without atomics; while any Val holds it, the
// Value keeps a ValuePtr to itself, so that ValuePtr and weak_ptr see it
// held as before and it lives as long as either holds it. Reads as a
// ValuePtr (get, ->, use_count, and converting to one).
class ValueRef {
public:
    ValueRef() noexcept = default;
    ValueRef(std::nullptr_t) noexcept {}
    ValueRef(const ValueRef& other) noexcept;
    ValueRef(ValueRef&& other) noexcept : _value(other._value) { other._value = nullptr; }
    ValueRef(const ValuePtr& value) noexcept;
    ValueRef(ValuePtr&& value) noexcept;
    ~ValueRef();

    ValueRef& operator=(const ValueRef& other) noexcept;
    ValueRef& operator=(ValueRef&& other) noexcept;
    ValueRef& operator=(const ValuePtr& value) noexcept { return *this = ValueRef(value); }
    ValueRef& operator=(ValuePtr&& value) noexcept { return *this = ValueRef(std::move(value)); }
    ValueRef& operator=(std::nullptr_t) noexcept {
        reset();
        return *this;
    }

    Value* get() const noexcept { return _value; }
    Value* operator->() const noexcept { return _value; }
    Value& operator*() const noexcept { return *_value; }
    explicit operator bool() const noexcept { return _value != nullptr; }
    void reset() noexcept;
    // Every hold on it: Vals' and ValuePtrs'.
    long use_count() const noexcept;
    // The Value as a ValuePtr (an empty one when there is none).
    operator const ValuePtr&() const noexcept;
    operator std::weak_ptr<Value>() const noexcept { return static_cast<const ValuePtr&>(*this); }

    friend bool operator==(const ValueRef& a, const ValueRef& b) noexcept { return a._value == b._value; }
    friend bool operator!=(const ValueRef& a, const ValueRef& b) noexcept { return a._value != b._value; }
    friend bool operator==(const ValueRef& a, std::nullptr_t) noexcept { return !a._value; }
    friend bool operator!=(const ValueRef& a, std::nullptr_t) noexcept { return a._value != nullptr; }
    friend bool operator==(const ValueRef& a, const ValuePtr& b) noexcept { return a._value == b.get(); }
    friend bool operator!=(const ValueRef& a, const ValuePtr& b) noexcept { return a._value != b.get(); }
    friend bool operator==(const ValuePtr& a, const ValueRef& b) noexcept { return a.get() == b._value; }
    friend bool operator!=(const ValuePtr& a, const ValueRef& b) noexcept { return a.get() != b._value; }

private:
    Value* _value = nullptr;
};

// A value as the VM keeps it: a number or boolean in place, a string or an
// object by reference. Cheap to copy; the host converts to and from
// ValuePtr implicitly.
class Val {
public:
    enum class Tag : uint8_t { Undefined, Null, Boolean, Number, String, Object };

    Tag tag;
    union {
        double num;
        bool flag;
    };
    // A string's or an object's Value.
    ValueRef ref;

    Val() noexcept : tag(Tag::Undefined), num(0) {}
    Val(const Val& other) = default;
    Val(Val&& other) noexcept = default;
    Val& operator=(const Val& other) = default;
    Val& operator=(Val&& other) noexcept = default;

    // From a boxed value: numbers, booleans, strings, null and undefined
    // become themselves; an empty pointer is undefined.
    Val(const ValuePtr& value);
    // Boxed again for the host. Numbers and booleans are new each time;
    // strings and objects are the same Value.
    operator ValuePtr() const;

    static Val undefined() { return Val(); }
    static Val null() {
        Val v;
        v.tag = Tag::Null;
        return v;
    }
    static Val boolean(bool value) {
        Val v;
        v.tag = Tag::Boolean;
        v.flag = value;
        return v;
    }
    static Val number(double value) {
        Val v;
        v.tag = Tag::Number;
        v.num = value;
        return v;
    }
    static Val string(const std::string& value);
    static Val string(std::string&& value);
    // An object (or function, array, class); an empty pointer is null.
    static Val object(ValuePtr value);

    bool isUndefined() const { return tag == Tag::Undefined; }
    bool isNull() const { return tag == Tag::Null; }
    bool isNullish() const { return tag <= Tag::Null; }
    bool isBoolean() const { return tag == Tag::Boolean; }
    bool isNumber() const { return tag == Tag::Number; }
    bool isString() const { return tag == Tag::String; }
    bool isObject() const { return tag == Tag::Object; }
    bool isFunction() const;
    bool isArray() const;
    bool isClass() const;

    // The Value of a string or object, nullptr otherwise.
    Value* get() const { return ref.get(); }
    Value* operator->() const { return ref.get(); }
    // A string's text (only for a string).
    const std::string& str() const;

    // ECMAScript conversions. toString gives an object's default text,
    // without running its toString method; VM::toString runs it.
    bool toBoolean() const;
    double toNumber() const;
    std::string toString() const;
    int32_t toInt32() const;
    uint32_t toUint32() const;

    // The host object attached to an object, as a type; nullptr when there
    // is none or it is another type.
    template <class T>
    T* host() const;

    // Whether two values are the same value, as === has it.
    bool strictEquals(const Val& other) const;
};

// The arguments a native function is given: missing ones read as
// undefined.
class Args {
public:
    Args() = default;
    Args(const Val* data, size_t count) : _data(data), _count(count) {}

    size_t size() const { return _count; }
    bool empty() const { return _count == 0; }
    const Val& operator[](size_t i) const;
    const Val* begin() const { return _data; }
    const Val* end() const { return _data + _count; }

    // Shorthands: the argument converted, or the default when missing.
    double number(size_t i, double otherwise = 0) const;
    int32_t integer(size_t i, int32_t otherwise = 0) const;
    std::string string(size_t i, const std::string& otherwise = std::string()) const;
    bool boolean(size_t i, bool otherwise = false) const;

private:
    const Val* _data = nullptr;
    size_t _count = 0;
};

// A host function the fast way: the VM, 'this' and the arguments. Throws
// (VM::throwError) to raise an ActionScript error.
using NativeMethod = std::function<Val(VM& vm, const Val& self, Args args)>;

// Dynamic properties by name, made on first use.
class PropertyMap {
public:
    using Map = std::map<std::string, Val>;
    using iterator = Map::iterator;
    using const_iterator = Map::const_iterator;

    PropertyMap() = default;
    PropertyMap(const PropertyMap& other) : _map(other._map ? std::make_unique<Map>(*other._map) : nullptr) {}
    PropertyMap& operator=(const PropertyMap& other) {
        _map = other._map ? std::make_unique<Map>(*other._map) : nullptr;
        return *this;
    }

    Val& operator[](const std::string& key) { return map()[key]; }
    size_t count(const std::string& key) const { return _map ? _map->count(key) : 0; }
    iterator find(const std::string& key) { return _map ? _map->find(key) : none().end(); }
    const_iterator find(const std::string& key) const { return _map ? _map->find(key) : none().end(); }
    iterator begin() { return _map ? _map->begin() : none().begin(); }
    iterator end() { return _map ? _map->end() : none().end(); }
    const_iterator begin() const { return _map ? _map->begin() : none().begin(); }
    const_iterator end() const { return _map ? _map->end() : none().end(); }
    size_t erase(const std::string& key) { return _map ? _map->erase(key) : 0; }
    size_t size() const { return _map ? _map->size() : 0; }
    bool empty() const { return !_map || _map->empty(); }
    void clear() { _map.reset(); }
    // The value by that name, or nullptr.
    const Val* lookup(const std::string& key) const {
        if (!_map) return nullptr;
        auto it = _map->find(key);
        return it == _map->end() ? nullptr : &it->second;
    }

private:
    std::unique_ptr<Map> _map;
    Map& map() {
        if (!_map) _map = std::make_unique<Map>();
        return *_map;
    }
    static Map& none() {
        static Map none;
        return none;
    }
};

// A captured variable, shared by the function that declares it and the
// closures that use it.
struct Cell {
    Val value;
};

class Value {
public:
    ValueType type;

    // Primitive values
    bool boolValue;
    double numberValue;
    std::string stringValue;

    // Dynamic properties
    PropertyMap properties;

    // Array (and Vector) elements
    std::vector<Val> elements;

    // A host function the simple way (see NativeFunction).
    NativeFunction nativeFunc;

    // An object the host program answers for (see HostObject); for an
    // instance of a native class, its host state.
    std::shared_ptr<HostObject> host;

    // The class of an instance; for a Class value, the class it is.
    std::shared_ptr<ClassInfo> cls;
    // An instance's fields, by the slots its class gives them.
    std::vector<Val> slots;
    // A function's code, captures and binding.
    std::shared_ptr<FunctionInfo> function;
    // Methods read off an instance, bound to it, kept so that reading one
    // twice gives the same function (as removeEventListener needs).
    std::unique_ptr<std::unordered_map<const void*, ValuePtr>> boundMethods;

    // The Vals holding it, and while there are any, itself (see ValueRef).
    uint32_t valCount = 0;
    ValuePtr heldByVals;

    Value()
        : type(ValueType::Undefined)
        , boolValue(false)
        , numberValue(0) {
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
    static ValuePtr makeHostObject(std::shared_ptr<HostObject> host);

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

    // Property access without the VM: fields, the host's properties and
    // dynamic ones (getters are not run; VM::getProperty runs them).
    ValuePtr getProperty(const std::string& name);
    void setProperty(const std::string& name, ValuePtr value);

    // Array access
    ValuePtr getElement(size_t index);
    void setElement(size_t index, ValuePtr value);

    // Utility
    std::string typeName() const;
};

// The last Val let go of the Value: its own hold goes (and with it the
// Value, when nothing else holds it).
void releaseValue(Value* value) noexcept;

inline ValueRef::ValueRef(const ValueRef& other) noexcept : _value(other._value) {
    if (_value) ++_value->valCount;
}

inline ValueRef::ValueRef(const ValuePtr& value) noexcept : _value(value.get()) {
    if (_value && _value->valCount++ == 0) _value->heldByVals = value;
}

inline ValueRef::ValueRef(ValuePtr&& value) noexcept : _value(value.get()) {
    if (_value && _value->valCount++ == 0) _value->heldByVals = std::move(value);
}

inline ValueRef::~ValueRef() {
    if (_value && --_value->valCount == 0) releaseValue(_value);
}

inline ValueRef& ValueRef::operator=(const ValueRef& other) noexcept {
    // The new hold first: other may be held only by what this lets go.
    if (other._value) ++other._value->valCount;
    Value* old = _value;
    _value = other._value;
    if (old && --old->valCount == 0) releaseValue(old);
    return *this;
}

inline ValueRef& ValueRef::operator=(ValueRef&& other) noexcept {
    if (this != &other) {
        Value* old = _value;
        _value = other._value;
        other._value = nullptr;
        if (old && --old->valCount == 0) releaseValue(old);
    }
    return *this;
}

inline void ValueRef::reset() noexcept {
    if (_value) {
        Value* old = _value;
        _value = nullptr;
        if (--old->valCount == 0) releaseValue(old);
    }
}

inline long ValueRef::use_count() const noexcept {
    return _value ? _value->heldByVals.use_count() - 1 + static_cast<long>(_value->valCount) : 0;
}

inline ValueRef::operator const ValuePtr&() const noexcept {
    static const ValuePtr none;
    return _value ? _value->heldByVals : none;
}

// An object whose properties the host program works out itself, such as a
// movie clip's _x or its gotoAndStop method. What the host does not answer
// for is kept as an ordinary property of the object.
//
// It is also where an instance of a native class keeps its host state: the
// class's constructor attaches one (Val::host<T>() finds it again).
class HostObject {
public:
    virtual ~HostObject() = default;

    // The property's value, or nullptr when the host has none by that name.
    virtual ValuePtr get(const std::string& name) {
        (void)name;
        return nullptr;
    }

    // True when the host took the value; false keeps it as an ordinary
    // property.
    virtual bool set(const std::string& name, ValuePtr value) {
        (void)name;
        (void)value;
        return false;
    }

    // obj[key], for keys that are not names (a Dictionary's object keys, a
    // ByteArray's bytes): true when the host answered.
    virtual bool getIndex(const Val& key, Val& out) {
        (void)key;
        (void)out;
        return false;
    }
    virtual bool setIndex(const Val& key, const Val& value) {
        (void)key;
        (void)value;
        return false;
    }
    virtual bool deleteIndex(const Val& key) {
        (void)key;
        return false;
    }
    virtual bool hasIndex(const Val& key) {
        (void)key;
        return false;
    }

    // The keys for-in visits (before the object's dynamic properties):
    // true when the host gave them.
    virtual bool enumerate(std::vector<Val>& keys) {
        (void)keys;
        return false;
    }
};

template <class T>
T* Val::host() const {
    if (tag != Tag::Object || !ref) {
        return nullptr;
    }
    HostObject* object = ref->host.get();
    if (!object) {
        return nullptr;
    }
    // The exact type is the usual case, and quicker than a dynamic_cast.
    if (typeid(*object) == typeid(T)) {
        return static_cast<T*>(object);
    }
    return dynamic_cast<T*>(object);
}

// What a class member is.
enum class TraitKind : uint8_t { Slot, Method, Accessor };

struct Trait {
    TraitKind kind = TraitKind::Slot;
    bool isConst = false;
    bool isFinal = false;
    // Declared public (describeType and JSON list only those).
    bool isPublic = true;
    Coerce coerce = Coerce::None;
    uint32_t slot = 0;
    // A method's function (unbound), or an accessor's getter and setter.
    Val method;
    Val getter;
    Val setter;
    ClassInfo* declaredBy = nullptr;
    std::string typeName;
    // Its metadata tags ([Embed(...)], [Bindable]...), if it has any.
    std::shared_ptr<std::vector<Metadata>> metadata;
};

// A class: its name, its superclass and interfaces, and its members, those
// of instances (inherited ones included) and its own static ones.
struct ClassInfo {
    ClassInfo();
    // Unique for the life of the program: what inline caches know a class
    // by (an address may be used again).
    uint64_t id;

    // Qualified ("com.example.Shape", "flash.display.Sprite", "Array"),
    // and its parts.
    std::string name;
    std::string shortName;
    std::string packageName;

    std::shared_ptr<ClassInfo> base;
    std::vector<std::shared_ptr<ClassInfo>> interfaces;
    bool isInterface = false;
    bool isDynamic = false;
    bool isFinal = false;
    bool isNative = false;
    // Its host answers for names before its members do (XML: x.name is
    // the children called name; a method is found when called).
    bool hostFirst = false;
    // What new makes: an Object, or for Array, Vector and their subclasses
    // an Array.
    ValueType instanceType = ValueType::Object;

    std::unordered_map<std::string, Trait> traits;
    std::vector<Val> slotDefaults;
    std::unordered_map<std::string, Trait> statics;
    std::vector<Val> staticSlots;
    // Methods instances find after their dynamic properties, as on a
    // prototype: Object's toString, which an object literal may replace.
    std::unordered_map<std::string, Val> prototype;

    // The script constructor (field initializers, then the body), or the
    // native one.
    Val constructor;
    NativeMethod nativeConstructor;
    // What calling the class as a function does: int(x), String(x),
    // Array(...). Without it, a cast.
    NativeMethod callHandler;

    // For Vector.<T>: the element type's coercion and class.
    bool isVector = false;
    Coerce elementCoerce = Coerce::None;
    std::shared_ptr<ClassInfo> elementClass;
    std::string elementTypeName;

    std::weak_ptr<Value> classObject;
    // The class's metadata tags ([Event(...)], [SWF(...)]), if any.
    std::shared_ptr<std::vector<Metadata>> metadata;

    // Whether this class is that one, extends it or implements it.
    bool isSubclassOf(const ClassInfo* other) const;
    // An instance trait or a static one, nullptr when there is none.
    const Trait* findTrait(const std::string& member) const {
        auto it = traits.find(member);
        return it == traits.end() ? nullptr : &it->second;
    }
    const Trait* findStatic(const std::string& member) const {
        auto it = statics.find(member);
        return it == statics.end() ? nullptr : &it->second;
    }
};

// A function value's code (script or native), its captured variables, and
// what it is bound to.
struct FunctionInfo {
    std::shared_ptr<BytecodeFunction> code;
    std::vector<std::shared_ptr<Cell>> captured;
    NativeMethod native;
    std::string name;
    int length = 0;
    // A method read off an object (or bound by the host) runs with this
    // 'this' however it is called.
    bool bound = false;
    Val boundThis;
    // For a function made in a method: the instance whose members its
    // names find, and the timeline scope (frame scripts) it was made in.
    Val scopeThis;
    Val scope;
    // The class a method belongs to: its statics and super.
    std::shared_ptr<ClassInfo> owner;
    // A method (its 'this' is the instance its names find).
    bool isMethod = false;
};

// ECMAScript number formatting and parsing.
std::string numberToString(double value);
// The text as a number, as Number("...") reads it: NaN when it is not one.
double stringToNumber(const std::string& text);
int32_t toInt32(double value);
uint32_t toUint32(double value);
// Whether a number is already what the int (or uint) coercion makes of
// it, and can be kept as it is: -0 is not (it becomes 0).
inline bool isInt32(double value) {
    return value >= -2147483648.0 && value <= 2147483647.0 &&
           value == static_cast<double>(static_cast<int32_t>(value)) && (value != 0 || !std::signbit(value));
}
inline bool isUint32(double value) {
    return value >= 0 && value <= 4294967295.0 && value == static_cast<double>(static_cast<uint32_t>(value)) &&
           (value != 0 || !std::signbit(value));
}
// The coercion applied to a value.
Val coerceValue(const Val& value, Coerce coerce);
// The value a variable of a type starts with: 0 for int and uint, NaN for
// Number, false for Boolean, null for other types, undefined for * and
// untyped.
Val defaultForCoerce(Coerce coerce);

} // namespace as3
