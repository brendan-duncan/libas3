#pragma once

#include <cstdint>
#include <string>

namespace as3 {

// Bytecode instruction opcodes
// Stack-based VM: instructions pop operands from stack and push results
enum class OpCode : uint8_t {
    // Constants (1 byte opcode + operands)
    OP_CONSTANT,        // Push constant from pool [u16 index]
    OP_CONSTANT_LONG,   // Push constant from pool [u32 index]
    OP_TRUE,            // Push true
    OP_FALSE,           // Push false
    OP_NULL,            // Push null
    OP_UNDEFINED,       // Push undefined

    // Literals (inline values)
    OP_NUMBER_0,        // Push 0
    OP_NUMBER_1,        // Push 1
    OP_NUMBER_NEG1,     // Push -1
    OP_NUMBER,          // Push inline number [f64]

    // Stack manipulation
    OP_POP,             // Pop and discard top value
    OP_DUP,             // Duplicate top value
    OP_SWAP,            // Swap top two values

    // Variables (using constant pool for names)
    OP_GET_GLOBAL,      // Get global variable [u16 name_index]
    OP_SET_GLOBAL,      // Set global variable [u16 name_index]
    OP_DEFINE_GLOBAL,   // Define global variable [u16 name_index]
    OP_GET_LOCAL,       // Get local variable [u8 slot]
    OP_SET_LOCAL,       // Set local variable [u8 slot]
    OP_GET_UPVALUE,     // Get captured variable [u8 index]
    OP_SET_UPVALUE,     // Set captured variable [u8 index]
    OP_CLOSE_UPVALUE,   // Close upvalue when leaving scope

    // Arithmetic
    OP_ADD,             // a + b
    OP_SUBTRACT,        // a - b
    OP_MULTIPLY,        // a * b
    OP_DIVIDE,          // a / b
    OP_MODULO,          // a % b
    OP_POWER,           // a ** b
    OP_NEGATE,          // -a

    // Comparison
    OP_EQUAL,           // a == b (loose)
    OP_NOT_EQUAL,       // a != b (loose)
    OP_STRICT_EQUAL,    // a === b
    OP_STRICT_NOT_EQUAL,// a !== b
    OP_GREATER,         // a > b
    OP_GREATER_EQUAL,   // a >= b
    OP_LESS,            // a < b
    OP_LESS_EQUAL,      // a <= b

    // Logical
    OP_NOT,             // !a
    OP_AND,             // a && b (short-circuit)
    OP_OR,              // a || b (short-circuit)

    // Bitwise
    OP_BIT_AND,         // a & b
    OP_BIT_OR,          // a | b
    OP_BIT_XOR,         // a ^ b
    OP_BIT_NOT,         // ~a
    OP_LEFT_SHIFT,      // a << b
    OP_RIGHT_SHIFT,     // a >> b
    OP_UNSIGNED_RIGHT_SHIFT, // a >>> b

    // Increment/Decrement
    OP_INCREMENT,       // ++a (prefix)
    OP_DECREMENT,       // --a (prefix)
    OP_POST_INCREMENT,  // a++ (postfix)
    OP_POST_DECREMENT,  // a-- (postfix)

    // Control flow
    OP_JUMP,            // Unconditional jump [s16 offset]
    OP_JUMP_IF_FALSE,   // Jump if top is falsy [s16 offset]
    OP_JUMP_IF_TRUE,    // Jump if top is truthy [s16 offset]
    OP_LOOP,            // Jump backward [u16 offset]

    // Functions
    OP_CALL,            // Call function [u8 arg_count]
    OP_CALL_METHOD,     // Call method with 'this' binding [u8 arg_count]
    OP_CLOSURE,         // Create closure [u16 function_index] [upvalue_count]
    OP_RETURN,          // Return from function
    OP_THIS,            // Push 'this' value

    // Objects & Arrays
    OP_NEW_ARRAY,       // Create array [u16 element_count]
    OP_NEW_OBJECT,      // Create object [u16 property_count]
    OP_GET_PROPERTY,    // Get object property [u16 name_index]
    OP_SET_PROPERTY,    // Set object property [u16 name_index]
    OP_GET_INDEX,       // Get array element (computed access)
    OP_SET_INDEX,       // Set array element (computed access)

    // Classes
    OP_CLASS,           // Define class [u16 name_index]
    OP_METHOD,          // Define method [u16 name_index]
    OP_INHERIT,         // Set up inheritance
    OP_INSTANTIATE,     // Create class instance [u8 arg_count]
    OP_GET_SUPER,       // Access super property [u16 name_index]
    OP_INVOKE,          // Optimized method call [u16 name] [u8 arg_count]
    OP_SUPER_INVOKE,    // Optimized super call [u16 name] [u8 arg_count]

    // Special
    OP_TYPEOF,          // typeof operator
    OP_INSTANCEOF,      // instanceof operator
    OP_IN,              // in operator
    OP_DELETE,          // delete operator

    // Debugging
    OP_PRINT,           // Print top value (for debugging)
    OP_BREAKPOINT,      // Debugger breakpoint
};

// Get human-readable name for opcode
const char* getOpCodeName(OpCode op);

// Get number of operand bytes for opcode
int getOpCodeOperandCount(OpCode op);

// Instruction structure for disassembly
struct Instruction {
    OpCode opcode;
    union {
        uint8_t byte;
        uint16_t u16;
        uint32_t u32;
        int16_t s16;
        double f64;
    } operand;
};

} // namespace as3
