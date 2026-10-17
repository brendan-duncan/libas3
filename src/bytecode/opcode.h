#pragma once

#include <cstdint>
#include <string>

namespace as3 {

// Bytecode instruction opcodes, for a stack machine: instructions take
// their operands from the stack and push their results. Operand bytes
// after the opcode are given in brackets: u8, u16 (indices), s32 (jumps,
// relative to the end of the instruction).
enum class OpCode : uint8_t {
    // Constants
    OP_CONSTANT,        // Push constant [u16 index]
    OP_UNDEFINED,
    OP_NULL,
    OP_TRUE,
    OP_FALSE,
    OP_ZERO,
    OP_ONE,

    // Stack manipulation
    OP_POP,
    OP_DUP,             // a -> a a
    OP_DUP2,            // a b -> a b a b
    OP_SWAP,            // a b -> b a
    OP_DUP_X1,          // a b -> b a b
    OP_DUP_X2,          // a b c -> c a b c

    // Variables
    OP_GET_LOCAL,       // [u16 slot]
    OP_SET_LOCAL,       // [u16 slot] stores the top, keeps it
    OP_PUT_LOCAL,       // [u16 slot] stores the top, pops it
    OP_GET_CELL,        // [u16 cell] a captured local of this function
    OP_SET_CELL,        // [u16 cell]
    OP_PUT_CELL,        // [u16 cell]
    OP_GET_UPVALUE,     // [u16 index] a variable captured from outside
    OP_SET_UPVALUE,     // [u16 index]
    OP_PUT_UPVALUE,     // [u16 index]
    OP_INC_LOCAL,       // [u16 slot][u8 delta+128][u8 coerce] local = coerce(ToNumber(local) + delta)
    OP_GET_NAME,        // [u16 name][u16 cache] a name looked up at run time
    OP_SET_NAME,        // [u16 name][u16 cache] keeps the value
    OP_TYPEOF_NAME,     // [u16 name] typeof of a name that may not exist
    OP_DEFINE_VAR,      // [u16 name] a top-level var: defined with the top's value unless it exists; pops
    OP_DEFINE_FUNCTION, // [u16 name] a top-level function: defined with the top's value; pops
    OP_GET_QNAME,       // [u16 name] a definition by qualified name (a.b.C)
    // In a with block: the name on one of its objects (in locals, the
    // innermost first) when one has it. [u16 name][u8 count][u16 local]...[s32]
    OP_WITH_GET,        // pushes its value and jumps, or goes on
    OP_WITH_SET,        // value -> value: sets it and jumps, or goes on
    OP_WITH_FIND,       // pushes the object that has it, or jumps

    // Properties
    OP_GET_PROP,        // [u16 name][u16 cache] obj -> value
    OP_SET_PROP,        // [u16 name][u16 cache] obj value -> value
    OP_DELETE_PROP,     // [u16 name] obj -> boolean
    OP_GET_INDEX,       // obj key -> value
    OP_SET_INDEX,       // obj key value -> value
    OP_DELETE_INDEX,    // obj key -> boolean
    OP_GET_SUPER,       // [u16 name] -> value (on this)
    OP_SET_SUPER,       // [u16 name] value -> value
    // The same, on a local or on the instance a method's names find,
    // without putting the object on the stack.
    OP_GET_LOCAL_PROP,  // [u16 slot][u16 name][u16 cache] -> value
    OP_SET_LOCAL_PROP,  // [u16 slot][u16 name][u16 cache] value -> value
    OP_GET_THIS_PROP,   // [u16 name][u16 cache] -> value
    OP_SET_THIS_PROP,   // [u16 name][u16 cache] value -> value

    // Arithmetic
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_MODULO,
    OP_NEGATE,
    OP_TO_NUMBER,       // unary +
    OP_INCREMENT,       // ToNumber(a) + 1
    OP_DECREMENT,       // ToNumber(a) - 1

    // Comparison
    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_STRICT_EQUAL,
    OP_STRICT_NOT_EQUAL,
    OP_GREATER,
    OP_GREATER_EQUAL,
    OP_LESS,
    OP_LESS_EQUAL,

