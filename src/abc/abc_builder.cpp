#include "abc_builder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace as3 {
namespace abc {

// Pool

uint32_t PoolBuilder::integer(int32_t value) {
    auto found = ints.find(value);
    if (found != ints.end()) return found->second;
    file.pool.ints.push_back(value);
    return ints[value] = static_cast<uint32_t>(file.pool.ints.size() - 1);
}

uint32_t PoolBuilder::uinteger(uint32_t value) {
    auto found = uints.find(value);
    if (found != uints.end()) return found->second;
    file.pool.uints.push_back(value);
    return uints[value] = static_cast<uint32_t>(file.pool.uints.size() - 1);
}

uint32_t PoolBuilder::number(double value) {
    uint64_t bits;
    std::memcpy(&bits, &value, 8);
    auto found = doubles.find(bits);
    if (found != doubles.end()) return found->second;
    file.pool.doubles.push_back(value);
    return doubles[bits] = static_cast<uint32_t>(file.pool.doubles.size() - 1);
}

uint32_t PoolBuilder::string(const std::string& text) {
    auto found = strings.find(text);
    if (found != strings.end()) return found->second;
    file.pool.strings.push_back(text);
    return strings[text] = static_cast<uint32_t>(file.pool.strings.size() - 1);
}

uint32_t PoolBuilder::ns(uint8_t kind, const std::string& name) {
    const uint32_t text = string(name);
    auto found = namespaces.find({kind, text});
    if (found != namespaces.end()) return found->second;
    file.pool.namespaces.push_back({kind, text});
    return namespaces[{kind, text}] = static_cast<uint32_t>(file.pool.namespaces.size() - 1);
}

uint32_t PoolBuilder::privateNs(const std::string& name) {
    file.pool.namespaces.push_back({NS_Private, string(name)});
    return static_cast<uint32_t>(file.pool.namespaces.size() - 1);
}

uint32_t PoolBuilder::nsSet(std::vector<uint32_t> list) {
    // Each once, in the order first given.
    std::vector<uint32_t> unique;
    for (uint32_t ns : list) {
        if (ns && std::find(unique.begin(), unique.end(), ns) == unique.end()) unique.push_back(ns);
    }
    auto found = sets.find(unique);
    if (found != sets.end()) return found->second;
    file.pool.nsSets.push_back({unique});
    return sets[unique] = static_cast<uint32_t>(file.pool.nsSets.size() - 1);
}

uint32_t PoolBuilder::qname(uint32_t ns, const std::string& name, bool attribute) {
    const uint8_t kind = attribute ? MN_QNameA : MN_QName;
    const uint32_t text = string(name);
    const auto key = std::make_tuple(kind, ns, text, 0u);
    auto found = names.find(key);
    if (found != names.end()) return found->second;
    Multiname entry;
    entry.kind = kind;
    entry.ns = ns;
    entry.name = text;
    file.pool.multinames.push_back(entry);
    return names[key] = static_cast<uint32_t>(file.pool.multinames.size() - 1);
}

uint32_t PoolBuilder::multiname(const std::string& name, uint32_t set, bool attribute) {
    const uint8_t kind = attribute ? MN_MultinameA : MN_Multiname;
    const uint32_t text = name.empty() ? 0 : string(name);
    const auto key = std::make_tuple(kind, 0u, text, set);
    auto found = names.find(key);
    if (found != names.end()) return found->second;
    Multiname entry;
    entry.kind = kind;
    entry.name = text;
    entry.nsSet = set;
    file.pool.multinames.push_back(entry);
    return names[key] = static_cast<uint32_t>(file.pool.multinames.size() - 1);
}

uint32_t PoolBuilder::multinameL(uint32_t set, bool attribute) {
    const uint8_t kind = attribute ? MN_MultinameLA : MN_MultinameL;
    const auto key = std::make_tuple(kind, 0u, 0u, set);
    auto found = names.find(key);
    if (found != names.end()) return found->second;
    Multiname entry;
    entry.kind = kind;
    entry.nsSet = set;
    file.pool.multinames.push_back(entry);
    return names[key] = static_cast<uint32_t>(file.pool.multinames.size() - 1);
}

uint32_t PoolBuilder::typeName(uint32_t base, const std::vector<uint32_t>& parameters) {
    const auto key = std::make_pair(base, parameters);
    auto found = types.find(key);
    if (found != types.end()) return found->second;
    Multiname entry;
    entry.kind = MN_TypeName;
    entry.base = base;
    entry.parameters = parameters;
    file.pool.multinames.push_back(entry);
    return types[key] = static_cast<uint32_t>(file.pool.multinames.size() - 1);
}

// Code

void CodeBuilder::emitOperand(Operand kind, uint32_t value) {
    switch (kind) {
        case Operand::None:
            break;
        case Operand::Byte:
        case Operand::SignedByte:
            code.push_back(static_cast<uint8_t>(value));
            break;
        case Operand::Branch:
            writeS24(code, static_cast<int32_t>(value));
            break;
        default:
            writeU30(code, value);
            break;
    }
}

void CodeBuilder::track(const Instruction& instruction) {
    if (stack < 0) return;
    const StackEffect effect = stackEffect(file, instruction);
    if (stack < effect.pops && problem.empty()) {
        problem = std::string(opInfo(instruction.op).name) + " at " + std::to_string(instruction.offset) +
                  " takes more than the stack holds";
    }
    stack += effect.pushes - effect.pops;
    scope += effect.scope;
    maxStack = std::max(maxStack, stack);
    maxScope = std::max(maxScope, scope);
    if (effect.ends) {
        lastStack = stack;
        lastScope = scope;
        stack = -1;
        scope = -1;
    }
}

void CodeBuilder::op(uint8_t opcode) {
    Instruction in;
    in.offset = code.size();
    in.op = opcode;
    code.push_back(opcode);
    track(in);
}

void CodeBuilder::op(uint8_t opcode, uint32_t a) {
    Instruction in;
    in.offset = code.size();
    in.op = opcode;
    in.operands[0] = a;
    code.push_back(opcode);
    emitOperand(opInfo(opcode).operands[0], a);
    track(in);
}

void CodeBuilder::op(uint8_t opcode, uint32_t a, uint32_t b) {
    Instruction in;
    in.offset = code.size();
    in.op = opcode;
    in.operands[0] = a;
    in.operands[1] = b;
    code.push_back(opcode);
    emitOperand(opInfo(opcode).operands[0], a);
    emitOperand(opInfo(opcode).operands[1], b);
    track(in);
}

void CodeBuilder::getLocal(uint32_t reg) {
    if (reg < 4) op(static_cast<uint8_t>(OP_getlocal0 + reg));
    else op(OP_getlocal, reg);
}

void CodeBuilder::setLocal(uint32_t reg) {
    if (reg < 4) op(static_cast<uint8_t>(OP_setlocal0 + reg));
    else op(OP_setlocal, reg);
}

void CodeBuilder::pushInt(int32_t value, PoolBuilder& pool) {
    if (value >= -128 && value <= 127) op(OP_pushbyte, static_cast<uint32_t>(value) & 0xFF);
    else if (value >= 0 && value <= 32767) op(OP_pushshort, static_cast<uint32_t>(value));
    else op(OP_pushint, pool.integer(value));
}

void CodeBuilder::pushDouble(double value, PoolBuilder& pool) {
    if (std::isnan(value)) {
        op(OP_pushnan);
        return;
    }
    if (value == std::floor(value) && value >= -2147483648.0 && value <= 2147483647.0 &&
        !(value == 0 && std::signbit(value))) {
        pushInt(static_cast<int32_t>(value), pool);
        return;
    }
    if (value == std::floor(value) && value > 0 && value <= 4294967295.0) {
        op(OP_pushuint, pool.uinteger(static_cast<uint32_t>(value)));
        return;
    }
    op(OP_pushdouble, pool.number(value));
}

CodeBuilder::Label CodeBuilder::newLabel() {
    labels.emplace_back();
    return Label{static_cast<int>(labels.size() - 1)};
}

void CodeBuilder::meet(LabelState& label) {
    if (stack < 0) return;
    if (label.stack < 0) {
        label.stack = stack;
        label.scope = scope;
    } else if ((label.stack != stack || label.scope != scope) && problem.empty()) {
        problem = "paths meet at " + std::to_string(code.size()) + " with stacks " + std::to_string(label.stack) + " and " +
                  std::to_string(stack) + ", scopes " + std::to_string(label.scope) + " and " + std::to_string(scope);
    }
}

void CodeBuilder::bind(Label label, bool loopHead) {
    LabelState& state = labels[label.id];
    if (loopHead) {
        // The label instruction, reached as the code before it is.
        const int savedStack = stack;
        Instruction in;
        in.op = OP_label;
        code.push_back(OP_label);
        stack = savedStack;
    }
    state.offset = loopHead ? code.size() - 1 : code.size();
    if (stack >= 0) {
        meet(state);
    } else if (state.stack >= 0) {
        stack = state.stack;
        scope = state.scope;
    } else {
        stack = lastStack;
        scope = lastScope;
        state.stack = stack;
        state.scope = scope;
    }
    for (size_t at : state.fixups) {
        patchS24(code, at, static_cast<int32_t>(state.offset) - static_cast<int32_t>(at + 3));
    }
    state.fixups.clear();
}

void CodeBuilder::bindHandler(Label label) {
    LabelState& state = labels[label.id];
    state.offset = code.size();
    stack = 1;
    scope = 0;
    maxStack = std::max(maxStack, 1);
    state.stack = 1;
    state.scope = 0;
}

void CodeBuilder::jump(uint8_t opcode, Label label) {
    LabelState& state = labels[label.id];
    Instruction in;
    in.offset = code.size();
    in.op = opcode;
    code.push_back(opcode);
    const size_t at = code.size();
    writeS24(code, 0);
    // The stack as the branch leaves it, for the target.
    const StackEffect effect = stackEffect(file, in);
    if (stack >= 0) {
        if (stack < effect.pops && problem.empty()) problem = "a branch takes more than the stack holds";
        stack -= effect.pops;
        meet(state);
        stack += effect.pops;
    }
    if (state.offset != SIZE_MAX) {
        patchS24(code, at, static_cast<int32_t>(state.offset) - static_cast<int32_t>(at + 3));
    } else {
        state.fixups.push_back(at);
    }
    track(in);
}

size_t CodeBuilder::offsetOf(Label label) const {
    return labels[label.id].offset;
}

uint32_t CodeBuilder::newRegister() {
    return locals++;
}

uint32_t CodeBuilder::newTemp() {
    if (!freeTemps.empty()) {
        const uint32_t reg = freeTemps.back();
        freeTemps.pop_back();
        return reg;
    }
    return locals++;
}

void CodeBuilder::freeTemp(uint32_t reg) {
    op(OP_kill, reg);
    freeTemps.push_back(reg);
}

void CodeBuilder::reserveRegisters(uint32_t count) {
    locals = std::max(locals, count);
}

MethodBody CodeBuilder::finish(uint32_t method, uint32_t initScopeDepth) {
    for (const LabelState& label : labels) {
        if (!label.fixups.empty() && problem.empty()) problem = "a branch to a label never placed";
    }
    MethodBody body;
    body.method = method;
    body.maxStack = static_cast<uint32_t>(maxStack);
    body.localCount = locals;
    body.initScopeDepth = initScopeDepth;
    body.maxScopeDepth = initScopeDepth + static_cast<uint32_t>(maxScope);
    body.code = code;
    body.exceptions = exceptions;
    body.traits = activationTraits;
    return body;
}

} // namespace abc
} // namespace as3
