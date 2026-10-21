#pragma once

#include "../runtime/value.h"
#include "chunk.h"
#include <atomic>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <memory>
#include <optional>
#include <functional>
#include <initializer_list>
#include <unordered_map>

namespace as3 {

class VM;

// An ActionScript exception on its way to a catch: what was thrown. Raised
// with throw (VM::throwError makes one from an error class and a message);
// not a std::exception, so that host code catching those lets it pass.
struct ScriptException {
    Val value;
    // The line it was thrown on, once known (-1 before).
    int line = -1;
};

// The source of a class the VM asked the host for (see setClassResolver).
struct ClassSource {
    std::string source;
    // For messages: the file it came from.
    std::string fileName;
};

using ClassResolver = std::function<std::optional<ClassSource>(const std::string& qualifiedName)>;

// The source of a file an include names, from the file that includes it.
using IncludeResolver = std::function<std::optional<std::string>(const std::string& path, const std::string& fromFile)>;

// Defines a class the host implements, for scripts to use and extend:
//
//   vm.defineNativeClass("flash.geom.Point")
//       .constructor([](VM& vm, const Val& self, Args args) {
//           self->host = std::make_shared<PointState>(args.number(0), args.number(1));
//           return Val();
//       })
//       .property("x", getX, setX)
//       .method("toString", pointToString)
//       .define();
//
// See README.md for the whole of it.
class NativeClassBuilder {
public:
    NativeClassBuilder(VM& vm, const std::string& qualifiedName);

    // The superclass, by qualified name (defined already, or found by the
    // class resolver). Object when not given.
    NativeClassBuilder& extends(const std::string& baseName);
    NativeClassBuilder& implements(const std::string& interfaceName);
    // Runs when an instance is made, with the arguments new (or a
    // subclass's super(...)) gives: attaches the host state to self->host.
    // A class without one runs its superclass's. It may return a value to
    // make instead (rarely wanted); undefined keeps self.
    NativeClassBuilder& constructor(NativeMethod constructor);
    NativeClassBuilder& method(const std::string& name, NativeMethod method, int length = 0);
    NativeClassBuilder& getter(const std::string& name, NativeMethod getter);
    // A setter is given the value as its one argument.
    NativeClassBuilder& setter(const std::string& name, NativeMethod setter);
    NativeClassBuilder& property(const std::string& name, NativeMethod getter, NativeMethod setter);
    // A field of each instance (a variable, as a script class declares).
    NativeClassBuilder& field(const std::string& name, const Val& initial = Val(), Coerce coerce = Coerce::None);
    // Static members: Class.name.
    NativeClassBuilder& staticMethod(const std::string& name, NativeMethod method, int length = 0);
    NativeClassBuilder& staticGetter(const std::string& name, NativeMethod getter);
    NativeClassBuilder& staticSetter(const std::string& name, NativeMethod setter);
    NativeClassBuilder& constant(const std::string& name, const Val& value);
    NativeClassBuilder& staticVar(const std::string& name, const Val& value, Coerce coerce = Coerce::None);
    // A method instances find after their dynamic properties (as on a
    // prototype), so an object may have its own of that name.
    NativeClassBuilder& prototypeMethod(const std::string& name, NativeMethod method, int length = 0);
    // What calling the class as a function does (Class(x)); a cast when
    // not given.
    NativeClassBuilder& callHandler(NativeMethod handler);
    // Instances may be given properties the class does not declare.
    NativeClassBuilder& dynamic(bool isDynamic = true);
    NativeClassBuilder& final(bool isFinal = true);
    NativeClassBuilder& interface(bool isInterface = true);

    // Defines it: its qualified name and (unless taken) its short one find
    // it. Returns the class.
    Val define();

