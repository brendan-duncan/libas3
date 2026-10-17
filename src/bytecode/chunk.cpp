#include "chunk.h"
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <iomanip>

namespace as3 {

Chunk::Chunk() {
}

void Chunk::writeByte(uint8_t byte, int line) {
    code.push_back(byte);
    addLine(line);
}

void Chunk::writeOpCode(OpCode op, int line) {
    writeByte(static_cast<uint8_t>(op), line);
}

void Chunk::writeU16(uint16_t value) {
    const int line = lines.empty() ? 0 : lines.back();
    writeByte(static_cast<uint8_t>(value & 0xFF), line);
    writeByte(static_cast<uint8_t>((value >> 8) & 0xFF), line);
}

void Chunk::writeU32(uint32_t value) {
    const int line = lines.empty() ? 0 : lines.back();
    for (int i = 0; i < 4; ++i) {
        writeByte(static_cast<uint8_t>((value >> (8 * i)) & 0xFF), line);
    }
}

void Chunk::writeS32(int32_t value) {
    writeU32(static_cast<uint32_t>(value));
}

uint16_t Chunk::addConstant(const Val& value) {
    // The same number or string once.
    for (size_t i = 0; i < constants.size(); ++i) {
        const Val& known = constants[i];
        if (known.tag == value.tag &&
            ((value.isNumber() && known.num == value.num && std::signbit(known.num) == std::signbit(value.num)) ||
             (value.isString() && known.str() == value.str()))) {
            return static_cast<uint16_t>(i);
        }
    }
    constants.push_back(value);
    return static_cast<uint16_t>(constants.size() - 1);
}

uint16_t Chunk::addName(const std::string& text) {
    auto found = nameIndex.find(text);
    if (found != nameIndex.end()) {
        return found->second;
    }
    names.push_back(text);
    const uint16_t index = static_cast<uint16_t>(names.size() - 1);
    nameIndex[text] = index;
    return index;
}

uint8_t Chunk::getByte(size_t offset) const {
    return code[offset];
}

uint16_t Chunk::getU16(size_t offset) const {
    return static_cast<uint16_t>(code[offset] | (code[offset + 1] << 8));
}

uint32_t Chunk::getU32(size_t offset) const {
    return static_cast<uint32_t>(code[offset]) | (static_cast<uint32_t>(code[offset + 1]) << 8) |
           (static_cast<uint32_t>(code[offset + 2]) << 16) | (static_cast<uint32_t>(code[offset + 3]) << 24);
}

int32_t Chunk::getS32(size_t offset) const {
    return static_cast<int32_t>(getU32(offset));
}

int Chunk::getLine(size_t offset) const {
    if (offset < lines.size()) {
        return lines[offset];
    }
    return lines.empty() ? 0 : lines.back();
}

void Chunk::patchS32(size_t offset, int32_t value) {
    const uint32_t bits = static_cast<uint32_t>(value);
    for (int i = 0; i < 4; ++i) {
        code[offset + i] = static_cast<uint8_t>((bits >> (8 * i)) & 0xFF);
    }
}

void Chunk::addLine(int line) {
    lines.push_back(line);
}

size_t Chunk::instructionLength(size_t offset) const {
    const OpCode op = static_cast<OpCode>(code[offset]);
    if (op == OpCode::OP_WITH_GET || op == OpCode::OP_WITH_SET || op == OpCode::OP_WITH_FIND) {
        // A name, the locals of the with objects, and a jump.
        return 1 + 2 + 1 + 2 * static_cast<size_t>(code[offset + 3]) + 4;
    }
    return 1 + static_cast<size_t>(getOpCodeOperandCount(op));
}

namespace {

bool isComparison(OpCode op) {
    switch (op) {
        case OpCode::OP_LESS:
        case OpCode::OP_GREATER:
        case OpCode::OP_LESS_EQUAL:
        case OpCode::OP_GREATER_EQUAL:
        case OpCode::OP_EQUAL:
        case OpCode::OP_NOT_EQUAL:
        case OpCode::OP_STRICT_EQUAL:
        case OpCode::OP_STRICT_NOT_EQUAL:
            return true;
        default:
            return false;
    }
}

} // namespace

void Chunk::fuse() {
    for (size_t at = 0; at < code.size();) {
        // The VM dispatches on the opcode without a range check: every
        // one is checked here, once.
        if (code[at] >= static_cast<uint8_t>(OpCode::OP_COUNT)) {
            throw std::logic_error("libas3: unknown opcode in " + name);
        }
        const size_t next = at + instructionLength(at);
        if (next >= code.size()) {
            break;
        }
        const OpCode op = static_cast<OpCode>(code[at]);
        const OpCode then = static_cast<OpCode>(code[next]);
        const bool jumps = then == OpCode::OP_JUMP_IF_TRUE || then == OpCode::OP_JUMP_IF_FALSE;
        OpCode fused = op;
        switch (op) {
            case OpCode::OP_GET_LOCAL: {
                if (then == OpCode::OP_CONSTANT) fused = OpCode::OP_GET_LOCAL_CONSTANT;
                if (then == OpCode::OP_GET_LOCAL) fused = OpCode::OP_GET_LOCAL_LOCAL;
                // A comparison and its jump after them: a loop's test. The
                // comparison is fused with its jump when this walk reaches it.
                const size_t third = next + instructionLength(next);
                if (fused != op && third + 1 < code.size() && isComparison(static_cast<OpCode>(code[third])) &&
                    (code[third + 1] == static_cast<uint8_t>(OpCode::OP_JUMP_IF_TRUE) ||
                     code[third + 1] == static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE))) {
                    fused = then == OpCode::OP_CONSTANT ? OpCode::OP_LOCAL_CONSTANT_COMPARE_JUMP
                                                        : OpCode::OP_LOCAL_LOCAL_COMPARE_JUMP;
                }
                break;
            }
            case OpCode::OP_COERCE:
                if (then == OpCode::OP_PUT_LOCAL) fused = OpCode::OP_COERCE_PUT_LOCAL;
                break;
            case OpCode::OP_SET_LOCAL_PROP:
                if (then == OpCode::OP_POP) fused = OpCode::OP_SET_LOCAL_PROP_POP;
                break;
            case OpCode::OP_SET_THIS_PROP:
                if (then == OpCode::OP_POP) fused = OpCode::OP_SET_THIS_PROP_POP;
                break;
            case OpCode::OP_LESS:
                if (jumps) fused = OpCode::OP_LESS_JUMP;
                break;
            case OpCode::OP_GREATER:
                if (jumps) fused = OpCode::OP_GREATER_JUMP;
                break;
            case OpCode::OP_LESS_EQUAL:
                if (jumps) fused = OpCode::OP_LESS_EQUAL_JUMP;
                break;
            case OpCode::OP_GREATER_EQUAL:
                if (jumps) fused = OpCode::OP_GREATER_EQUAL_JUMP;
                break;
            case OpCode::OP_EQUAL:
                if (jumps) fused = OpCode::OP_EQUAL_JUMP;
                break;
            case OpCode::OP_NOT_EQUAL:
                if (jumps) fused = OpCode::OP_NOT_EQUAL_JUMP;
                break;
            case OpCode::OP_STRICT_EQUAL:
                if (jumps) fused = OpCode::OP_STRICT_EQUAL_JUMP;
                break;
            case OpCode::OP_STRICT_NOT_EQUAL:
                if (jumps) fused = OpCode::OP_STRICT_NOT_EQUAL_JUMP;
                break;
            default:
                break;
        }
        code[at] = static_cast<uint8_t>(fused);
        at = next;
    }
}