    // Logical and bitwise
    OP_NOT,
    OP_BIT_AND,
    OP_BIT_OR,
    OP_BIT_XOR,
    OP_BIT_NOT,
    OP_LEFT_SHIFT,
    OP_RIGHT_SHIFT,
    OP_UNSIGNED_RIGHT_SHIFT,

    // Types
    OP_TYPEOF,
    OP_INSTANCEOF,      // value class -> boolean
    OP_IN,              // key obj -> boolean
    OP_IS,              // value type -> boolean
    OP_AS,              // value type -> value or null
    OP_COERCE,          // [u8 Coerce]

    // Control flow
    OP_JUMP,                    // [s32]
    OP_JUMP_IF_FALSE,           // [s32] pops
    OP_JUMP_IF_TRUE,            // [s32] pops
    OP_JUMP_IF_FALSE_KEEP,      // [s32] keeps the value when jumping, pops it otherwise
    OP_JUMP_IF_TRUE_KEEP,       // [s32]
    OP_JUMP_IF_NOT_NULLISH_KEEP,// [s32]

    // Functions
    OP_CALL,            // [u8 argc] fn args... -> result ('this' undefined)
    OP_CALL_PROP,       // [u16 name][u8 argc][u16 cache] obj args... -> result
    OP_CALL_NAME,       // [u16 name][u8 argc][u16 cache] args... -> result
    OP_CALL_INDEX,      // [u8 argc] obj key args... -> result
    OP_CALL_SUPER,      // [u16 name][u8 argc] args... -> result
    OP_CALL_THIS_PROP,  // [u16 name][u8 argc][u16 cache] place args... -> result
    OP_CALL_LOCAL_PROP, // [u16 slot][u16 name][u8 argc][u16 cache] place args... -> result
    OP_SUPER_CTOR,      // [u8 argc] args... -> undefined
    OP_NEW,             // [u8 argc] class args... -> instance
    OP_RETURN,
    OP_RETURN_UNDEFINED,
    OP_THIS,
    OP_SCOPE_THIS,      // the instance names in a method (or a function in one) find
    OP_OWNER_CLASS,     // the class the running method belongs to
    OP_CLOSURE,         // [u16 function index]
    OP_DEFAULT_ARG,     // [u16 param][s32] jumps when the argument was given

    // Objects and arrays
    OP_NEW_ARRAY,       // [u16 count]
    OP_NEW_OBJECT,      // [u16 pairs] key value ... -> object
    OP_NEW_VECTOR,      // [u16 count] type elements... -> vector
    OP_NEW_REGEXP,      // [u16 pattern][u16 flags]
    OP_APPLY_TYPE,      // [u8 count] base types... -> class (Vector.<int>)
    OP_DEFINE_CLASS,    // [u16 class index] -> class

    // Exceptions and iteration
    OP_THROW,
    OP_ITER_INIT,       // [u8 each] obj -> iterator
    OP_ITER_NEXT,       // [u16 iterator local][s32] pushes the next key (or value), or jumps when done

    OP_NOP,

    // Two instructions in one, written over the first of a pair the
    // compiler made (Chunk::fuse): the first's operands, then the second
    // instruction as it was, which the fused one runs too. The second
    // stays whole, so that a jump to it still finds it.
    OP_GET_LOCAL_CONSTANT,      // GET_LOCAL, CONSTANT
    OP_GET_LOCAL_LOCAL,         // GET_LOCAL, GET_LOCAL
    OP_COERCE_PUT_LOCAL,        // COERCE, PUT_LOCAL
    OP_SET_LOCAL_PROP_POP,      // SET_LOCAL_PROP, POP
    OP_SET_THIS_PROP_POP,       // SET_THIS_PROP, POP
    // A comparison, then JUMP_IF_TRUE or JUMP_IF_FALSE.
    OP_LESS_JUMP,
    OP_GREATER_JUMP,
    OP_LESS_EQUAL_JUMP,
    OP_GREATER_EQUAL_JUMP,
    OP_EQUAL_JUMP,
    OP_NOT_EQUAL_JUMP,
    OP_STRICT_EQUAL_JUMP,
    OP_STRICT_NOT_EQUAL_JUMP,
};

// Get human-readable name for opcode
const char* getOpCodeName(OpCode op);

// The number of operand bytes after the opcode.
int getOpCodeOperandCount(OpCode op);

} // namespace as3
