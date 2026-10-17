#include "abc_opcodes.h"

namespace as3 {
namespace abc {

namespace {

struct Table {
    OpInfo ops[256];

    void set(uint8_t op, const char* name, Operand a = Operand::None, Operand b = Operand::None) {
        ops[op].name = name;
        ops[op].operands[0] = a;
        ops[op].operands[1] = b;
    }

    Table() {
        using O = Operand;
        set(OP_bkpt, "bkpt");
        set(OP_nop, "nop");
        set(OP_throw, "throw");
        set(OP_getsuper, "getsuper", O::Multiname);
        set(OP_setsuper, "setsuper", O::Multiname);
        set(OP_dxns, "dxns", O::String);
        set(OP_dxnslate, "dxnslate");
        set(OP_kill, "kill", O::Register);
        set(OP_label, "label");
        set(OP_ifnlt, "ifnlt", O::Branch);
        set(OP_ifnle, "ifnle", O::Branch);
        set(OP_ifngt, "ifngt", O::Branch);
        set(OP_ifnge, "ifnge", O::Branch);
        set(OP_jump, "jump", O::Branch);
        set(OP_iftrue, "iftrue", O::Branch);
        set(OP_iffalse, "iffalse", O::Branch);
        set(OP_ifeq, "ifeq", O::Branch);
        set(OP_ifne, "ifne", O::Branch);
        set(OP_iflt, "iflt", O::Branch);
        set(OP_ifle, "ifle", O::Branch);
        set(OP_ifgt, "ifgt", O::Branch);
        set(OP_ifge, "ifge", O::Branch);
        set(OP_ifstricteq, "ifstricteq", O::Branch);
        set(OP_ifstrictne, "ifstrictne", O::Branch);
        set(OP_lookupswitch, "lookupswitch");
        set(OP_pushwith, "pushwith");
        set(OP_popscope, "popscope");
        set(OP_nextname, "nextname");
        set(OP_hasnext, "hasnext");
        set(OP_pushnull, "pushnull");
        set(OP_pushundefined, "pushundefined");
        set(OP_nextvalue, "nextvalue");
        set(OP_pushbyte, "pushbyte", O::SignedByte);
        set(OP_pushshort, "pushshort", O::Short);
        set(OP_pushtrue, "pushtrue");
        set(OP_pushfalse, "pushfalse");
        set(OP_pushnan, "pushnan");
        set(OP_pop, "pop");
        set(OP_dup, "dup");
        set(OP_swap, "swap");
        set(OP_pushstring, "pushstring", O::String);
        set(OP_pushint, "pushint", O::Int);
        set(OP_pushuint, "pushuint", O::UInt);
        set(OP_pushdouble, "pushdouble", O::Double);
        set(OP_pushscope, "pushscope");
        set(OP_pushnamespace, "pushnamespace", O::Namespace);
        set(OP_hasnext2, "hasnext2", O::Register, O::Register);
        set(OP_li8, "li8");
        set(OP_li16, "li16");
        set(OP_li32, "li32");
        set(OP_lf32, "lf32");
        set(OP_lf64, "lf64");
        set(OP_si8, "si8");
        set(OP_si16, "si16");
        set(OP_si32, "si32");
        set(OP_sf32, "sf32");
        set(OP_sf64, "sf64");
        set(OP_newfunction, "newfunction", O::Method);
        set(OP_call, "call", O::U30);
        set(OP_construct, "construct", O::U30);
        set(OP_callmethod, "callmethod", O::U30, O::U30);
        set(OP_callstatic, "callstatic", O::Method, O::U30);
        set(OP_callsuper, "callsuper", O::Multiname, O::U30);
        set(OP_callproperty, "callproperty", O::Multiname, O::U30);
        set(OP_returnvoid, "returnvoid");
        set(OP_returnvalue, "returnvalue");
        set(OP_constructsuper, "constructsuper", O::U30);
        set(OP_constructprop, "constructprop", O::Multiname, O::U30);
        set(OP_callproplex, "callproplex", O::Multiname, O::U30);
        set(OP_callsupervoid, "callsupervoid", O::Multiname, O::U30);
        set(OP_callpropvoid, "callpropvoid", O::Multiname, O::U30);
        set(OP_sxi1, "sxi1");
        set(OP_sxi8, "sxi8");
        set(OP_sxi16, "sxi16");
        set(OP_applytype, "applytype", O::U30);
        set(OP_newobject, "newobject", O::U30);
        set(OP_newarray, "newarray", O::U30);
        set(OP_newactivation, "newactivation");
        set(OP_newclass, "newclass", O::Class);
        set(OP_getdescendants, "getdescendants", O::Multiname);
        set(OP_newcatch, "newcatch", O::Exception);
        set(OP_findpropstrict, "findpropstrict", O::Multiname);
        set(OP_findproperty, "findproperty", O::Multiname);
        set(OP_finddef, "finddef", O::Multiname);
        set(OP_getlex, "getlex", O::Multiname);
        set(OP_setproperty, "setproperty", O::Multiname);
        set(OP_getlocal, "getlocal", O::Register);
        set(OP_setlocal, "setlocal", O::Register);
        set(OP_getglobalscope, "getglobalscope");
        set(OP_getscopeobject, "getscopeobject", O::Byte);
        set(OP_getproperty, "getproperty", O::Multiname);
        set(OP_getouterscope, "getouterscope", O::U30);
        set(OP_initproperty, "initproperty", O::Multiname);
        set(OP_deleteproperty, "deleteproperty", O::Multiname);
        set(OP_getslot, "getslot", O::U30);
        set(OP_setslot, "setslot", O::U30);
        set(OP_getglobalslot, "getglobalslot", O::U30);
        set(OP_setglobalslot, "setglobalslot", O::U30);
        set(OP_convert_s, "convert_s");
        set(OP_esc_xelem, "esc_xelem");
        set(OP_esc_xattr, "esc_xattr");
        set(OP_convert_i, "convert_i");
        set(OP_convert_u, "convert_u");
        set(OP_convert_d, "convert_d");
        set(OP_convert_b, "convert_b");
        set(OP_convert_o, "convert_o");
        set(OP_checkfilter, "checkfilter");
        set(OP_coerce, "coerce", O::Multiname);
        set(OP_coerce_b, "coerce_b");
        set(OP_coerce_a, "coerce_a");
        set(OP_coerce_i, "coerce_i");
        set(OP_coerce_d, "coerce_d");
        set(OP_coerce_s, "coerce_s");
        set(OP_astype, "astype", O::Multiname);
        set(OP_astypelate, "astypelate");
        set(OP_coerce_u, "coerce_u");
        set(OP_coerce_o, "coerce_o");
        set(OP_negate, "negate");
        set(OP_increment, "increment");
        set(OP_inclocal, "inclocal", O::Register);
        set(OP_decrement, "decrement");
        set(OP_declocal, "declocal", O::Register);
        set(OP_typeof, "typeof");
        set(OP_not, "not");
        set(OP_bitnot, "bitnot");
        set(OP_add, "add");
        set(OP_subtract, "subtract");
        set(OP_multiply, "multiply");
        set(OP_divide, "divide");
        set(OP_modulo, "modulo");
        set(OP_lshift, "lshift");
        set(OP_rshift, "rshift");
        set(OP_urshift, "urshift");
        set(OP_bitand, "bitand");
        set(OP_bitor, "bitor");
        set(OP_bitxor, "bitxor");
        set(OP_equals, "equals");
        set(OP_strictequals, "strictequals");
        set(OP_lessthan, "lessthan");
        set(OP_lessequals, "lessequals");
        set(OP_greaterthan, "greaterthan");
        set(OP_greaterequals, "greaterequals");
        set(OP_instanceof, "instanceof");
        set(OP_istype, "istype", O::Multiname);
        set(OP_istypelate, "istypelate");
        set(OP_in, "in");
        set(OP_increment_i, "increment_i");
        set(OP_decrement_i, "decrement_i");
        set(OP_inclocal_i, "inclocal_i", O::Register);
        set(OP_declocal_i, "declocal_i", O::Register);
        set(OP_negate_i, "negate_i");
        set(OP_add_i, "add_i");
        set(OP_subtract_i, "subtract_i");
        set(OP_multiply_i, "multiply_i");
        set(OP_getlocal0, "getlocal0");
        set(OP_getlocal1, "getlocal1");
        set(OP_getlocal2, "getlocal2");
        set(OP_getlocal3, "getlocal3");
        set(OP_setlocal0, "setlocal0");
        set(OP_setlocal1, "setlocal1");
        set(OP_setlocal2, "setlocal2");
        set(OP_setlocal3, "setlocal3");
        set(OP_debug, "debug");
        set(OP_debugline, "debugline", O::U30);
        set(OP_debugfile, "debugfile", O::String);
        set(OP_bkptline, "bkptline", O::U30);
        set(OP_timestamp, "timestamp");
    }
};

const Table& table() {
    static const Table instance;
    return instance;
}

struct Cursor {
    const std::vector<uint8_t>& code;
    size_t at;
    bool failed = false;

