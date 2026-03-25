#pragma once

#include "opcode.h"
#include "../runtime/value.h"
#include <vector>
#include <memory>

namespace as3 {

// A chunk of bytecode with associated constant pool and debug info
class Chunk {
public:
    Chunk();

    // Bytecode emission
    void writeByte(uint8_t byte, int line = 0);
    void writeOpCode(OpCode op, int line = 0);
    void writeU16(uint16_t value);
    void writeU32(uint32_t value);
    void writeS16(int16_t value);
    void writeF64(double value);

    // Constant pool management
    uint16_t addConstant(ValuePtr value);
    ValuePtr getConstant(size_t index) const;

    // Code access
    const std::vector<uint8_t>& getCode() const { return code; }
    uint8_t getByte(size_t offset) const;
    uint16_t getU16(size_t offset) const;
    uint32_t getU32(size_t offset) const;
    int16_t getS16(size_t offset) const;
    double getF64(size_t offset) const;

    // Debug info
    int getLine(size_t offset) const;
    void setName(const std::string& n) { name = n; }
    const std::string& getName() const { return name; }

    // Patching (for jumps)
    void patchJump(size_t offset, int16_t jump);
    size_t getCurrentOffset() const { return code.size(); }

    // Disassembly
    void disassemble(const std::string& name) const;
    size_t disassembleInstruction(size_t offset) const;

private:
    std::vector<uint8_t> code;            // Bytecode
    std::vector<ValuePtr> constants;       // Constant pool
    std::vector<int> lines;                // Line numbers for debugging
    std::string name;                      // Function/chunk name

    void addLine(int line);
};

// Function object for bytecode functions
class BytecodeFunction {
public:
    std::string name;
    int arity;                              // Number of parameters
    Chunk chunk;                            // Function bytecode
    std::vector<bool> upvalueIsLocal;       // Is upvalue a local variable?
    std::vector<uint8_t> upvalueIndex;      // Index of upvalue

    BytecodeFunction(const std::string& n, int a)
        : name(n), arity(a) {}
};

} // namespace as3
