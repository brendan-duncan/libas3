#pragma once

#include "../runtime/value.h"
#include "chunk.h"
#include <vector>
#include <map>
#include <string>
#include <memory>

namespace as3 {

// Call frame for function calls
struct CallFrame {
    BytecodeFunction* function;
    size_t ip;                  // Instruction pointer (offset into chunk)
    size_t stackOffset;         // Where this frame's locals start on stack
    ValuePtr thisBinding;       // 'this' value for this call
    bool isConstructor;         // True if this is a constructor call
    ValuePtr constructorInstance; // Instance to return from constructor

    CallFrame(BytecodeFunction* func, size_t offset, ValuePtr thisVal = nullptr, bool isCtor = false, ValuePtr instance = nullptr)
        : function(func)
        , ip(0)
        , stackOffset(offset)
        , thisBinding(thisVal)
        , isConstructor(isCtor)
        , constructorInstance(instance) {
    }
};

// Virtual Machine for bytecode execution
class VM {
public:
    VM();
    explicit VM(VM* parent);

    // Execute a bytecode function. With a scope, the script runs on it as
    // ActionScript runs a frame script on its timeline: it is 'this', names
    // not found as locals are looked for on it before the globals, and
    // assigning a name that is not a global (or declaring one with var at
    // the top level) keeps it there.
    void execute(std::shared_ptr<BytecodeFunction> function, ValuePtr scope = nullptr);

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

private:
    VM* parent;
    std::vector<ValuePtr> stack;            // Value stack
    std::vector<CallFrame> frames;          // Call frames
    std::map<std::string, ValuePtr> globals; // Global variables (local + inherited)
    ValuePtr scope;                         // The timeline a script runs on (see execute)
    bool hadError;
    std::string errorMessage;
    int errorLine = -1;

    // Main execution loop: runs until the frame count is back down to
    // stopDepth, or to the script's end.
    static constexpr size_t kToTheEnd = static_cast<size_t>(-1);
    void run(size_t stopDepth = kToTheEnd);

    // A name not found as a local: on the scope, then the globals.
    ValuePtr lookup(const std::string& name);
    // Assigning such a name: a global already there, else the scope's.
    void assign(const std::string& name, ValuePtr value);
    // The arguments on top of the stack made the function's own count:
    // missing ones undefined, extra ones dropped.
    void fitArguments(uint8_t given, int wanted);

    // Stack operations
    void push(ValuePtr value);
    ValuePtr pop();
    ValuePtr peek(size_t distance = 0);
    void resetStack();

    // Current frame access
    CallFrame& currentFrame();
    Chunk& currentChunk();
    uint8_t readByte();
    uint16_t readU16();
    int16_t readS16();
    double readF64();
    ValuePtr readConstant();

    // Instruction execution
    void execAdd();
    void execSubtract();
    void execMultiply();
    void execDivide();
    void execModulo();
    void execNegate();
    void execNot();
    void execEqual();
    void execNotEqual();
    void execStrictEqual();
    void execStrictNotEqual();
    void execGreater();
    void execGreaterEqual();
    void execLess();
    void execLessEqual();
    void execBitwiseAnd();
    void execBitwiseOr();
    void execBitwiseXor();
    void execBitwiseNot();
    void execLeftShift();
    void execRightShift();
    void execUnsignedRightShift();

    // Error reporting
    void runtimeError(const std::string& message);

    // Setup built-ins
    void setupBuiltins();
};

} // namespace as3
