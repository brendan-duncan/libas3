#pragma once

// AVM2's instructions: their opcodes, operands and what they do to the
// operand and scope stacks, shared by the assembler and the verifier.

#include "abc_file.h"

#include <cstdint>
#include <string>
#include <vector>

namespace as3 {
namespace abc {

enum Op : uint8_t {
    OP_bkpt = 0x01,
    OP_nop = 0x02,
    OP_throw = 0x03,
    OP_getsuper = 0x04,
    OP_setsuper = 0x05,
    OP_dxns = 0x06,
    OP_dxnslate = 0x07,
    OP_kill = 0x08,
    OP_label = 0x09,
    OP_ifnlt = 0x0C,
    OP_ifnle = 0x0D,
    OP_ifngt = 0x0E,
    OP_ifnge = 0x0F,
    OP_jump = 0x10,
    OP_iftrue = 0x11,
    OP_iffalse = 0x12,
    OP_ifeq = 0x13,
    OP_ifne = 0x14,
    OP_iflt = 0x15,
    OP_ifle = 0x16,
    OP_ifgt = 0x17,
    OP_ifge = 0x18,
    OP_ifstricteq = 0x19,
    OP_ifstrictne = 0x1A,
    OP_lookupswitch = 0x1B,
    OP_pushwith = 0x1C,
    OP_popscope = 0x1D,
    OP_nextname = 0x1E,
    OP_hasnext = 0x1F,
    OP_pushnull = 0x20,
    OP_pushundefined = 0x21,
    OP_nextvalue = 0x23,
    OP_pushbyte = 0x24,
    OP_pushshort = 0x25,
    OP_pushtrue = 0x26,
    OP_pushfalse = 0x27,
    OP_pushnan = 0x28,
    OP_pop = 0x29,
    OP_dup = 0x2A,
    OP_swap = 0x2B,
    OP_pushstring = 0x2C,
    OP_pushint = 0x2D,
    OP_pushuint = 0x2E,
    OP_pushdouble = 0x2F,
    OP_pushscope = 0x30,
    OP_pushnamespace = 0x31,
    OP_hasnext2 = 0x32,
    OP_li8 = 0x35,
    OP_li16 = 0x36,
    OP_li32 = 0x37,
    OP_lf32 = 0x38,
    OP_lf64 = 0x39,
    OP_si8 = 0x3A,
    OP_si16 = 0x3B,
    OP_si32 = 0x3C,
    OP_sf32 = 0x3D,
    OP_sf64 = 0x3E,
    OP_newfunction = 0x40,
    OP_call = 0x41,
    OP_construct = 0x42,
    OP_callmethod = 0x43,
    OP_callstatic = 0x44,
    OP_callsuper = 0x45,
    OP_callproperty = 0x46,
    OP_returnvoid = 0x47,
    OP_returnvalue = 0x48,
    OP_constructsuper = 0x49,
    OP_constructprop = 0x4A,
    OP_callproplex = 0x4C,
    OP_callsupervoid = 0x4E,
    OP_callpropvoid = 0x4F,
    OP_sxi1 = 0x50,
    OP_sxi8 = 0x51,
    OP_sxi16 = 0x52,
    OP_applytype = 0x53,
    OP_newobject = 0x55,
    OP_newarray = 0x56,
    OP_newactivation = 0x57,
    OP_newclass = 0x58,
    OP_getdescendants = 0x59,
    OP_newcatch = 0x5A,
    OP_findpropstrict = 0x5D,
    OP_findproperty = 0x5E,
    OP_finddef = 0x5F,
    OP_getlex = 0x60,
    OP_setproperty = 0x61,
    OP_getlocal = 0x62,
    OP_setlocal = 0x63,
    OP_getglobalscope = 0x64,
    OP_getscopeobject = 0x65,
    OP_getproperty = 0x66,
    OP_getouterscope = 0x67,
    OP_initproperty = 0x68,
    OP_deleteproperty = 0x6A,
    OP_getslot = 0x6C,
    OP_setslot = 0x6D,
    OP_getglobalslot = 0x6E,
    OP_setglobalslot = 0x6F,
    OP_convert_s = 0x70,
    OP_esc_xelem = 0x71,
    OP_esc_xattr = 0x72,
    OP_convert_i = 0x73,
    OP_convert_u = 0x74,
    OP_convert_d = 0x75,
    OP_convert_b = 0x76,
    OP_convert_o = 0x77,
    OP_checkfilter = 0x78,
    OP_coerce = 0x80,
    OP_coerce_b = 0x81,
    OP_coerce_a = 0x82,
    OP_coerce_i = 0x83,
    OP_coerce_d = 0x84,
    OP_coerce_s = 0x85,
    OP_astype = 0x86,
    OP_astypelate = 0x87,
    OP_coerce_u = 0x88,
    OP_coerce_o = 0x89,
    OP_negate = 0x90,
    OP_increment = 0x91,
    OP_inclocal = 0x92,
    OP_decrement = 0x93,
    OP_declocal = 0x94,
    OP_typeof = 0x95,
    OP_not = 0x96,
    OP_bitnot = 0x97,
    OP_add = 0xA0,
    OP_subtract = 0xA1,
    OP_multiply = 0xA2,
    OP_divide = 0xA3,
    OP_modulo = 0xA4,
    OP_lshift = 0xA5,
    OP_rshift = 0xA6,
    OP_urshift = 0xA7,
    OP_bitand = 0xA8,
    OP_bitor = 0xA9,
    OP_bitxor = 0xAA,
    OP_equals = 0xAB,
    OP_strictequals = 0xAC,
    OP_lessthan = 0xAD,
    OP_lessequals = 0xAE,
    OP_greaterthan = 0xAF,
    OP_greaterequals = 0xB0,
    OP_instanceof = 0xB1,
    OP_istype = 0xB2,
    OP_istypelate = 0xB3,
    OP_in = 0xB4,
    OP_increment_i = 0xC0,
    OP_decrement_i = 0xC1,
    OP_inclocal_i = 0xC2,
    OP_declocal_i = 0xC3,
    OP_negate_i = 0xC4,
    OP_add_i = 0xC5,
    OP_subtract_i = 0xC6,
    OP_multiply_i = 0xC7,
    OP_getlocal0 = 0xD0,
    OP_getlocal1 = 0xD1,
    OP_getlocal2 = 0xD2,
    OP_getlocal3 = 0xD3,
    OP_setlocal0 = 0xD4,
    OP_setlocal1 = 0xD5,
    OP_setlocal2 = 0xD6,
    OP_setlocal3 = 0xD7,
    OP_debug = 0xEF,
    OP_debugline = 0xF0,
    OP_debugfile = 0xF1,
    OP_bkptline = 0xF2,
    OP_timestamp = 0xF3,
};

// What an operand is.
enum class Operand : uint8_t {
    None,
    U30,          // a plain count (argc) or number
    Multiname,
    String,
    Int,
    UInt,
    Double,
    Namespace,
    Method,
    Class,
    Exception,    // an index into the body's exception table
    Register,
    Byte,         // u8
    SignedByte,   // pushbyte's
    Short,        // pushshort's u30, read as a signed 16 bits
    Branch,       // s24
};

struct OpInfo {
    const char* name = nullptr;   // null for an opcode that is not one
    Operand operands[2] = {Operand::None, Operand::None};
};

const OpInfo& opInfo(uint8_t op);

// An instruction as decoded.
struct Instruction {
    size_t offset = 0;
    size_t length = 0;
    uint8_t op = 0;
    uint32_t operands[2] = {0, 0};
    // lookupswitch: the default first, then the cases; offsets relative to
    // the instruction's start. A branch: its target, absolute.
    std::vector<int32_t> targets;
    // debug: its four operands.
    uint32_t debug[4] = {0, 0, 0, 0};
};

// Decodes the instruction at offset; false (with why) when it is not one
// or runs past the end.
bool decode(const std::vector<uint8_t>& code, size_t offset, Instruction& out, std::string& error);

// A whole body's code, in order; false (with why, and the instructions
// before it) at the first that does not decode.
bool decodeAll(const std::vector<uint8_t>& code, std::vector<Instruction>& out, std::string& error);

// What an instruction does to the stacks. Multinames with runtime parts
// take those from the stack too, so the file's pool is needed.
struct StackEffect {
    int pops = 0;
    int pushes = 0;
    int scope = 0;           // +1 pushscope or pushwith, -1 popscope
    bool ends = false;       // no fall-through: jump, return, throw, lookupswitch
};
StackEffect stackEffect(const AbcFile& file, const Instruction& instruction);

bool isBranch(uint8_t op);

} // namespace abc
} // namespace as3