void BytecodeFunction::finish() {
    chunk.fuse();
    localDefaults.clear();
    for (const Coerce coerce : localInit) {
        localDefaults.push_back(defaultForCoerce(coerce));
    }
}

void Chunk::disassemble(const std::string& title) const {
    std::cout << "== " << title << " ==" << std::endl;
    for (size_t offset = 0; offset < code.size();) {
        offset = disassembleInstruction(offset);
    }
}

size_t Chunk::disassembleInstruction(size_t offset) const {
    std::cout << std::setw(4) << std::setfill('0') << offset << std::setfill(' ') << " ";
    if (offset > 0 && getLine(offset) == getLine(offset - 1)) {
        std::cout << "   | ";
    } else {
        std::cout << std::setw(4) << getLine(offset) << " ";
    }
    const OpCode op = static_cast<OpCode>(code[offset]);
    std::cout << std::left << std::setw(28) << getOpCodeName(op) << std::right;
    int operands = getOpCodeOperandCount(op);
    if (op == OpCode::OP_WITH_GET || op == OpCode::OP_WITH_SET || op == OpCode::OP_WITH_FIND) {
        // A name, the locals of the with objects, and a jump.
        const int count = code[offset + 3];
        operands = 2 + 1 + 2 * count + 4;
        std::cout << "'" << names[getU16(offset + 1)] << "' in " << count << " -> "
                  << (offset + 1 + operands + getS32(offset + 1 + operands - 4)) << std::endl;
        return offset + 1 + static_cast<size_t>(operands);
    }
    switch (op) {
        case OpCode::OP_CONSTANT:
            std::cout << getU16(offset + 1) << " '" << constants[getU16(offset + 1)].toString() << "'";
            break;
        case OpCode::OP_GET_NAME:
        case OpCode::OP_SET_NAME:
        case OpCode::OP_GET_PROP:
        case OpCode::OP_SET_PROP:
        case OpCode::OP_CALL_PROP:
        case OpCode::OP_CALL_NAME:
        case OpCode::OP_CALL_SUPER:
        case OpCode::OP_TYPEOF_NAME:
        case OpCode::OP_DEFINE_VAR:
        case OpCode::OP_DEFINE_FUNCTION:
        case OpCode::OP_GET_QNAME:
        case OpCode::OP_DELETE_PROP:
        case OpCode::OP_GET_SUPER:
        case OpCode::OP_SET_SUPER:
        case OpCode::OP_GET_THIS_PROP:
        case OpCode::OP_SET_THIS_PROP:
        case OpCode::OP_SET_THIS_PROP_POP:
        case OpCode::OP_CALL_THIS_PROP:
            std::cout << "'" << names[getU16(offset + 1)] << "'";
            if (op == OpCode::OP_CALL_PROP || op == OpCode::OP_CALL_NAME || op == OpCode::OP_CALL_SUPER ||
                op == OpCode::OP_CALL_THIS_PROP) {
                std::cout << " (" << static_cast<int>(code[offset + 3]) << " args)";
            }
            break;
        case OpCode::OP_JUMP:
        case OpCode::OP_JUMP_IF_FALSE:
        case OpCode::OP_JUMP_IF_TRUE:
        case OpCode::OP_JUMP_IF_FALSE_KEEP:
        case OpCode::OP_JUMP_IF_TRUE_KEEP:
        case OpCode::OP_JUMP_IF_NOT_NULLISH_KEEP:
            std::cout << "-> " << (offset + 5 + getS32(offset + 1));
            break;
        case OpCode::OP_GET_LOCAL_PROP:
        case OpCode::OP_SET_LOCAL_PROP:
        case OpCode::OP_SET_LOCAL_PROP_POP:
        case OpCode::OP_CALL_LOCAL_PROP:
            std::cout << "local " << getU16(offset + 1) << " '" << names[getU16(offset + 3)] << "'";
            break;
        default:
            if (operands == 1) {
                std::cout << static_cast<int>(code[offset + 1]);
            } else if (operands >= 2) {
                std::cout << getU16(offset + 1);
            }
            break;
    }
    std::cout << std::endl;
    return offset + 1 + static_cast<size_t>(operands);
}

} // namespace as3
