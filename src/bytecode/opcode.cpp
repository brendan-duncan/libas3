#include "opcode.h"

namespace as3 {

const char* getOpCodeName(OpCode op) {
    switch (op) {
        case OpCode::OP_CONSTANT: return "OP_CONSTANT";
        case OpCode::OP_CONSTANT_LONG: return "OP_CONSTANT_LONG";
        case OpCode::OP_TRUE: return "OP_TRUE";
        case OpCode::OP_FALSE: return "OP_FALSE";
        case OpCode::OP_NULL: return "OP_NULL";
        case OpCode::OP_UNDEFINED: return "OP_UNDEFINED";

        case OpCode::OP_NUMBER_0: return "OP_NUMBER_0";
        case OpCode::OP_NUMBER_1: return "OP_NUMBER_1";
        case OpCode::OP_NUMBER_NEG1: return "OP_NUMBER_NEG1";
        case OpCode::OP_NUMBER: return "OP_NUMBER";

        case OpCode::OP_POP: return "OP_POP";
        case OpCode::OP_DUP: return "OP_DUP";
        case OpCode::OP_SWAP: return "OP_SWAP";

        case OpCode::OP_GET_GLOBAL: return "OP_GET_GLOBAL";
        case OpCode::OP_SET_GLOBAL: return "OP_SET_GLOBAL";
        case OpCode::OP_DEFINE_GLOBAL: return "OP_DEFINE_GLOBAL";
        case OpCode::OP_GET_LOCAL: return "OP_GET_LOCAL";
        case OpCode::OP_SET_LOCAL: return "OP_SET_LOCAL";
        case OpCode::OP_GET_UPVALUE: return "OP_GET_UPVALUE";
        case OpCode::OP_SET_UPVALUE: return "OP_SET_UPVALUE";
        case OpCode::OP_CLOSE_UPVALUE: return "OP_CLOSE_UPVALUE";

        case OpCode::OP_ADD: return "OP_ADD";
        case OpCode::OP_SUBTRACT: return "OP_SUBTRACT";
        case OpCode::OP_MULTIPLY: return "OP_MULTIPLY";
        case OpCode::OP_DIVIDE: return "OP_DIVIDE";
        case OpCode::OP_MODULO: return "OP_MODULO";
        case OpCode::OP_POWER: return "OP_POWER";
        case OpCode::OP_NEGATE: return "OP_NEGATE";

        case OpCode::OP_EQUAL: return "OP_EQUAL";
        case OpCode::OP_NOT_EQUAL: return "OP_NOT_EQUAL";
        case OpCode::OP_STRICT_EQUAL: return "OP_STRICT_EQUAL";
        case OpCode::OP_STRICT_NOT_EQUAL: return "OP_STRICT_NOT_EQUAL";
        case OpCode::OP_GREATER: return "OP_GREATER";
        case OpCode::OP_GREATER_EQUAL: return "OP_GREATER_EQUAL";
        case OpCode::OP_LESS: return "OP_LESS";
        case OpCode::OP_LESS_EQUAL: return "OP_LESS_EQUAL";

        case OpCode::OP_NOT: return "OP_NOT";
        case OpCode::OP_AND: return "OP_AND";
        case OpCode::OP_OR: return "OP_OR";

        case OpCode::OP_BIT_AND: return "OP_BIT_AND";
        case OpCode::OP_BIT_OR: return "OP_BIT_OR";
        case OpCode::OP_BIT_XOR: return "OP_BIT_XOR";
        case OpCode::OP_BIT_NOT: return "OP_BIT_NOT";
        case OpCode::OP_LEFT_SHIFT: return "OP_LEFT_SHIFT";
        case OpCode::OP_RIGHT_SHIFT: return "OP_RIGHT_SHIFT";
        case OpCode::OP_UNSIGNED_RIGHT_SHIFT: return "OP_UNSIGNED_RIGHT_SHIFT";

        case OpCode::OP_INCREMENT: return "OP_INCREMENT";
        case OpCode::OP_DECREMENT: return "OP_DECREMENT";
        case OpCode::OP_POST_INCREMENT: return "OP_POST_INCREMENT";
        case OpCode::OP_POST_DECREMENT: return "OP_POST_DECREMENT";

        case OpCode::OP_JUMP: return "OP_JUMP";
        case OpCode::OP_JUMP_IF_FALSE: return "OP_JUMP_IF_FALSE";
        case OpCode::OP_JUMP_IF_TRUE: return "OP_JUMP_IF_TRUE";
        case OpCode::OP_LOOP: return "OP_LOOP";

        case OpCode::OP_CALL: return "OP_CALL";
        case OpCode::OP_CLOSURE: return "OP_CLOSURE";
        case OpCode::OP_RETURN: return "OP_RETURN";

        case OpCode::OP_NEW_ARRAY: return "OP_NEW_ARRAY";
        case OpCode::OP_NEW_OBJECT: return "OP_NEW_OBJECT";
        case OpCode::OP_GET_PROPERTY: return "OP_GET_PROPERTY";
        case OpCode::OP_SET_PROPERTY: return "OP_SET_PROPERTY";
        case OpCode::OP_GET_INDEX: return "OP_GET_INDEX";
        case OpCode::OP_SET_INDEX: return "OP_SET_INDEX";

        case OpCode::OP_CLASS: return "OP_CLASS";
        case OpCode::OP_METHOD: return "OP_METHOD";
        case OpCode::OP_INHERIT: return "OP_INHERIT";
        case OpCode::OP_GET_SUPER: return "OP_GET_SUPER";
        case OpCode::OP_INVOKE: return "OP_INVOKE";
        case OpCode::OP_SUPER_INVOKE: return "OP_SUPER_INVOKE";

        case OpCode::OP_TYPEOF: return "OP_TYPEOF";
        case OpCode::OP_INSTANCEOF: return "OP_INSTANCEOF";
        case OpCode::OP_IN: return "OP_IN";
        case OpCode::OP_DELETE: return "OP_DELETE";

        case OpCode::OP_PRINT: return "OP_PRINT";
        case OpCode::OP_BREAKPOINT: return "OP_BREAKPOINT";

        default: return "OP_UNKNOWN";
    }
}

int getOpCodeOperandCount(OpCode op) {
    switch (op) {
        // No operands
        case OpCode::OP_TRUE:
        case OpCode::OP_FALSE:
        case OpCode::OP_NULL:
        case OpCode::OP_UNDEFINED:
        case OpCode::OP_NUMBER_0:
        case OpCode::OP_NUMBER_1:
        case OpCode::OP_NUMBER_NEG1:
        case OpCode::OP_POP:
        case OpCode::OP_DUP:
        case OpCode::OP_SWAP:
        case OpCode::OP_ADD:
        case OpCode::OP_SUBTRACT:
        case OpCode::OP_MULTIPLY:
        case OpCode::OP_DIVIDE:
        case OpCode::OP_MODULO:
        case OpCode::OP_POWER:
        case OpCode::OP_NEGATE:
        case OpCode::OP_EQUAL:
        case OpCode::OP_NOT_EQUAL:
        case OpCode::OP_STRICT_EQUAL:
        case OpCode::OP_STRICT_NOT_EQUAL:
        case OpCode::OP_GREATER:
        case OpCode::OP_GREATER_EQUAL:
        case OpCode::OP_LESS:
        case OpCode::OP_LESS_EQUAL:
        case OpCode::OP_NOT:
        case OpCode::OP_AND:
        case OpCode::OP_OR:
        case OpCode::OP_BIT_AND:
        case OpCode::OP_BIT_OR:
        case OpCode::OP_BIT_XOR:
        case OpCode::OP_BIT_NOT:
        case OpCode::OP_LEFT_SHIFT:
        case OpCode::OP_RIGHT_SHIFT:
        case OpCode::OP_UNSIGNED_RIGHT_SHIFT:
        case OpCode::OP_INCREMENT:
        case OpCode::OP_DECREMENT:
        case OpCode::OP_POST_INCREMENT:
        case OpCode::OP_POST_DECREMENT:
        case OpCode::OP_RETURN:
        case OpCode::OP_GET_INDEX:
        case OpCode::OP_SET_INDEX:
        case OpCode::OP_INHERIT:
        case OpCode::OP_TYPEOF:
        case OpCode::OP_INSTANCEOF:
        case OpCode::OP_IN:
        case OpCode::OP_DELETE:
        case OpCode::OP_PRINT:
        case OpCode::OP_BREAKPOINT:
        case OpCode::OP_CLOSE_UPVALUE:
            return 0;

        // 1 byte operand
        case OpCode::OP_GET_LOCAL:
        case OpCode::OP_SET_LOCAL:
        case OpCode::OP_GET_UPVALUE:
        case OpCode::OP_SET_UPVALUE:
        case OpCode::OP_CALL:
            return 1;

        // 2 byte operand
        case OpCode::OP_CONSTANT:
        case OpCode::OP_GET_GLOBAL:
        case OpCode::OP_SET_GLOBAL:
        case OpCode::OP_DEFINE_GLOBAL:
        case OpCode::OP_JUMP:
        case OpCode::OP_JUMP_IF_FALSE:
        case OpCode::OP_JUMP_IF_TRUE:
        case OpCode::OP_LOOP:
        case OpCode::OP_CLOSURE:
        case OpCode::OP_NEW_ARRAY:
        case OpCode::OP_NEW_OBJECT:
        case OpCode::OP_GET_PROPERTY:
        case OpCode::OP_SET_PROPERTY:
        case OpCode::OP_CLASS:
        case OpCode::OP_METHOD:
        case OpCode::OP_GET_SUPER:
            return 2;

        // 3 byte operand (2 byte index + 1 byte count)
        case OpCode::OP_INVOKE:
        case OpCode::OP_SUPER_INVOKE:
            return 3;

        // 4 byte operand
        case OpCode::OP_CONSTANT_LONG:
            return 4;

        // 8 byte operand (inline double)
        case OpCode::OP_NUMBER:
            return 8;

        default:
            return 0;
    }
}

} // namespace as3
