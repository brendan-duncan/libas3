#pragma once

// Building an abcFile: constant pool entries made once and found again,
// and a method's code with labels, branches patched when their targets are
// placed, and the operand and scope stacks followed as instructions are
// added, for max_stack and max_scope_depth.

#include "abc_file.h"
#include "abc_opcodes.h"

#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace as3 {
namespace abc {

class PoolBuilder {
public:
    explicit PoolBuilder(AbcFile& file) : file(file) {}

    uint32_t integer(int32_t value);
    uint32_t uinteger(uint32_t value);
    uint32_t number(double value);
    uint32_t string(const std::string& text);
    uint32_t ns(uint8_t kind, const std::string& name);
    // A private namespace is its entry: each call makes a new one.
    uint32_t privateNs(const std::string& name);
    uint32_t nsSet(std::vector<uint32_t> namespaces);
    uint32_t qname(uint32_t ns, const std::string& name, bool attribute = false);
    // A name looked for in each of a set's namespaces; an empty name is
    // any name (x.*).
    uint32_t multiname(const std::string& name, uint32_t set, bool attribute = false);
    uint32_t multinameL(uint32_t set, bool attribute = false);
    uint32_t typeName(uint32_t base, const std::vector<uint32_t>& parameters);

    AbcFile& file;

private:
    std::map<int32_t, uint32_t> ints;
    std::map<uint32_t, uint32_t> uints;
    std::map<uint64_t, uint32_t> doubles;
    std::map<std::string, uint32_t> strings;
    std::map<std::pair<uint8_t, uint32_t>, uint32_t> namespaces;
    std::map<std::vector<uint32_t>, uint32_t> sets;
    std::map<std::tuple<uint8_t, uint32_t, uint32_t, uint32_t>, uint32_t> names;
    std::map<std::pair<uint32_t, std::vector<uint32_t>>, uint32_t> types;
};

class CodeBuilder {
public:
    explicit CodeBuilder(AbcFile& file) : file(file) {}

    struct Label {
        int id = -1;
    };

    // Instructions. The operand counts and kinds are as the opcode table
    // has them; branches go through jump().
    void op(uint8_t opcode);
    void op(uint8_t opcode, uint32_t a);
    void op(uint8_t opcode, uint32_t a, uint32_t b);

    // Commonly used forms, shortest encoding chosen.
    void getLocal(uint32_t reg);
    void setLocal(uint32_t reg);
    void pushInt(int32_t value, PoolBuilder& pool);
    void pushDouble(double value, PoolBuilder& pool);

    Label newLabel();
    // A label placed here; with loopHead, a label instruction first (a
    // backward branch's target must be one).
    void bind(Label label, bool loopHead = false);
    // Where an exception handler starts: the stack holds the exception
    // and the scope stack is empty.
    void bindHandler(Label label);
    void jump(uint8_t opcode, Label label);
    size_t offsetOf(Label label) const;

    size_t here() const { return code.size(); }
    bool reachable() const { return stack >= 0; }
    int stackDepth() const { return stack; }
    int scopeDepth() const { return scope; }

    // Registers: parameters first (set before code is made), then locals
    // and temporaries.
    uint32_t newRegister();
    uint32_t newTemp();
    void freeTemp(uint32_t reg);
    void reserveRegisters(uint32_t count);
    uint32_t registerCount() const { return locals; }

    // What went wrong in the builder itself (paths meeting with different
    // stacks); empty when nothing did.
    const std::string& error() const { return problem; }

    // The finished body, with its exceptions as they were given.
    MethodBody finish(uint32_t method, uint32_t initScopeDepth);

    std::vector<ExceptionInfo> exceptions;
    std::vector<Trait> activationTraits;

private:
    AbcFile& file;
    std::vector<uint8_t> code;
    struct LabelState {
        size_t offset = SIZE_MAX;
        int stack = -1;
        int scope = -1;
        std::vector<size_t> fixups;   // where an s24 is to be patched
    };
    std::vector<LabelState> labels;
    int stack = 0;
    int scope = 0;
    int maxStack = 0;
    int maxScope = 0;
    // The state before code became unreachable, for a label placed there
    // that nothing has branched to yet (a loop's body, reached only from
    // its test below).
    int lastStack = 0;
    int lastScope = 0;
    uint32_t locals = 1;
    std::vector<uint32_t> freeTemps;
    std::string problem;

    void track(const Instruction& instruction);
    void emitOperand(Operand kind, uint32_t value);
    void meet(LabelState& label);
};

} // namespace abc
} // namespace as3