    ClassInfo& info() { return *_info; }

private:
    VM& _vm;
    std::shared_ptr<ClassInfo> _info;
    std::string _baseName;
    bool _baseGiven = false;
    std::vector<std::string> _interfaces;
    std::vector<std::pair<std::string, Trait>> _traits;
    std::vector<std::pair<std::string, Trait>> _statics;
    std::vector<Val> _staticValues;
    std::vector<std::pair<std::string, Val>> _fields;
};

// Call frame for function calls
struct CallFrame {
    BytecodeFunction* code = nullptr;
    // The function running; held when nothing else is sure to hold it (a
    // method in a class, the callee's place on the stack, are).
    Value* closure = nullptr;
    ValueRef closureHold;
    const uint8_t* ip = nullptr;
    const uint8_t* codeStart = nullptr;
    // The first local (parameters first), and where the result goes.
    Val* base = nullptr;
    Val* result = nullptr;
    Val thisVal;
    // The instance names find (for a method, 'this' itself), and the
    // timeline scope.
    Val scopeThis;
    bool methodThis = false;
    Val scope;
    const Val& scopeSelf() const { return methodThis ? thisVal : scopeThis; }
    ClassInfo* owner = nullptr;
    std::vector<std::shared_ptr<Cell>> cells;
    uint32_t argc = 0;
    // What returning gives: the value returned, or (a constructor, a
    // setter) what was saved.
    uint8_t mode = 0;
    Val saved;
};

// The call frames: made once and used again, so that a call does not
// make and free one.
class FrameStack {
public:
    explicit FrameStack(size_t capacity) { _frames.reserve(capacity); }
    size_t size() const { return _count; }
    bool empty() const { return _count == 0; }
    CallFrame& back() { return _frames[_count - 1]; }
    const CallFrame& back() const { return _frames[_count - 1]; }
    const CallFrame& operator[](size_t i) const { return _frames[i]; }
    // A frame with every field to be set by the caller.
    CallFrame& push() {
        if (_count == _frames.size()) {
            _frames.emplace_back();
        }
        return _frames[_count++];
    }
    void pop_back() {
        CallFrame& frame = _frames[--_count];
        frame.closureHold.reset();
        frame.thisVal = Val();
        if (!frame.scopeThis.isUndefined()) frame.scopeThis = Val();
        if (!frame.scope.isUndefined()) frame.scope = Val();
        if (!frame.saved.isUndefined()) frame.saved = Val();
        frame.cells.clear();
    }

private:
    std::vector<CallFrame> _frames;
    size_t _count = 0;
};

// Virtual Machine for bytecode execution
class VM {
public:
    VM();
    explicit VM(VM* parent);
    ~VM();

    VM(const VM&) = delete;
    VM& operator=(const VM&) = delete;

    // Execute a bytecode function. With a scope, the script runs on it as
    // ActionScript runs a frame script on its timeline: it is 'this', names
    // not found as locals are looked for on it before the globals, and
    // assigning a name that is not a global (or declaring one with var at
    // the top level) keeps it there. Errors are kept (hasError), not
    // thrown. It may be run while another script runs (from a native).
    void execute(std::shared_ptr<BytecodeFunction> function, ValuePtr scope = nullptr);

    // Parses, compiles and runs source, as execute: false (with the error
    // kept) when it could not be parsed or compiled, or failed.
    bool load(const std::string& source, const std::string& fileName = std::string(), ValuePtr scope = nullptr);

    // Calls a function (compiled or native) from the host, with these
    // arguments, 'this' and scope (see execute), as when an event it
    // handles comes. Returns what it returns; undefined, with hasError(),
    // when it failed. Can be called while a script runs, from a native
    // function, as well as between scripts.
    ValuePtr call(ValuePtr function, const std::vector<ValuePtr>& args = {}, ValuePtr thisValue = nullptr,
                  ValuePtr scope = nullptr);

    // Whether two values are equal as ActionScript's == has it: numbers,
    // strings and booleans compared as numbers when their types differ,
    // null equal to undefined.
    static bool looselyEqual(const ValuePtr& a, const ValuePtr& b);
    static bool looselyEqual(const Val& a, const Val& b);

    // Get/set global variables (checks parent if not found locally)
    ValuePtr getGlobal(const std::string& name);
    void setGlobal(const std::string& name, ValuePtr value);

    // Register a custom built-in function (callable from AS3 code)
    void registerBuiltin(const std::string& name, NativeFunction func);

    // Define a class with extern (native) methods
    // Methods are called via callbacks when invoked from AS3 code
    void defineExternClass(const std::string& className,
                           const std::map<std::string, NativeFunction>& methods);

    // Register an object instance with a class type
    // The class should be defined via defineExternClass or exist in globals
    void registerObject(const std::string& name, const std::string& classType);

    // Get parent VM (may be nullptr)
    VM* getParent() const { return parent; }

    // Error handling: the last run's error, with the line it was on (-1
    // when not known). Runtime errors stop the script and are kept here;
    // they are not thrown out of the VM or printed.
    bool hasError() const { return hadError; }
    const std::string& getError() const { return errorMessage; }
    int getErrorLine() const { return errorLine; }
    // The value thrown, and where (each function it was in, innermost
    // first: "\tat Class/method()[file:line]").
    const Val& getErrorValue() const { return errorValue; }
    const std::string& getErrorStack() const { return errorStack; }
    void clearError();
    // What compiling (the resolver's classes, load) warned of.
    const std::vector<std::string>& getWarnings() const { return warnings; }