    uint8_t u8() {
        if (at >= code.size()) {
            failed = true;
            return 0;
        }
        return code[at++];
    }
    uint32_t u30() {
        uint32_t value = 0;
        for (int shift = 0; shift < 35; shift += 7) {
            const uint8_t byte = u8();
            if (failed) return 0;
            value |= static_cast<uint32_t>(byte & 0x7F) << shift;
            if (!(byte & 0x80)) return value;
        }
        return value;
    }
    int32_t s24() {
        if (at + 3 > code.size()) {
            failed = true;
            at = code.size();
            return 0;
        }
        const int32_t value = readS24(&code[at]);
        at += 3;
        return value;
    }
};

} // namespace

const OpInfo& opInfo(uint8_t op) {
    return table().ops[op];
}

bool isBranch(uint8_t op) {
    return (op >= OP_ifnlt && op <= OP_ifstrictne);
}

bool decode(const std::vector<uint8_t>& code, size_t offset, Instruction& out, std::string& error) {
    out = Instruction();
    out.offset = offset;
    Cursor in{code, offset};
    out.op = in.u8();
    const OpInfo& info = opInfo(out.op);
    if (!info.name) {
        error = "unknown opcode 0x" + std::string(1, "0123456789abcdef"[out.op >> 4]) +
                std::string(1, "0123456789abcdef"[out.op & 15]);
        return false;
    }
    if (out.op == OP_lookupswitch) {
        out.targets.push_back(in.s24());
        const uint32_t cases = in.u30();
        if (cases > code.size()) {
            error = "lookupswitch with " + std::to_string(cases) + " cases";
            return false;
        }
        for (uint32_t i = 0; i <= cases && !in.failed; ++i) {
            out.targets.push_back(in.s24());
        }
        out.operands[0] = cases;
    } else if (out.op == OP_debug) {
        out.debug[0] = in.u8();
        out.debug[1] = in.u30();
        out.debug[2] = in.u8();
        out.debug[3] = in.u30();
    } else {
        for (int i = 0; i < 2; ++i) {
            switch (info.operands[i]) {
                case Operand::None:
                    break;
                case Operand::Byte:
                case Operand::SignedByte:
                    out.operands[i] = in.u8();
                    break;
                case Operand::Branch: {
                    const int32_t delta = in.s24();
                    out.operands[i] = static_cast<uint32_t>(delta);
                    break;
                }
                default:
                    out.operands[i] = in.u30();
                    break;
            }
        }
    }
    if (in.failed) {
        error = std::string(info.name) + " runs past the end of the code";
        return false;
    }
    out.length = in.at - offset;
    if (isBranch(out.op) || out.op == OP_jump) {
        out.targets.push_back(static_cast<int32_t>(in.at) + static_cast<int32_t>(out.operands[0]));
    }
    return true;
}

bool decodeAll(const std::vector<uint8_t>& code, std::vector<Instruction>& out, std::string& error) {
    out.clear();
    for (size_t at = 0; at < code.size();) {
        Instruction instruction;
        if (!decode(code, at, instruction, error)) {
            error = "at " + std::to_string(at) + ": " + error;
            return false;
        }
        at += instruction.length;
        out.push_back(std::move(instruction));
    }
    return true;
}

StackEffect stackEffect(const AbcFile& file, const Instruction& in) {
    StackEffect effect;
    const auto runtime = [&file](uint32_t index) {
        return index < file.pool.multinames.size() ? file.pool.multinames[index].runtimeParts() : 0;
    };
    const auto set = [&effect](int pops, int pushes) {
        effect.pops = pops;
        effect.pushes = pushes;
    };
    const int argc = static_cast<int>(in.operands[1]);
    switch (in.op) {
        case OP_throw: set(1, 0); effect.ends = true; break;
        case OP_getsuper: set(1 + runtime(in.operands[0]), 1); break;
        case OP_setsuper: set(2 + runtime(in.operands[0]), 0); break;
        case OP_dxnslate: set(1, 0); break;
        case OP_ifnlt: case OP_ifnle: case OP_ifngt: case OP_ifnge:
        case OP_ifeq: case OP_ifne: case OP_iflt: case OP_ifle: case OP_ifgt: case OP_ifge:
        case OP_ifstricteq: case OP_ifstrictne:
            set(2, 0);
            break;
        case OP_iftrue: case OP_iffalse: set(1, 0); break;
        case OP_jump: effect.ends = true; break;
        case OP_lookupswitch: set(1, 0); effect.ends = true; break;
        case OP_pushwith: case OP_pushscope: set(1, 0); effect.scope = 1; break;
        case OP_popscope: effect.scope = -1; break;
        case OP_nextname: case OP_nextvalue: case OP_hasnext: set(2, 1); break;
        case OP_pushnull: case OP_pushundefined: case OP_pushbyte: case OP_pushshort: case OP_pushtrue:
        case OP_pushfalse: case OP_pushnan: case OP_pushstring: case OP_pushint: case OP_pushuint:
        case OP_pushdouble: case OP_pushnamespace: case OP_hasnext2:
            set(0, 1);
            break;
        case OP_pop: set(1, 0); break;
        case OP_dup: set(1, 2); break;
        case OP_swap: set(2, 2); break;
        case OP_li8: case OP_li16: case OP_li32: case OP_lf32: case OP_lf64:
        case OP_sxi1: case OP_sxi8: case OP_sxi16:
            set(1, 1);
            break;
        case OP_si8: case OP_si16: case OP_si32: case OP_sf32: case OP_sf64: set(2, 0); break;
        case OP_newfunction: set(0, 1); break;
        case OP_call: set(static_cast<int>(in.operands[0]) + 2, 1); break;
        case OP_construct: set(static_cast<int>(in.operands[0]) + 1, 1); break;
        case OP_callmethod: case OP_callstatic: set(argc + 1, 1); break;
        case OP_callsuper: case OP_callproperty: case OP_constructprop: case OP_callproplex:
            set(argc + 1 + runtime(in.operands[0]), 1);
            break;
        case OP_callsupervoid: case OP_callpropvoid:
            set(argc + 1 + runtime(in.operands[0]), 0);
            break;
        case OP_returnvoid: effect.ends = true; break;
        case OP_returnvalue: set(1, 0); effect.ends = true; break;
        case OP_constructsuper: set(static_cast<int>(in.operands[0]) + 1, 0); break;
        case OP_applytype: set(static_cast<int>(in.operands[0]) + 1, 1); break;
        case OP_newobject: set(2 * static_cast<int>(in.operands[0]), 1); break;
        case OP_newarray: set(static_cast<int>(in.operands[0]), 1); break;
        case OP_newactivation: set(0, 1); break;
        case OP_newclass: set(1, 1); break;
        case OP_getdescendants: set(1 + runtime(in.operands[0]), 1); break;
        case OP_newcatch: set(0, 1); break;
        case OP_findpropstrict: case OP_findproperty: set(runtime(in.operands[0]), 1); break;
        case OP_finddef: case OP_getlex: set(0, 1); break;
        case OP_setproperty: case OP_initproperty: set(2 + runtime(in.operands[0]), 0); break;
        case OP_getlocal: case OP_getlocal0: case OP_getlocal1: case OP_getlocal2: case OP_getlocal3:
        case OP_getglobalscope: case OP_getscopeobject: case OP_getouterscope: case OP_getglobalslot:
            set(0, 1);
            break;
        case OP_setlocal: case OP_setlocal0: case OP_setlocal1: case OP_setlocal2: case OP_setlocal3:
        case OP_setglobalslot:
            set(1, 0);
            break;
        case OP_getproperty: case OP_deleteproperty: set(1 + runtime(in.operands[0]), 1); break;
        case OP_getslot: set(1, 1); break;
        case OP_setslot: set(2, 0); break;
        case OP_convert_s: case OP_esc_xelem: case OP_esc_xattr: case OP_convert_i: case OP_convert_u:
        case OP_convert_d: case OP_convert_b: case OP_convert_o: case OP_checkfilter: case OP_coerce:
        case OP_coerce_b: case OP_coerce_a: case OP_coerce_i: case OP_coerce_d: case OP_coerce_s:
        case OP_astype: case OP_coerce_u: case OP_coerce_o: case OP_negate: case OP_increment:
        case OP_decrement: case OP_typeof: case OP_not: case OP_bitnot: case OP_istype:
        case OP_increment_i: case OP_decrement_i: case OP_negate_i:
            set(1, 1);
            break;
        case OP_astypelate: case OP_add: case OP_subtract: case OP_multiply: case OP_divide: case OP_modulo:
        case OP_lshift: case OP_rshift: case OP_urshift: case OP_bitand: case OP_bitor: case OP_bitxor:
        case OP_equals: case OP_strictequals: case OP_lessthan: case OP_lessequals: case OP_greaterthan:
        case OP_greaterequals: case OP_instanceof: case OP_istypelate: case OP_in: case OP_add_i:
        case OP_subtract_i: case OP_multiply_i:
            set(2, 1);
            break;
        default:
            // nop, bkpt, label, kill, dxns, inclocal and the like, debug.
            break;
    }
    return effect;
}

} // namespace abc
} // namespace as3
