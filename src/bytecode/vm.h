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

    // Execute a bytecode function
    void execute(std::shared_ptr<BytecodeFunction> function);

    // Get/set global variables
    ValuePtr getGlobal(const std::string& name);
    void setGlobal(const std::string& name, ValuePtr value);

    // Register a custom built-in function (callable from AS3 code)
    void registerBuiltin(const std::string& name, NativeFunction func);

    // Define a class with extern (native) methods
    // Methods are called via callbacks when invoked from AS3 code
    void defineExternClass(const std::string& className,
                           const std::map<std::string, NativeFunction>& methods);

    // Error handling
    bool hasError() const { return hadError; }
    const std::string& getError() const { return errorMessage; }

private:
    std::vector<ValuePtr> stack;            // Value stack
    std::vector<CallFrame> frames;          // Call frames
    std::map<std::string, ValuePtr> globals; // Global variables
    bool hadError;
    std::string errorMessage;

    // Main execution loop
    void run();

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