    // --- Native classes and functions ---

    // A class the host implements (see NativeClassBuilder).
    NativeClassBuilder defineNativeClass(const std::string& qualifiedName);
    // A function: flash.utils.getTimer, or a top-level one without a
    // package.
    void defineFunction(const std::string& qualifiedName, NativeMethod function, int length = 0);
    // A constant or variable by qualified name.
    void defineValue(const std::string& qualifiedName, const Val& value);
    // A native function as a value.
    Val makeFunction(const std::string& name, NativeMethod function, int length = 0);

    // When a name is not defined (a superclass, a class a script uses),
    // the VM asks the host for its source by qualified name ("a.b.C", or
    // "C" for one without a package), compiles and runs it, and goes on.
    // The host answers nothing for names it does not have.
    void setClassResolver(ClassResolver resolver) { classResolver = std::move(resolver); }
    // Definitions the host has as values rather than source (the classes
    // of ABC loaded with abc::Domain): asked for a qualified name before
    // the class resolver, true with the value when it has it.
    using DefinitionProvider = std::function<bool(const std::string& qualifiedName, Val& value)>;
    void addDefinitionProvider(DefinitionProvider provider) { definitionProviders.push_back(std::move(provider)); }
    // What include "file.as" reads, in what load and the class resolver
    // compile.
    void setIncludeResolver(IncludeResolver resolver) { includeResolver = std::move(resolver); }
    // ActionScript 1 and 2 read a variable that was never set as
    // undefined; ActionScript 3 throws a ReferenceError. Off by default.
    void setLooseNames(bool loose) { looseNames = loose; }

    // A definition (class, function or value) by qualified name ("a.b.C"
    // or "a.b::C"), asking the class resolver if needed: undefined when
    // there is none.
    Val findDefinition(const std::string& qualifiedName);
    // A class by qualified name, nullptr when there is none.
    std::shared_ptr<ClassInfo> findClass(const std::string& qualifiedName);

    // --- Calling scripts from native code ---
    // These throw ScriptException for an ActionScript error, which goes on
    // to the script that called the native function.

    Val callFunction(const Val& function, const Val& thisValue, Args args);
    Val callFunction(const Val& function, const Val& thisValue, const std::vector<Val>& args) {
        return callFunction(function, thisValue, Args(args.data(), args.size()));
    }
    // callFunction(f, self, {a, b}): the arguments on the C++ stack, not
    // in a vector made for each call (a sort's comparison).
    Val callFunction(const Val& function, const Val& thisValue, std::initializer_list<Val> args) {
        return callFunction(function, thisValue, Args(args.begin(), args.size()));
    }
    // obj.name(args), as a script calls it: the overriding method of a
    // subclass when there is one.
    Val callMethod(const Val& object, const std::string& name, Args args);
    Val callMethod(const Val& object, const std::string& name, const std::vector<Val>& args = {}) {
        return callMethod(object, name, Args(args.data(), args.size()));
    }
    Val callMethod(const Val& object, const std::string& name, std::initializer_list<Val> args) {
        return callMethod(object, name, Args(args.begin(), args.size()));
    }
    // new cls(args).
    Val construct(const Val& cls, Args args);
    Val construct(const Val& cls, const std::vector<Val>& args = {}) {
        return construct(cls, Args(args.data(), args.size()));
    }
    Val construct(const Val& cls, std::initializer_list<Val> args) {
        return construct(cls, Args(args.begin(), args.size()));
    }
    // An instance with its fields at their defaults, its constructor not
    // run: for the host to attach its state to, then construct.
    Val newInstance(const std::shared_ptr<ClassInfo>& cls);
    // Runs a class's constructor on an instance made by newInstance.
    void runConstructor(const Val& instance, Args args);
    Val getProperty(const Val& object, const std::string& name);
    void setProperty(const Val& object, const std::string& name, const Val& value);
    bool hasProperty(const Val& object, const std::string& name);
    Val getIndex(const Val& object, const Val& key);
    void setIndex(const Val& object, const Val& key, const Val& value);

    // As String(value): running an object's toString.
    std::string toString(const Val& value);
    // An array or object.
    Val newArray(std::vector<Val> elements = {});
    Val newObject();
    // An error of an error class ("TypeError", "flash.errors.IOError"),
    // as the VM makes for its own: "Error #id: message" when id is given.
    Val makeError(const std::string& className, const std::string& message, int id = 0);
    [[noreturn]] void throwError(const std::string& className, const std::string& message, int id = 0);
    // The class of a value (its own, or Number, String... for primitives).
    ClassInfo* classOf(const Val& value);
    // x is T, as the operator has it.
    bool isType(const Val& value, const Val& type);
    // The functions running, innermost first, as getStackTrace gives them.
    std::string stackTrace();
    // Code another engine runs (a movie's ABC) tells of each function it
    // enters and leaves, by its name as a stack trace gives it, so traces
    // have its frames among the VM's own.
    void enterOuterFrame(const std::string* name) { outerFrames.emplace_back(frames.size(), name); }
    void leaveOuterFrame() { if (!outerFrames.empty()) outerFrames.pop_back(); }

