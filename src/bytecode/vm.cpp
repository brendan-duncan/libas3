#include "vm.h"
#include "opcode.h"
#include <iostream>
#include <cmath>

namespace as3 {

VM::VM()
    : hadError(false) {
    setupBuiltins();
}

void VM::setupBuiltins() {
    // trace() function
    globals["trace"] = Value::makeNativeFunction([](const std::vector<ValuePtr>& args) {
        for (size_t i = 0; i < args.size(); i++) {
            if (i > 0) std::cout << " ";
            std::cout << args[i]->toString();
        }
        std::cout << std::endl;
        return Value::makeUndefined();
    });

    // print() function
    globals["print"] = Value::makeNativeFunction([](const std::vector<ValuePtr>& args) {
        for (size_t i = 0; i < args.size(); i++) {
            if (i > 0) std::cout << " ";
            std::cout << args[i]->toString();
        }
        std::cout << std::endl;
        return Value::makeUndefined();
    });

    // parseInt()
    globals["parseInt"] = Value::makeNativeFunction([](const std::vector<ValuePtr>& args) {
        if (args.empty()) return Value::makeNumber(std::nan(""));
        std::string str = args[0]->toString();
        try {
            return Value::makeNumber(std::stoi(str));
        } catch (...) {
            return Value::makeNumber(std::nan(""));
        }
    });

    // parseFloat()
    globals["parseFloat"] = Value::makeNativeFunction([](const std::vector<ValuePtr>& args) {
        if (args.empty()) return Value::makeNumber(std::nan(""));
        std::string str = args[0]->toString();
        try {
            return Value::makeNumber(std::stod(str));
        } catch (...) {
            return Value::makeNumber(std::nan(""));
        }
    });

    // isNaN()
    globals["isNaN"] = Value::makeNativeFunction([](const std::vector<ValuePtr>& args) {
        if (args.empty()) return Value::makeBoolean(true);
        double num = args[0]->toNumber();
        return Value::makeBoolean(std::isnan(num));
    });

    // Global constants
    globals["undefined"] = Value::makeUndefined();
    globals["null"] = Value::makeNull();
    globals["NaN"] = Value::makeNumber(std::nan(""));
    globals["Infinity"] = Value::makeNumber(std::numeric_limits<double>::infinity());
}

void VM::execute(std::shared_ptr<BytecodeFunction> function) {
    resetStack();
    frames.clear();
    frames.emplace_back(function.get(), 0);
    hadError = false;

    try {
        run();
    } catch (const std::exception& e) {
        runtimeError(e.what());
    }
}

void VM::run() {
    while (true) {
        CallFrame& frame = currentFrame();
        Chunk& chunk = currentChunk();

        // Decode instruction
        uint8_t instruction = readByte();
        OpCode op = static_cast<OpCode>(instruction);

        // Dispatch
        switch (op) {
            case OpCode::OP_CONSTANT: {
                ValuePtr constant = readConstant();
                push(constant);
                break;
            }

            case OpCode::OP_TRUE:
                push(Value::makeBoolean(true));
                break;

            case OpCode::OP_FALSE:
                push(Value::makeBoolean(false));
                break;

            case OpCode::OP_NULL:
                push(Value::makeNull());
                break;

            case OpCode::OP_UNDEFINED:
                push(Value::makeUndefined());
                break;

            case OpCode::OP_NUMBER_0:
                push(Value::makeNumber(0));
                break;

            case OpCode::OP_NUMBER_1:
                push(Value::makeNumber(1));
                break;

            case OpCode::OP_NUMBER_NEG1:
                push(Value::makeNumber(-1));
                break;

            case OpCode::OP_POP:
                pop();
                break;

            case OpCode::OP_DUP:
                push(peek(0));
                break;

            case OpCode::OP_GET_GLOBAL: {
                uint16_t nameIndex = readU16();
                ValuePtr name = chunk.getConstant(nameIndex);
                ValuePtr value = getGlobal(name->toString());
                push(value);
                break;
            }

            case OpCode::OP_SET_GLOBAL: {
                uint16_t nameIndex = readU16();
                ValuePtr name = chunk.getConstant(nameIndex);
                setGlobal(name->toString(), peek(0));
                break;
            }

            case OpCode::OP_DEFINE_GLOBAL: {
                uint16_t nameIndex = readU16();
                ValuePtr name = chunk.getConstant(nameIndex);
                setGlobal(name->toString(), pop());
                break;
            }

            case OpCode::OP_GET_LOCAL: {
                uint8_t slot = readByte();
                push(stack[frame.stackOffset + slot]);
                break;
            }

            case OpCode::OP_SET_LOCAL: {
                uint8_t slot = readByte();
                stack[frame.stackOffset + slot] = peek(0);
                break;
            }

            case OpCode::OP_ADD:
                execAdd();
                break;

            case OpCode::OP_SUBTRACT:
                execSubtract();
                break;

            case OpCode::OP_MULTIPLY:
                execMultiply();
                break;

            case OpCode::OP_DIVIDE:
                execDivide();
                break;

            case OpCode::OP_MODULO:
                execModulo();
                break;

            case OpCode::OP_POWER: {
                ValuePtr b = pop();
                ValuePtr a = pop();
                push(Value::makeNumber(std::pow(a->toNumber(), b->toNumber())));
                break;
            }

            case OpCode::OP_NEGATE:
                execNegate();
                break;

            case OpCode::OP_NOT:
                execNot();
                break;

            case OpCode::OP_EQUAL:
                execEqual();
                break;

            case OpCode::OP_NOT_EQUAL:
                execNotEqual();
                break;

            case OpCode::OP_STRICT_EQUAL:
                execStrictEqual();
                break;

            case OpCode::OP_STRICT_NOT_EQUAL:
                execStrictNotEqual();
                break;

            case OpCode::OP_GREATER:
                execGreater();
                break;

            case OpCode::OP_GREATER_EQUAL:
                execGreaterEqual();
                break;

            case OpCode::OP_LESS:
                execLess();
                break;

            case OpCode::OP_LESS_EQUAL:
                execLessEqual();
                break;

            case OpCode::OP_BIT_AND:
                execBitwiseAnd();
                break;

            case OpCode::OP_BIT_OR:
                execBitwiseOr();
                break;

            case OpCode::OP_BIT_XOR:
                execBitwiseXor();
                break;

            case OpCode::OP_BIT_NOT:
                execBitwiseNot();
                break;

            case OpCode::OP_LEFT_SHIFT:
                execLeftShift();
                break;

            case OpCode::OP_RIGHT_SHIFT:
                execRightShift();
                break;

            case OpCode::OP_UNSIGNED_RIGHT_SHIFT:
                execUnsignedRightShift();
                break;

            case OpCode::OP_AND: {
                ValuePtr b = pop();
                ValuePtr a = pop();
                push(a->toBoolean() ? b : a);
                break;
            }

            case OpCode::OP_OR: {
                ValuePtr b = pop();
                ValuePtr a = pop();
                push(a->toBoolean() ? a : b);
                break;
            }

            case OpCode::OP_JUMP: {
                int16_t offset = readS16();
                frame.ip += offset;
                break;
            }

            case OpCode::OP_JUMP_IF_FALSE: {
                int16_t offset = readS16();
                if (!peek(0)->toBoolean()) {
                    frame.ip += offset;
                }
                break;
            }

            case OpCode::OP_JUMP_IF_TRUE: {
                int16_t offset = readS16();
                if (peek(0)->toBoolean()) {
                    frame.ip += offset;
                }
                break;
            }

            case OpCode::OP_LOOP: {
                uint16_t offset = readU16();
                frame.ip -= offset;
                break;
            }

            case OpCode::OP_CLOSURE: {
                // Load function from constant pool
                uint16_t constantIndex = readU16();
                ValuePtr functionValue = chunk.getConstant(constantIndex);

                // Push the function onto the stack
                push(functionValue);

                // TODO: Handle upvalues for closures
                break;
            }

            case OpCode::OP_THIS: {
                // Push the 'this' value from current call frame
                if (!frames.empty()) {
                    ValuePtr thisValue = currentFrame().thisBinding;
                    push(thisValue ? thisValue : Value::makeUndefined());
                } else {
                    push(Value::makeUndefined());
                }
                break;
            }

            case OpCode::OP_CALL: {
                uint8_t argCount = readByte();
                // Get function
                ValuePtr callee = peek(argCount);

                if (callee->type == ValueType::NativeFunction) {
                    // Native function call
                    std::vector<ValuePtr> args;
                    for (int i = argCount - 1; i >= 0; i--) {
                        args.push_back(peek(i));
                    }

                    ValuePtr result = callee->nativeFunc(args);

                    // Pop arguments and function
                    for (uint8_t i = 0; i <= argCount; i++) {
                        pop();
                    }

                    push(result);
                } else if (callee->type == ValueType::Function) {
                    // Bytecode function call
                    BytecodeFunction* func = callee->bytecodeFunc.get();

                    // Check arity
                    if (argCount != func->arity) {
                        runtimeError("Expected " + std::to_string(func->arity) +
                                   " arguments but got " + std::to_string(argCount));
                        return;
                    }

                    // Create new call frame
                    // Stack layout: [... function arg0 arg1 ...]
                    // stackOffset points to arg0 (first local/parameter)
                    size_t stackOffset = stack.size() - argCount;
                    frames.emplace_back(func, stackOffset);

                    // Execution continues in the new frame
                } else {
                    runtimeError("Can only call functions");
                    return;
                }
                break;
            }

            case OpCode::OP_CALL_METHOD: {
                uint8_t argCount = readByte();
                // Stack layout: [... receiver function arg0 arg1 ...]
                // Get function at peek(argCount)
                ValuePtr callee = peek(argCount);
                // Get receiver at peek(argCount + 1)
                ValuePtr receiver = peek(argCount + 1);

                if (callee->type == ValueType::NativeFunction) {
                    // Native function call
                    std::vector<ValuePtr> args;
                    for (int i = argCount - 1; i >= 0; i--) {
                        args.push_back(peek(i));
                    }

                    ValuePtr result = callee->nativeFunc(args);

                    // Pop arguments, function, and receiver
                    for (uint8_t i = 0; i <= argCount + 1; i++) {
                        pop();
                    }

                    push(result);
                } else if (callee->type == ValueType::Function) {
                    // Bytecode function call with 'this' binding
                    BytecodeFunction* func = callee->bytecodeFunc.get();

                    // Check arity
                    if (argCount != func->arity) {
                        runtimeError("Expected " + std::to_string(func->arity) +
                                   " arguments but got " + std::to_string(argCount));
                        return;
                    }

                    // Need to reorganize stack to remove receiver from under the arguments
                    // Current: [... receiver function arg0 arg1 ...]
                    // Need: [... function arg0 arg1 ...] with receiver saved in thisBinding

                    // Save arguments temporarily
                    std::vector<ValuePtr> args;
                    for (int i = 0; i < argCount; i++) {
                        args.push_back(pop());
                    }
                    // Now stack is: [... receiver function]

                    // Pop and save function
                    ValuePtr funcValue = pop();
                    // Pop receiver (we already have it saved)
                    pop();

                    // Push function back
                    push(funcValue);

                    // Push arguments back in reverse order
                    for (int i = argCount - 1; i >= 0; i--) {
                        push(args[i]);
                    }

                    // Now stack is: [... function arg0 arg1 ...]
                    // stackOffset points to arg0 (first local/parameter)
                    size_t stackOffset = stack.size() - argCount;
                    frames.emplace_back(func, stackOffset, receiver);

                    // Execution continues in the new frame
                } else {
                    runtimeError("Can only call functions");
                    return;
                }
                break;
            }

            case OpCode::OP_RETURN: {
                ValuePtr result = pop();

                // Check if this is a constructor return
                bool wasConstructor = frame.isConstructor;
                ValuePtr constructorInstance = frame.constructorInstance;

                // Pop frame
                frames.pop_back();

                if (frames.empty()) {
                    // Top-level return
                    return;
                }

                // Pop all locals from the returning function
                while (stack.size() > frame.stackOffset) {
                    pop();
                }

                // Pop the function itself (it's at stackOffset - 1)
                if (!stack.empty()) {
                    pop();
                }

                // For constructors, return the instance instead of the function's return value
                // unless the constructor explicitly returned a non-undefined value
                if (wasConstructor && constructorInstance) {
                    if (result->isUndefined()) {
                        push(constructorInstance);
                    } else {
                        // Constructor explicitly returned something, use that
                        push(result);
                    }
                } else {
                    // Regular function, push return value
                    push(result);
                }
                break;
            }

            case OpCode::OP_CLASS: {
                // Create a new class
                uint16_t nameIndex = readU16();
                ValuePtr name = chunk.getConstant(nameIndex);
                ValuePtr classValue = Value::makeClass(name->toString());
                push(classValue);
                break;
            }

            case OpCode::OP_METHOD: {
                // Add a method to a class
                // Stack: [... class method]
                uint16_t nameIndex = readU16();
                ValuePtr name = chunk.getConstant(nameIndex);
                ValuePtr method = pop();
                ValuePtr classValue = peek(0);

                if (classValue->isClass()) {
                    classValue->setProperty(name->toString(), method);
                }
                break;
            }

            case OpCode::OP_INHERIT: {
                // Set up inheritance
                // Stack: [... superclass subclass]
                ValuePtr subclass = pop();
                ValuePtr superclass = peek(0);

                if (subclass->isClass() && superclass->isClass()) {
                    subclass->superClass = superclass;
                    // Copy methods from superclass to subclass
                    for (auto& prop : superclass->properties) {
                        if (!subclass->properties.count(prop.first)) {
                            subclass->setProperty(prop.first, prop.second);
                        }
                    }
                }

                pop(); // Pop superclass
                break;
            }

            case OpCode::OP_INSTANTIATE: {
                // Create instance of a class
                // Stack: [... class arg1 arg2 ...]
                uint8_t argCount = readByte();

                // Get class at position after all arguments
                ValuePtr classValue = peek(argCount);

                if (!classValue->isClass()) {
                    runtimeError("Can only instantiate classes");
                    return;
                }

                // Create new instance (empty object)
                ValuePtr instance = Value::makeObject();

                // Copy all methods from class to instance
                for (auto& prop : classValue->properties) {
                    instance->setProperty(prop.first, prop.second);
                }

                // Store reference to class
                instance->setProperty("__class__", classValue);

                // Check if there's a constructor (method with same name as class)
                ValuePtr constructor = classValue->getProperty(classValue->className);

                if (constructor && constructor->isFunction()) {
                    // Call constructor with 'this' bound to instance
                    // Rearrange stack: [class arg1 arg2 ...] -> [instance constructor arg1 arg2 ...]

                    // Pop arguments
                    std::vector<ValuePtr> args;
                    for (int i = 0; i < argCount; i++) {
                        args.push_back(pop());
                    }

                    // Pop class
                    pop();

                    // Push instance (receiver)
                    push(instance);

                    // Push constructor function
                    push(constructor);

                    // Push arguments back in correct order
                    for (int i = argCount - 1; i >= 0; i--) {
                        push(args[i]);
                    }

                    // Call constructor as method
                    // Stack is now: [instance constructor arg1 arg2 ...]
                    BytecodeFunction* func = constructor->bytecodeFunc.get();

                    if (argCount != func->arity) {
                        runtimeError("Constructor expected " + std::to_string(func->arity) +
                                   " arguments but got " + std::to_string(argCount));
                        return;
                    }

                    // Reorganize stack for call frame
                    // Need to extract receiver and remove it from under arguments
                    std::vector<ValuePtr> tempArgs;
                    for (int i = 0; i < argCount; i++) {
                        tempArgs.push_back(pop());
                    }

                    ValuePtr funcValue = pop();
                    ValuePtr receiver = pop();

                    // Push function back
                    push(funcValue);

                    // Push arguments back
                    for (int i = argCount - 1; i >= 0; i--) {
                        push(tempArgs[i]);
                    }

                    // Create call frame with 'this' binding and mark as constructor
                    size_t stackOffset = stack.size() - argCount;
                    frames.emplace_back(func, stackOffset, receiver, true, instance);

                    // When constructor returns, OP_RETURN will return the instance instead of undefined
                } else {
                    // No constructor, just return the instance
                    // Pop arguments and class
                    for (uint8_t i = 0; i <= argCount; i++) {
                        pop();
                    }

                    push(instance);
                }

                break;
            }

            case OpCode::OP_NEW_ARRAY: {
                uint16_t count = readU16();
                ValuePtr array = Value::makeArray();
                for (int i = count - 1; i >= 0; i--) {
                    array->elements.push_back(peek(i));
                }
                for (uint16_t i = 0; i < count; i++) {
                    pop();
                }
                push(array);
                break;
            }

            case OpCode::OP_NEW_OBJECT: {
                uint16_t count = readU16();
                ValuePtr obj = Value::makeObject();
                for (uint16_t i = 0; i < count; i++) {
                    ValuePtr value = pop();
                    ValuePtr key = pop();
                    obj->setProperty(key->toString(), value);
                }
                push(obj);
                break;
            }

            case OpCode::OP_GET_PROPERTY: {
                uint16_t nameIndex = readU16();
                ValuePtr name = chunk.getConstant(nameIndex);
                ValuePtr obj = pop();
                push(obj->getProperty(name->toString()));
                break;
            }

            case OpCode::OP_SET_PROPERTY: {
                uint16_t nameIndex = readU16();
                ValuePtr name = chunk.getConstant(nameIndex);
                ValuePtr value = pop();
                ValuePtr obj = pop();
                obj->setProperty(name->toString(), value);
                push(value);
                break;
            }

            case OpCode::OP_GET_INDEX: {
                ValuePtr index = pop();
                ValuePtr obj = pop();
                if (obj->isArray()) {
                    push(obj->getElement(static_cast<size_t>(index->toNumber())));
                } else {
                    push(obj->getProperty(index->toString()));
                }
                break;
            }

            case OpCode::OP_SET_INDEX: {
                ValuePtr value = pop();
                ValuePtr index = pop();
                ValuePtr obj = pop();
                if (obj->isArray()) {
                    obj->setElement(static_cast<size_t>(index->toNumber()), value);
                } else {
                    obj->setProperty(index->toString(), value);
                }
                push(value);
                break;
            }

            case OpCode::OP_GET_SUPER: {
                // Access a property from the superclass
                // Stack: [instance]
                uint16_t nameIndex = readU16();
                ValuePtr name = chunk.getConstant(nameIndex);
                ValuePtr instance = pop();

                // Get the class from the instance
                ValuePtr classValue = instance->getProperty("__class__");

                if (classValue && classValue->isClass() && classValue->superClass) {
                    // Look up property in superclass
                    ValuePtr value = classValue->superClass->getProperty(name->toString());
                    push(value);
                } else {
                    push(Value::makeUndefined());
                }
                break;
            }

            case OpCode::OP_PRINT: {
                std::cout << pop()->toString() << std::endl;
                break;
            }

            default:
                runtimeError("Unknown opcode: " + std::to_string(static_cast<int>(op)));
                return;
        }
    }
}

// Stack operations

void VM::push(ValuePtr value) {
    stack.push_back(value);
}

ValuePtr VM::pop() {
    if (stack.empty()) {
        runtimeError("Stack underflow");
        return Value::makeUndefined();
    }
    ValuePtr value = stack.back();
    stack.pop_back();
    return value;
}

ValuePtr VM::peek(size_t distance) {
    if (distance >= stack.size()) {
        runtimeError("Stack peek out of bounds");
        return Value::makeUndefined();
    }
    return stack[stack.size() - 1 - distance];
}

void VM::resetStack() {
    stack.clear();
}

CallFrame& VM::currentFrame() {
    return frames.back();
}

Chunk& VM::currentChunk() {
    return currentFrame().function->chunk;
}

uint8_t VM::readByte() {
    CallFrame& frame = currentFrame();
    return currentChunk().getByte(frame.ip++);
}

uint16_t VM::readU16() {
    CallFrame& frame = currentFrame();
    uint16_t value = currentChunk().getU16(frame.ip);
    frame.ip += 2;
    return value;
}

int16_t VM::readS16() {
    return static_cast<int16_t>(readU16());
}

double VM::readF64() {
    CallFrame& frame = currentFrame();
    double value = currentChunk().getF64(frame.ip);
    frame.ip += 8;
    return value;
}

ValuePtr VM::readConstant() {
    uint16_t index = readU16();
    return currentChunk().getConstant(index);
}

// Arithmetic operations

void VM::execAdd() {
    ValuePtr b = pop();
    ValuePtr a = pop();

    // String concatenation or numeric addition
    if (a->isString() || b->isString()) {
        push(Value::makeString(a->toString() + b->toString()));
    } else {
        push(Value::makeNumber(a->toNumber() + b->toNumber()));
    }
}

void VM::execSubtract() {
    ValuePtr b = pop();
    ValuePtr a = pop();
    push(Value::makeNumber(a->toNumber() - b->toNumber()));
}

void VM::execMultiply() {
    ValuePtr b = pop();
    ValuePtr a = pop();
    push(Value::makeNumber(a->toNumber() * b->toNumber()));
}

void VM::execDivide() {
    ValuePtr b = pop();
    ValuePtr a = pop();
    push(Value::makeNumber(a->toNumber() / b->toNumber()));
}

void VM::execModulo() {
    ValuePtr b = pop();
    ValuePtr a = pop();
    push(Value::makeNumber(std::fmod(a->toNumber(), b->toNumber())));
}

void VM::execNegate() {
    ValuePtr a = pop();
    push(Value::makeNumber(-a->toNumber()));
}

void VM::execNot() {
    ValuePtr a = pop();
    push(Value::makeBoolean(!a->toBoolean()));
}

// Comparison operations

void VM::execEqual() {
    ValuePtr b = pop();
    ValuePtr a = pop();

    if (a->type == b->type) {
        if (a->isNumber()) {
            push(Value::makeBoolean(a->numberValue == b->numberValue));
        } else if (a->isString()) {
            push(Value::makeBoolean(a->stringValue == b->stringValue));
        } else if (a->isBoolean()) {
            push(Value::makeBoolean(a->boolValue == b->boolValue));
        } else {
            push(Value::makeBoolean(a.get() == b.get()));
        }
    } else {
        push(Value::makeBoolean(false));
    }
}

void VM::execNotEqual() {
    ValuePtr b = pop();
    ValuePtr a = pop();

    if (a->type == b->type) {
        if (a->isNumber()) {
            push(Value::makeBoolean(a->numberValue != b->numberValue));
        } else if (a->isString()) {
            push(Value::makeBoolean(a->stringValue != b->stringValue));
        } else if (a->isBoolean()) {
            push(Value::makeBoolean(a->boolValue != b->boolValue));
        } else {
            push(Value::makeBoolean(a.get() != b.get()));
        }
    } else {
        push(Value::makeBoolean(true));
    }
}

void VM::execStrictEqual() {
    ValuePtr b = pop();
    ValuePtr a = pop();

    if (a->type != b->type) {
        push(Value::makeBoolean(false));
        return;
    }

    if (a->isNumber()) {
        push(Value::makeBoolean(a->numberValue == b->numberValue));
    } else if (a->isString()) {
        push(Value::makeBoolean(a->stringValue == b->stringValue));
    } else if (a->isBoolean()) {
        push(Value::makeBoolean(a->boolValue == b->boolValue));
    } else {
        push(Value::makeBoolean(a.get() == b.get()));
    }
}

void VM::execStrictNotEqual() {
    ValuePtr b = pop();
    ValuePtr a = pop();

    if (a->type != b->type) {
        push(Value::makeBoolean(true));
        return;
    }

    if (a->isNumber()) {
        push(Value::makeBoolean(a->numberValue != b->numberValue));
    } else if (a->isString()) {
        push(Value::makeBoolean(a->stringValue != b->stringValue));
    } else if (a->isBoolean()) {
        push(Value::makeBoolean(a->boolValue != b->boolValue));
    } else {
        push(Value::makeBoolean(a.get() != b.get()));
    }
}

void VM::execGreater() {
    ValuePtr b = pop();
    ValuePtr a = pop();
    push(Value::makeBoolean(a->toNumber() > b->toNumber()));
}

void VM::execGreaterEqual() {
    ValuePtr b = pop();
    ValuePtr a = pop();
    push(Value::makeBoolean(a->toNumber() >= b->toNumber()));
}

void VM::execLess() {
    ValuePtr b = pop();
    ValuePtr a = pop();
    push(Value::makeBoolean(a->toNumber() < b->toNumber()));
}

void VM::execLessEqual() {
    ValuePtr b = pop();
    ValuePtr a = pop();
    push(Value::makeBoolean(a->toNumber() <= b->toNumber()));
}

// Bitwise operations

void VM::execBitwiseAnd() {
    ValuePtr b = pop();
    ValuePtr a = pop();
    push(Value::makeNumber(static_cast<int32_t>(a->toNumber()) & static_cast<int32_t>(b->toNumber())));
}

void VM::execBitwiseOr() {
    ValuePtr b = pop();
    ValuePtr a = pop();
    push(Value::makeNumber(static_cast<int32_t>(a->toNumber()) | static_cast<int32_t>(b->toNumber())));
}

void VM::execBitwiseXor() {
    ValuePtr b = pop();
    ValuePtr a = pop();
    push(Value::makeNumber(static_cast<int32_t>(a->toNumber()) ^ static_cast<int32_t>(b->toNumber())));
}

void VM::execBitwiseNot() {
    ValuePtr a = pop();
    push(Value::makeNumber(~static_cast<int32_t>(a->toNumber())));
}

void VM::execLeftShift() {
    ValuePtr b = pop();
    ValuePtr a = pop();
    push(Value::makeNumber(static_cast<int32_t>(a->toNumber()) << static_cast<int32_t>(b->toNumber())));
}

void VM::execRightShift() {
    ValuePtr b = pop();
    ValuePtr a = pop();
    push(Value::makeNumber(static_cast<int32_t>(a->toNumber()) >> static_cast<int32_t>(b->toNumber())));
}

void VM::execUnsignedRightShift() {
    ValuePtr b = pop();
    ValuePtr a = pop();
    push(Value::makeNumber(static_cast<uint32_t>(a->toNumber()) >> static_cast<uint32_t>(b->toNumber())));
}

// Global variables

ValuePtr VM::getGlobal(const std::string& name) {
    auto it = globals.find(name);
    if (it != globals.end()) {
        return it->second;
    }
    return Value::makeUndefined();
}

void VM::setGlobal(const std::string& name, ValuePtr value) {
    globals[name] = value;
}

void VM::registerBuiltin(const std::string& name, NativeFunction func) {
    globals[name] = Value::makeNativeFunction(func);
}

void VM::defineExternClass(const std::string& className,
                           const std::map<std::string, NativeFunction>& methods) {
    ValuePtr classValue = Value::makeClass(className);
    
    for (const auto& pair : methods) {
        classValue->setProperty(pair.first, Value::makeNativeFunction(pair.second));
    }
    
    globals[className] = classValue;
}

void VM::registerObject(const std::string& name, const std::string& classType) {
    ValuePtr classValue = getGlobal(classType);
    
    if (!classValue->isClass()) {
        runtimeError("Cannot register object: class '" + classType + "' not found");
        return;
    }
    
    ValuePtr instance = Value::makeObject();
    
    for (auto& prop : classValue->properties) {
        instance->setProperty(prop.first, prop.second);
    }
    
    instance->setProperty("__class__", classValue);
    
    globals[name] = instance;
}

// Error handling

void VM::runtimeError(const std::string& message) {
    hadError = true;
    errorMessage = message;

    CallFrame& frame = currentFrame();
    size_t instruction = frame.ip - 1;
    int line = currentChunk().getLine(instruction);

    std::cerr << "Runtime error";
    if (line >= 0) {
        std::cerr << " at line " << line;
    }
    std::cerr << ": " << message << std::endl;

    throw std::runtime_error(message);
}

} // namespace as3
