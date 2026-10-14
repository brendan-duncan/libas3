#include "chunk.h"
#include <iostream>
#include <iomanip>
#include <cstring>

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
    writeByte((value >> 8) & 0xFF);
    writeByte(value & 0xFF);
}

void Chunk::writeU32(uint32_t value) {
    writeByte((value >> 24) & 0xFF);
    writeByte((value >> 16) & 0xFF);
    writeByte((value >> 8) & 0xFF);
    writeByte(value & 0xFF);
}

void Chunk::writeS16(int16_t value) {
    writeU16(static_cast<uint16_t>(value));
}

void Chunk::writeF64(double value) {
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(double));
    for (int i = 7; i >= 0; i--) {
        writeByte((bits >> (i * 8)) & 0xFF);
    }
}

uint16_t Chunk::addConstant(ValuePtr value) {
    constants.push_back(value);
    if (constants.size() > UINT16_MAX) {
        throw std::runtime_error("Too many constants in one chunk");
    }
    return static_cast<uint16_t>(constants.size() - 1);
}

ValuePtr Chunk::getConstant(size_t index) const {
    if (index >= constants.size()) {
        return Value::makeUndefined();
    }
    return constants[index];
}

uint8_t Chunk::getByte(size_t offset) const {
    if (offset >= code.size()) return 0;
    return code[offset];
}

uint16_t Chunk::getU16(size_t offset) const {
    if (offset + 1 >= code.size()) return 0;
    return (static_cast<uint16_t>(code[offset]) << 8) |
           static_cast<uint16_t>(code[offset + 1]);
}

uint32_t Chunk::getU32(size_t offset) const {
    if (offset + 3 >= code.size()) return 0;
    return (static_cast<uint32_t>(code[offset]) << 24) |
           (static_cast<uint32_t>(code[offset + 1]) << 16) |
           (static_cast<uint32_t>(code[offset + 2]) << 8) |
           static_cast<uint32_t>(code[offset + 3]);
}

int16_t Chunk::getS16(size_t offset) const {
    return static_cast<int16_t>(getU16(offset));
}

double Chunk::getF64(size_t offset) const {
    if (offset + 7 >= code.size()) return 0.0;
    uint64_t bits = 0;
    for (int i = 0; i < 8; i++) {
        bits = (bits << 8) | code[offset + i];
    }
    double value;
    std::memcpy(&value, &bits, sizeof(double));
    return value;
}

int Chunk::getLine(size_t offset) const {
    if (offset >= lines.size()) return -1;
    return lines[offset];
}

void Chunk::patchJump(size_t offset, int16_t jump) {
    code[offset] = (jump >> 8) & 0xFF;
    code[offset + 1] = jump & 0xFF;
}

void Chunk::addLine(int line) {
    // An operand written with no line is on its instruction's.
    lines.push_back(line == 0 && !lines.empty() ? lines.back() : line);
}

// Disassembly

void Chunk::disassemble(const std::string& chunkName) const {
    std::cout << "== " << chunkName << " ==" << std::endl;

    for (size_t offset = 0; offset < code.size(); ) {
        offset = disassembleInstruction(offset);
    }
}

size_t Chunk::disassembleInstruction(size_t offset) const {
    std::cout << std::setw(4) << std::setfill('0') << offset << " ";

    // Print line number
    if (offset > 0 && getLine(offset) == getLine(offset - 1)) {
        std::cout << "   | ";
    } else {
        std::cout << std::setw(4) << getLine(offset) << " ";
    }

    uint8_t instruction = code[offset];
    OpCode op = static_cast<OpCode>(instruction);

    switch (op) {
        case OpCode::OP_CONSTANT: {
            uint16_t constant = getU16(offset + 1);
            std::cout << std::setw(16) << std::left << getOpCodeName(op);
            std::cout << std::setw(4) << constant << " '";
            std::cout << getConstant(constant)->toString() << "'";
            std::cout << std::endl;
            return offset + 3;
        }

        case OpCode::OP_NUMBER: {
            double value = getF64(offset + 1);
            std::cout << std::setw(16) << std::left << getOpCodeName(op);
            std::cout << value << std::endl;
            return offset + 9;
        }

        case OpCode::OP_GET_LOCAL:
        case OpCode::OP_SET_LOCAL:
        case OpCode::OP_GET_UPVALUE:
        case OpCode::OP_SET_UPVALUE:
        case OpCode::OP_CALL: {
            uint8_t slot = getByte(offset + 1);
            std::cout << std::setw(16) << std::left << getOpCodeName(op);
            std::cout << static_cast<int>(slot) << std::endl;
            return offset + 2;
        }

        case OpCode::OP_GET_GLOBAL:
        case OpCode::OP_SET_GLOBAL:
        case OpCode::OP_DEFINE_GLOBAL:
        case OpCode::OP_GET_PROPERTY:
        case OpCode::OP_SET_PROPERTY:
        case OpCode::OP_CLASS:
        case OpCode::OP_METHOD: {
            uint16_t constant = getU16(offset + 1);
            std::cout << std::setw(16) << std::left << getOpCodeName(op);
            std::cout << std::setw(4) << constant << " '";
            std::cout << getConstant(constant)->toString() << "'";
            std::cout << std::endl;
            return offset + 3;
        }

        case OpCode::OP_JUMP:
        case OpCode::OP_JUMP_IF_FALSE:
        case OpCode::OP_JUMP_IF_TRUE: {
            int16_t jump = getS16(offset + 1);
            std::cout << std::setw(16) << std::left << getOpCodeName(op);
            std::cout << std::setw(4) << offset << " -> " << (offset + 3 + jump);
            std::cout << std::endl;
            return offset + 3;
        }

        case OpCode::OP_LOOP: {
            uint16_t jump = getU16(offset + 1);
            std::cout << std::setw(16) << std::left << getOpCodeName(op);
            std::cout << std::setw(4) << offset << " -> " << (offset + 3 - jump);
            std::cout << std::endl;
            return offset + 3;
        }

        case OpCode::OP_NEW_ARRAY:
        case OpCode::OP_NEW_OBJECT: {
            uint16_t count = getU16(offset + 1);
            std::cout << std::setw(16) << std::left << getOpCodeName(op);
            std::cout << count << std::endl;
            return offset + 3;
        }

        case OpCode::OP_CLOSURE: {
            uint16_t constant = getU16(offset + 1);
            std::cout << std::setw(16) << std::left << getOpCodeName(op);
            std::cout << std::setw(4) << constant << " '";
            std::cout << getConstant(constant)->toString() << "'";
            std::cout << std::endl;
            // TODO: Print upvalue info
            return offset + 3;
        }

        case OpCode::OP_INVOKE:
        case OpCode::OP_SUPER_INVOKE: {
            uint16_t constant = getU16(offset + 1);
            uint8_t argCount = getByte(offset + 3);
            std::cout << std::setw(16) << std::left << getOpCodeName(op);
            std::cout << "(" << static_cast<int>(argCount) << " args) ";
            std::cout << constant << " '";
            std::cout << getConstant(constant)->toString() << "'";
            std::cout << std::endl;
            return offset + 4;
        }

        default: {
            std::cout << getOpCodeName(op) << std::endl;
            return offset + 1;
        }
    }
}

} // namespace as3