    // The built-in classes.
    struct Builtins {
        std::shared_ptr<ClassInfo> object, klass, function, array, string, number, intClass, uintClass, boolean,
            error, vector, regexp, date, dictionary, math;
    };
    const Builtins& builtins() const { return core; }
    Builtins& builtins() { return core; }

    // Timers (setTimeout, setInterval): run what is due when the host says
    // how long it has been since the VM was made, in milliseconds. getTimer
    // gives that time; with a clock set, the clock's.
    void runTimers(double nowMs);
    void setClock(std::function<double()> clock) { this->clock = std::move(clock); }
    double now();

    // How much of the C++ stack scripts calling natives calling scripts
    // may use (from where the host called in) before that is a stack
    // overflow error: 512 KB unless set.
    void setNativeStackLimit(size_t bytes) { nativeStackLimit = bytes; }

    // Every object the VM made, emptied: breaks the cycles reference
    // counting cannot. The VM's destructor does it.
    void releaseObjects();

    // The VM's own state for the built-in library.
    struct TimerEntry {
        int id;
        double due;
        double interval;
        Val function;
        std::vector<Val> args;
    };

private:
    friend class NativeClassBuilder;
    friend struct VMAccess;

    VM* parent;
    // The value stack: its memory taken at once, its values made as it
    // is first used (up to stackBuilt).
    Val* stackBase = nullptr;
    Val* stackEnd = nullptr;
    Val* stackBuilt = nullptr;
    Val* sp = nullptr;
    FrameStack frames;
    // Each outer frame: how many of the VM's frames were under it, and its name.
    std::vector<std::pair<size_t, const std::string*>> outerFrames;
    void growStack(Val* needed);
    std::unordered_map<std::string, Val> globals;
    std::unordered_map<std::string, Val> definitions;
    ClassResolver classResolver;
    std::vector<DefinitionProvider> definitionProviders;
    IncludeResolver includeResolver;
    std::set<std::string> missing;
    std::set<std::string> loading;
    Builtins core;
    // The Math class's value: CALL_MATH does Math's work itself when the
    // object it has is this one.
    Value* mathClassValue = nullptr;
    bool hadError = false;
    bool looseNames = false;
    std::string errorMessage;
    int errorLine = -1;
    Val errorValue;
    std::string errorStack;
    std::vector<std::string> warnings;
    // A native's nested run: how deep, and how much of the C++ stack the
    // runs use, to stop runaway ones.
    int nativeDepth = 0;
    const char* stackEntry = nullptr;
    size_t nativeStackLimit = 512 * 1024;
    bool overflowing = false;
    Value::TrackList made;
    std::vector<TimerEntry> timers;
    int nextTimer = 1;
    double started = 0;
    std::function<double()> clock;
    std::map<const ClassInfo*, std::shared_ptr<ClassInfo>> vectorClasses;
    // Bumped when a global or definition is added, in any VM (the caches
    // are in the code, which more than one VM may run, and a VM finds its
    // parent's globals): cached name lookups made before are looked up
    // again. A global given a new value is read through its slot.
    static std::atomic<uint64_t> namesVersion;

public:
    // Internal: used by the built-in library.
    std::vector<TimerEntry>& timerList() { return timers; }
    int newTimerId() { return nextTimer++; }
    void track(const ValuePtr& value);
    Val allocate(const std::shared_ptr<ClassInfo>& cls);
    Val makeClosure(const std::shared_ptr<BytecodeFunction>& code, const std::shared_ptr<ClassInfo>& owner);
    Val toPrimitive(const Val& value, bool preferNumber);
    double toNumber(const Val& value);
    bool lessThan(const Val& a, const Val& b, bool& undefinedResult);
    // A comparison's result (a fused one's: LESS_JUMP...) on values that
    // are not both numbers.
    bool compareForJump(OpCode op, const Val& a, const Val& b);
    Val add(const Val& a, const Val& b);
    // == as scripts have it: looselyEqual, with an object's primitive value
    // found by running its valueOf or toString.
    bool equalsLoosely(const Val& a, const Val& b);
    std::string typeOf(const Val& value);
    Val defineClassFromTemplate(const std::shared_ptr<ClassTemplate>& klass);
    Val resolveTypeName(const std::string& name, const CompileUnit* unit);
    Val lookupQualified(const std::string& qualifiedName, bool askResolver);
    void registerDefinition(const std::string& qualifiedName, const Val& value);
    Val readTrait(const Val& object, const Trait& trait, bool isStatic);
    void writeTrait(const Val& object, const Trait& trait, const Val& value, const std::string& name, bool isStatic);
    Val boundMethod(const Val& object, const Trait& trait);
    Val applyType(const Val& base, const std::vector<Val>& types);
    std::shared_ptr<ClassInfo> vectorClass(const Val& elementType);
    Val makeIterator(const Val& object, bool each);
    // A name on an object: its class's members, then (unless only those)
    // its host's and dynamic properties.
    bool findOnObject(const Val& object, const std::string& name, Val& value, bool traitsOnly = false);
    bool setOnObject(const Val& object, const std::string& name, const Val& value, bool traitsOnly = false);
    Val classValue(const ClassInfo* cls);
    // A package by name (a, a.b) when definitions are under it, else
    // undefined.
    Val packageObject(const std::string& name);
    void loadSource(const ClassSource& source);
    void runNativeConstructor(ClassInfo* cls, const Val& instance, Args args, Val& replaced);
    void setupBuiltins();

private:
    // Main execution loop: runs until the frame count is back down to
    // stopDepth.
    void run(size_t stopDepth);
    bool handleException(ScriptException& exception, size_t stopDepth);
    // Calls function: a frame for a script function (run goes on in it),
    // or a native one now. Its arguments are at args, and the result goes
    // to result.
    bool invoke(const Val& function, const Val& thisValue, Val* args, uint32_t argc, Val* result,
                uint8_t mode = 0, const Val& saved = Val(), bool borrowed = false);
    void pushFrame(const Val& function, FunctionInfo& info, const Val& thisValue, Val* args, uint32_t argc,
                   Val* result, uint8_t mode, const Val& saved, bool borrowed = false);
    Val callNative(const Val& function, const Val& thisValue, Val* args, uint32_t argc);
    Val callAsFunction(ClassInfo* cls, const Val& classValue, Args args);
    // The function at the top of the stack run to its end: for the host.
    Val runCall(const Val& function, const Val& thisValue, Args args, const Val& scope);
    void constructInto(const Val& cls, Val* args, uint32_t argc, Val* result);
    void superConstruct(CallFrame& frame, Val* args, uint32_t argc, Val* result);

