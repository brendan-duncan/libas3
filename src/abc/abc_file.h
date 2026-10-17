#pragma once

// The abcFile format of AVM2 (what a SWF's DoABC tag carries), as data: its
// constant pools, methods, metadata, classes, scripts and method bodies,
// and how it is written and read. Pool indexes are as in the file: 0 is the
// pool's "nothing" (any name, the empty namespace...) and real entries
// start at 1.

#include <cstdint>
#include <string>
#include <vector>

namespace as3 {
namespace abc {

// Namespace kinds.
enum NamespaceKind : uint8_t {
    NS_Private = 0x05,
    NS_Namespace = 0x08,         // a user namespace, or an interface's
    NS_Package = 0x16,           // public
    NS_PackageInternal = 0x17,   // internal
    NS_Protected = 0x18,
    NS_Explicit = 0x19,
    NS_StaticProtected = 0x1A,
};

// Multiname kinds.
enum MultinameKind : uint8_t {
    MN_QName = 0x07,
    MN_QNameA = 0x0D,
    MN_RTQName = 0x0F,
    MN_RTQNameA = 0x10,
    MN_RTQNameL = 0x11,
    MN_RTQNameLA = 0x12,
    MN_Multiname = 0x09,
    MN_MultinameA = 0x0E,
    MN_MultinameL = 0x1B,
    MN_MultinameLA = 0x1C,
    MN_TypeName = 0x1D,
};

// method_info flags.
enum MethodFlags : uint8_t {
    METHOD_NeedArguments = 0x01,
    METHOD_NeedActivation = 0x02,
    METHOD_NeedRest = 0x04,
    METHOD_HasOptional = 0x08,
    METHOD_IgnoreRest = 0x10,
    METHOD_Native = 0x20,
    METHOD_SetDxns = 0x40,
    METHOD_HasParamNames = 0x80,
};

// instance_info flags.
enum InstanceFlags : uint8_t {
    INSTANCE_Sealed = 0x01,
    INSTANCE_Final = 0x02,
    INSTANCE_Interface = 0x04,
    INSTANCE_ProtectedNs = 0x08,
};

// Trait kinds (the low four bits of a trait's kind byte) and attributes
// (the high four).
enum TraitKind : uint8_t {
    TRAIT_Slot = 0,
    TRAIT_Method = 1,
    TRAIT_Getter = 2,
    TRAIT_Setter = 3,
    TRAIT_Class = 4,
    TRAIT_Function = 5,
    TRAIT_Const = 6,
};
enum TraitAttributes : uint8_t {
    ATTR_Final = 0x1,
    ATTR_Override = 0x2,
    ATTR_Metadata = 0x4,
};

// The kind of a constant: an optional parameter's default or a slot's
// value. With it, an index into the pool it names (unused for the ones
// without a value).
enum ConstantKind : uint8_t {
    CONSTANT_Undefined = 0x00,
    CONSTANT_Utf8 = 0x01,
    CONSTANT_Int = 0x03,
    CONSTANT_UInt = 0x04,
    CONSTANT_PrivateNs = 0x05,
    CONSTANT_Double = 0x06,
    CONSTANT_Namespace = 0x08,
    CONSTANT_False = 0x0A,
    CONSTANT_True = 0x0B,
    CONSTANT_Null = 0x0C,
    CONSTANT_PackageNamespace = 0x16,
    CONSTANT_PackageInternalNs = 0x17,
    CONSTANT_ProtectedNamespace = 0x18,
    CONSTANT_ExplicitNamespace = 0x19,
    CONSTANT_StaticProtectedNs = 0x1A,
};

struct Namespace {
    uint8_t kind = NS_Package;
    uint32_t name = 0;    // string
};

struct NamespaceSet {
    std::vector<uint32_t> namespaces;
};

struct Multiname {
    uint8_t kind = MN_QName;
    uint32_t ns = 0;       // QName(A)
    uint32_t name = 0;     // QName(A), RTQName(A), Multiname(A): a string, 0 for any
    uint32_t nsSet = 0;    // Multiname(A), MultinameL(A)
    uint32_t base = 0;     // TypeName: the generic's multiname
    std::vector<uint32_t> parameters;   // TypeName

