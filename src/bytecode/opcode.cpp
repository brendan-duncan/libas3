#include "opcode.h"

namespace as3 {

const char* getOpCodeName(OpCode op) {
    switch (op) {
        case OpCode::OP_CONSTANT: return "CONSTANT";
        case OpCode::OP_UNDEFINED: return "UNDEFINED";
        case OpCode::OP_NULL: return "NULL";
        case OpCode::OP_TRUE: return "TRUE";
        case OpCode::OP_FALSE: return "FALSE";
        case OpCode::OP_ZERO: return "ZERO";
        case OpCode::OP_ONE: return "ONE";
        case OpCode::OP_POP: return "POP";
        case OpCode::OP_DUP: return "DUP";
        case OpCode::OP_DUP2: return "DUP2";
        case OpCode::OP_SWAP: return "SWAP";
        case OpCode::OP_DUP_X1: return "DUP_X1";
        case OpCode::OP_DUP_X2: return "DUP_X2";
        case OpCode::OP_GET_LOCAL: return "GET_LOCAL";
        case OpCode::OP_SET_LOCAL: return "SET_LOCAL";
        case OpCode::OP_PUT_LOCAL: return "PUT_LOCAL";
        case OpCode::OP_GET_CELL: return "GET_CELL";
        case OpCode::OP_SET_CELL: return "SET_CELL";
        case OpCode::OP_PUT_CELL: return "PUT_CELL";
        case OpCode::OP_GET_UPVALUE: return "GET_UPVALUE";
        case OpCode::OP_SET_UPVALUE: return "SET_UPVALUE";
        case OpCode::OP_PUT_UPVALUE: return "PUT_UPVALUE";
        case OpCode::OP_INC_LOCAL: return "INC_LOCAL";
        case OpCode::OP_GET_NAME: return "GET_NAME";
        case OpCode::OP_SET_NAME: return "SET_NAME";
        case OpCode::OP_TYPEOF_NAME: return "TYPEOF_NAME";
        case OpCode::OP_DEFINE_VAR: return "DEFINE_VAR";
        case OpCode::OP_DEFINE_FUNCTION: return "DEFINE_FUNCTION";
        case OpCode::OP_GET_QNAME: return "GET_QNAME";
        case OpCode::OP_WITH_GET: return "WITH_GET";
        case OpCode::OP_WITH_SET: return "WITH_SET";
        case OpCode::OP_WITH_FIND: return "WITH_FIND";
        case OpCode::OP_GET_PROP: return "GET_PROP";
        case OpCode::OP_SET_PROP: return "SET_PROP";
        case OpCode::OP_DELETE_PROP: return "DELETE_PROP";
        case OpCode::OP_GET_INDEX: return "GET_INDEX";
        case OpCode::OP_SET_INDEX: return "SET_INDEX";
        case OpCode::OP_DELETE_INDEX: return "DELETE_INDEX";
        case OpCode::OP_GET_SUPER: return "GET_SUPER";
        case OpCode::OP_SET_SUPER: return "SET_SUPER";
        case OpCode::OP_GET_LOCAL_PROP: return "GET_LOCAL_PROP";
        case OpCode::OP_SET_LOCAL_PROP: return "SET_LOCAL_PROP";
        case OpCode::OP_GET_THIS_PROP: return "GET_THIS_PROP";
        case OpCode::OP_SET_THIS_PROP: return "SET_THIS_PROP";
        case OpCode::OP_CALL_THIS_PROP: return "CALL_THIS_PROP";
        case OpCode::OP_CALL_MATH: return "CALL_MATH";
        case OpCode::OP_CALL_LOCAL_PROP: return "CALL_LOCAL_PROP";
        case OpCode::OP_ADD: return "ADD";
        case OpCode::OP_SUBTRACT: return "SUBTRACT";
        case OpCode::OP_MULTIPLY: return "MULTIPLY";
        case OpCode::OP_DIVIDE: return "DIVIDE";
        case OpCode::OP_MODULO: return "MODULO";
        case OpCode::OP_NEGATE: return "NEGATE";
        case OpCode::OP_TO_NUMBER: return "TO_NUMBER";
        case OpCode::OP_INCREMENT: return "INCREMENT";
        case OpCode::OP_DECREMENT: return "DECREMENT";
        case OpCode::OP_EQUAL: return "EQUAL";
        case OpCode::OP_NOT_EQUAL: return "NOT_EQUAL";
        case OpCode::OP_STRICT_EQUAL: return "STRICT_EQUAL";
        case OpCode::OP_STRICT_NOT_EQUAL: return "STRICT_NOT_EQUAL";
        case OpCode::OP_GREATER: return "GREATER";
        case OpCode::OP_GREATER_EQUAL: return "GREATER_EQUAL";
        case OpCode::OP_LESS: return "LESS";
        case OpCode::OP_LESS_EQUAL: return "LESS_EQUAL";
        case OpCode::OP_NOT: return "NOT";
        case OpCode::OP_BIT_AND: return "BIT_AND";
        case OpCode::OP_BIT_OR: return "BIT_OR";
        case OpCode::OP_BIT_XOR: return "BIT_XOR";
        case OpCode::OP_BIT_NOT: return "BIT_NOT";
        case OpCode::OP_LEFT_SHIFT: return "LEFT_SHIFT";
        case OpCode::OP_RIGHT_SHIFT: return "RIGHT_SHIFT";
        case OpCode::OP_UNSIGNED_RIGHT_SHIFT: return "UNSIGNED_RIGHT_SHIFT";
        case OpCode::OP_TYPEOF: return "TYPEOF";
        case OpCode::OP_INSTANCEOF: return "INSTANCEOF";
        case OpCode::OP_IN: return "IN";
        case OpCode::OP_IS: return "IS";
        case OpCode::OP_AS: return "AS";
        case OpCode::OP_COERCE: return "COERCE";
        case OpCode::OP_JUMP: return "JUMP";
        case OpCode::OP_JUMP_IF_FALSE: return "JUMP_IF_FALSE";
        case OpCode::OP_JUMP_IF_TRUE: return "JUMP_IF_TRUE";
        case OpCode::OP_JUMP_IF_FALSE_KEEP: return "JUMP_IF_FALSE_KEEP";
        case OpCode::OP_JUMP_IF_TRUE_KEEP: return "JUMP_IF_TRUE_KEEP";
        case OpCode::OP_JUMP_IF_NOT_NULLISH_KEEP: return "JUMP_IF_NOT_NULLISH_KEEP";
        case OpCode::OP_CALL: return "CALL";
        case OpCode::OP_CALL_PROP: return "CALL_PROP";
        case OpCode::OP_CALL_NAME: return "CALL_NAME";
        case OpCode::OP_CALL_INDEX: return "CALL_INDEX";
        case OpCode::OP_CALL_SUPER: return "CALL_SUPER";
        case OpCode::OP_SUPER_CTOR: return "SUPER_CTOR";
        case OpCode::OP_NEW: return "NEW";
        case OpCode::OP_RETURN: return "RETURN";
        case OpCode::OP_RETURN_UNDEFINED: return "RETURN_UNDEFINED";
        case OpCode::OP_THIS: return "THIS";
        case OpCode::OP_SCOPE_THIS: return "SCOPE_THIS";
        case OpCode::OP_OWNER_CLASS: return "OWNER_CLASS";
        case OpCode::OP_CLOSURE: return "CLOSURE";
        case OpCode::OP_DEFAULT_ARG: return "DEFAULT_ARG";
        case OpCode::OP_NEW_ARRAY: return "NEW_ARRAY";
        case OpCode::OP_NEW_OBJECT: return "NEW_OBJECT";
        case OpCode::OP_NEW_VECTOR: return "NEW_VECTOR";
        case OpCode::OP_NEW_REGEXP: return "NEW_REGEXP";
        case OpCode::OP_APPLY_TYPE: return "APPLY_TYPE";
        case OpCode::OP_DEFINE_CLASS: return "DEFINE_CLASS";
        case OpCode::OP_THROW: return "THROW";
        case OpCode::OP_ITER_INIT: return "ITER_INIT";
        case OpCode::OP_ITER_NEXT: return "ITER_NEXT";
        case OpCode::OP_NOP: return "NOP";
        case OpCode::OP_GET_LOCAL_CONSTANT: return "GET_LOCAL_CONSTANT";
        case OpCode::OP_GET_LOCAL_LOCAL: return "GET_LOCAL_LOCAL";
        case OpCode::OP_COERCE_PUT_LOCAL: return "COERCE_PUT_LOCAL";
        case OpCode::OP_SET_LOCAL_PROP_POP: return "SET_LOCAL_PROP_POP";
        case OpCode::OP_SET_THIS_PROP_POP: return "SET_THIS_PROP_POP";
        case OpCode::OP_LESS_JUMP: return "LESS_JUMP";
        case OpCode::OP_GREATER_JUMP: return "GREATER_JUMP";
        case OpCode::OP_LESS_EQUAL_JUMP: return "LESS_EQUAL_JUMP";
        case OpCode::OP_GREATER_EQUAL_JUMP: return "GREATER_EQUAL_JUMP";
        case OpCode::OP_EQUAL_JUMP: return "EQUAL_JUMP";
        case OpCode::OP_NOT_EQUAL_JUMP: return "NOT_EQUAL_JUMP";
        case OpCode::OP_STRICT_EQUAL_JUMP: return "STRICT_EQUAL_JUMP";
        case OpCode::OP_STRICT_NOT_EQUAL_JUMP: return "STRICT_NOT_EQUAL_JUMP";
        case OpCode::OP_LOCAL_CONSTANT_COMPARE_JUMP: return "LOCAL_CONSTANT_COMPARE_JUMP";
        case OpCode::OP_LOCAL_LOCAL_COMPARE_JUMP: return "LOCAL_LOCAL_COMPARE_JUMP";
        case OpCode::OP_COUNT: break;
    }
    return "UNKNOWN";
}

int getOpCodeOperandCount(OpCode op) {
    switch (op) {
        case OpCode::OP_CONSTANT:
        case OpCode::OP_GET_LOCAL:
        case OpCode::OP_GET_LOCAL_CONSTANT:
        case OpCode::OP_GET_LOCAL_LOCAL:
        case OpCode::OP_LOCAL_CONSTANT_COMPARE_JUMP:
        case OpCode::OP_LOCAL_LOCAL_COMPARE_JUMP:
        case OpCode::OP_SET_LOCAL:
        case OpCode::OP_PUT_LOCAL:
        case OpCode::OP_GET_CELL:
        case OpCode::OP_SET_CELL:
        case OpCode::OP_PUT_CELL:
        case OpCode::OP_GET_UPVALUE:
        case OpCode::OP_SET_UPVALUE:
        case OpCode::OP_PUT_UPVALUE:
        case OpCode::OP_TYPEOF_NAME:
        case OpCode::OP_DEFINE_VAR:
        case OpCode::OP_DEFINE_FUNCTION:
        case OpCode::OP_GET_QNAME:
        case OpCode::OP_DELETE_PROP:
        case OpCode::OP_GET_SUPER:
        case OpCode::OP_SET_SUPER:
        case OpCode::OP_CLOSURE:
        case OpCode::OP_NEW_ARRAY:
        case OpCode::OP_NEW_OBJECT:
        case OpCode::OP_NEW_VECTOR:
        case OpCode::OP_DEFINE_CLASS:
            return 2;
        case OpCode::OP_INC_LOCAL:
        case OpCode::OP_GET_THIS_PROP:
        case OpCode::OP_SET_THIS_PROP:
        case OpCode::OP_SET_THIS_PROP_POP:
        case OpCode::OP_GET_NAME:
        case OpCode::OP_SET_NAME:
        case OpCode::OP_GET_PROP:
        case OpCode::OP_SET_PROP:
        case OpCode::OP_NEW_REGEXP:
            return 4;
        case OpCode::OP_COERCE:
        case OpCode::OP_COERCE_PUT_LOCAL:
        case OpCode::OP_CALL:
        case OpCode::OP_CALL_INDEX:
        case OpCode::OP_SUPER_CTOR:
        case OpCode::OP_NEW:
        case OpCode::OP_APPLY_TYPE:
        case OpCode::OP_ITER_INIT:
            return 1;
        case OpCode::OP_CALL_SUPER:
            return 3;
        case OpCode::OP_CALL_PROP:
        case OpCode::OP_CALL_NAME:
        case OpCode::OP_CALL_THIS_PROP:
            return 5;
        case OpCode::OP_GET_LOCAL_PROP:
        case OpCode::OP_SET_LOCAL_PROP:
        case OpCode::OP_SET_LOCAL_PROP_POP:
        case OpCode::OP_CALL_MATH:
            return 6;
        case OpCode::OP_CALL_LOCAL_PROP:
            return 7;
        case OpCode::OP_JUMP:
        case OpCode::OP_JUMP_IF_FALSE:
        case OpCode::OP_JUMP_IF_TRUE:
        case OpCode::OP_JUMP_IF_FALSE_KEEP:
        case OpCode::OP_JUMP_IF_TRUE_KEEP:
        case OpCode::OP_JUMP_IF_NOT_NULLISH_KEEP:
            return 4;
        case OpCode::OP_DEFAULT_ARG:
        case OpCode::OP_ITER_NEXT:
            return 6;
        default:
            return 0;
    }
}

} // namespace as3