    Val getProp(const Val& object, const std::string& name, PropertyCache* cache);
    // What an inline cache knows, done at once: 0 when it does not know
    // the object's class, 1 when done, 2 when a getter's, setter's or
    // method's frame was pushed (run goes on in it).
    int cachedGet(const Val& object, const PropertyCache& cache, Val* out);
    int cachedSet(const Val& object, const PropertyCache& cache, Val* value);
    int cachedCall(const Val& object, const PropertyCache& cache, Val* args, uint32_t argc, Val* result);
    void setProp(const Val& object, const std::string& name, const Val& value, PropertyCache* cache);
    bool callProp(const Val& object, const std::string& name, Val* args, uint32_t argc, Val* result,
                  PropertyCache* cache);
    bool deleteProp(const Val& object, const std::string& name);
    Val getIndexOf(const Val& object, const Val& key);
    void setIndexOf(const Val& object, const Val& key, const Val& value);
    bool deleteIndex(const Val& object, const Val& key);
    bool hasKey(const Val& object, const Val& key);

    // Names a script uses: looked up on the instance, the class's
    // statics, the timeline, then the imports and the globals.
    bool lookupName(CallFrame& frame, const std::string& name, Val& value, Val* receiver,
                    PropertyCache* cache = nullptr);
    // Assigns a name; the global's slot when that is what it was.
    Val* assignName(CallFrame& frame, const std::string& name, const Val& value);
    bool findGlobal(const std::string& name, Val& value);
    Val* globalSlot(const std::string& name);
    void defineVar(CallFrame& frame, const std::string& name, const Val& value, bool onlyIfMissing);

    [[noreturn]] void nullError(const Val& object, const std::string& name);
    [[noreturn]] void undefinedNameError(const std::string& name);
    [[noreturn]] void notAFunctionError(const std::string& name);
    Val makeRegExpValue(const Val& pattern, const Val& flags);
    void enterNative(const char* marker);
    // Frames and stack values above a call taken off.
    void unwindTo(size_t depth, Val* result);
    void reportError(const ScriptException& exception);
    int currentLine();
};

} // namespace as3