    // How many of the name's parts are on the stack where it is used: a
    // runtime namespace, a runtime name, or both.
    int runtimeParts() const;
    bool isAttribute() const;
};

struct ConstantPool {
    // Each has an unused entry 0, so that an index is a position.
    std::vector<int32_t> ints{0};
    std::vector<uint32_t> uints{0};
    std::vector<double> doubles{0};
    std::vector<std::string> strings{std::string()};
    std::vector<Namespace> namespaces{Namespace()};
    std::vector<NamespaceSet> nsSets{NamespaceSet()};
    std::vector<Multiname> multinames{Multiname()};
};

struct OptionDetail {
    uint32_t value = 0;
    uint8_t kind = CONSTANT_Undefined;
};

struct MethodInfo {
    std::vector<uint32_t> paramTypes;    // multinames, 0 for *
    uint32_t returnType = 0;             // 0 for *
    uint32_t name = 0;                   // string
    uint8_t flags = 0;
    std::vector<OptionDetail> options;   // the last parameters' defaults
    std::vector<uint32_t> paramNames;    // strings, with HasParamNames
};

struct MetadataInfo {
    uint32_t name = 0;
    // Key and value strings; a key of 0 for a value given without one.
    std::vector<std::pair<uint32_t, uint32_t>> items;
};

struct Trait {
    uint32_t name = 0;          // a QName
    uint8_t kind = TRAIT_Slot;
    uint8_t attributes = 0;
    uint32_t slotId = 0;        // slot, const, class, function; disp_id for the rest
    uint32_t typeName = 0;      // slot, const
    uint32_t valueIndex = 0;    // slot, const: 0 for none
    uint8_t valueKind = 0;
    uint32_t index = 0;         // the class (class) or method (the rest)
    std::vector<uint32_t> metadata;
};

struct InstanceInfo {
    uint32_t name = 0;
    uint32_t superName = 0;
    uint8_t flags = 0;
    uint32_t protectedNs = 0;
    std::vector<uint32_t> interfaces;   // multinames
    uint32_t iinit = 0;
    std::vector<Trait> traits;
};

struct ClassInfo {
    uint32_t cinit = 0;
    std::vector<Trait> traits;
};

struct ScriptInfo {
    uint32_t init = 0;
    std::vector<Trait> traits;
};

struct ExceptionInfo {
    uint32_t from = 0;
    uint32_t to = 0;
    uint32_t target = 0;
    uint32_t excType = 0;    // multiname, 0 for any
    uint32_t varName = 0;    // multiname
};

struct MethodBody {
    uint32_t method = 0;
    uint32_t maxStack = 0;
    uint32_t localCount = 0;
    uint32_t initScopeDepth = 0;
    uint32_t maxScopeDepth = 0;
    std::vector<uint8_t> code;
    std::vector<ExceptionInfo> exceptions;
    std::vector<Trait> traits;   // the activation's slots
};

struct AbcFile {
    uint16_t minorVersion = 16;
    uint16_t majorVersion = 46;
    ConstantPool pool;
    std::vector<MethodInfo> methods;
    std::vector<MetadataInfo> metadata;
    std::vector<InstanceInfo> instances;   // one per class, with classes
    std::vector<ClassInfo> classes;
    std::vector<ScriptInfo> scripts;
    std::vector<MethodBody> bodies;
};

// The file's bytes.
std::vector<uint8_t> write(const AbcFile& file);

// Reads a file; false, with what was wrong in error, when it is not one.
bool read(const std::vector<uint8_t>& bytes, AbcFile& file, std::string& error);

// Encodings, for code.
void writeU30(std::vector<uint8_t>& out, uint32_t value);
void writeS24(std::vector<uint8_t>& out, int32_t value);
void patchS24(std::vector<uint8_t>& out, size_t at, int32_t value);
int32_t readS24(const uint8_t* at);

// Names, for messages and disassembly: "flash.display:Sprite", "x", "*".
std::string namespaceName(const AbcFile& file, uint32_t index);
std::string multinameName(const AbcFile& file, uint32_t index);

} // namespace abc
} // namespace as3
