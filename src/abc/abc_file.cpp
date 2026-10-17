#include "abc_file.h"

#include <cstring>
#include <stdexcept>

namespace as3 {
namespace abc {

int Multiname::runtimeParts() const {
    switch (kind) {
        case MN_RTQName:
        case MN_RTQNameA:
        case MN_MultinameL:
        case MN_MultinameLA:
            return 1;
        case MN_RTQNameL:
        case MN_RTQNameLA:
            return 2;
        default:
            return 0;
    }
}

bool Multiname::isAttribute() const {
    return kind == MN_QNameA || kind == MN_RTQNameA || kind == MN_RTQNameLA || kind == MN_MultinameA ||
           kind == MN_MultinameLA;
}

// Writing

void writeU30(std::vector<uint8_t>& out, uint32_t value) {
    do {
        uint8_t byte = value & 0x7F;
        value >>= 7;
        if (value) byte |= 0x80;
        out.push_back(byte);
    } while (value);
}

void writeS24(std::vector<uint8_t>& out, int32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
}

void patchS24(std::vector<uint8_t>& out, size_t at, int32_t value) {
    out[at] = static_cast<uint8_t>(value & 0xFF);
    out[at + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    out[at + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
}

int32_t readS24(const uint8_t* at) {
    int32_t value = at[0] | (at[1] << 8) | (at[2] << 16);
    if (value & 0x800000) value |= ~0xFFFFFF;
    return value;
}

namespace {

void writeU8(std::vector<uint8_t>& out, uint8_t value) {
    out.push_back(value);
}

void writeU16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

// s32 and u32 are written as u30 is, a signed value sign-extended to 32
// bits.
void writeS32(std::vector<uint8_t>& out, int32_t value) {
    writeU30(out, static_cast<uint32_t>(value));
}

void writeD64(std::vector<uint8_t>& out, double value) {
    uint8_t bytes[8];
    std::memcpy(bytes, &value, 8);
    out.insert(out.end(), bytes, bytes + 8);
}

void writeString(std::vector<uint8_t>& out, const std::string& text) {
    writeU30(out, static_cast<uint32_t>(text.size()));
    out.insert(out.end(), text.begin(), text.end());
}

// A pool's count is one more than its entries, or 0 when it has none.
uint32_t poolCount(size_t size) {
    return size <= 1 ? 0 : static_cast<uint32_t>(size);
}

void writeTraits(std::vector<uint8_t>& out, const std::vector<Trait>& traits) {
    writeU30(out, static_cast<uint32_t>(traits.size()));
    for (const Trait& trait : traits) {
        writeU30(out, trait.name);
        uint8_t attributes = trait.attributes;
        if (!trait.metadata.empty()) attributes |= ATTR_Metadata;
        else attributes &= ~ATTR_Metadata;
        writeU8(out, static_cast<uint8_t>((attributes << 4) | (trait.kind & 0x0F)));
        switch (trait.kind) {
            case TRAIT_Slot:
            case TRAIT_Const:
                writeU30(out, trait.slotId);
                writeU30(out, trait.typeName);
                writeU30(out, trait.valueIndex);
                if (trait.valueIndex) writeU8(out, trait.valueKind);
                break;
            default:
                // Class, function, method, getter, setter.
                writeU30(out, trait.slotId);
                writeU30(out, trait.index);
                break;
        }
        if (!trait.metadata.empty()) {
            writeU30(out, static_cast<uint32_t>(trait.metadata.size()));
            for (uint32_t index : trait.metadata) writeU30(out, index);
        }
    }
}

} // namespace

std::vector<uint8_t> write(const AbcFile& file) {
    std::vector<uint8_t> out;
    writeU16(out, file.minorVersion);
    writeU16(out, file.majorVersion);

    const ConstantPool& pool = file.pool;
    writeU30(out, poolCount(pool.ints.size()));
    for (size_t i = 1; i < pool.ints.size(); ++i) writeS32(out, pool.ints[i]);
    writeU30(out, poolCount(pool.uints.size()));
    for (size_t i = 1; i < pool.uints.size(); ++i) writeU30(out, pool.uints[i]);
    writeU30(out, poolCount(pool.doubles.size()));
    for (size_t i = 1; i < pool.doubles.size(); ++i) writeD64(out, pool.doubles[i]);
    writeU30(out, poolCount(pool.strings.size()));
    for (size_t i = 1; i < pool.strings.size(); ++i) writeString(out, pool.strings[i]);
    writeU30(out, poolCount(pool.namespaces.size()));
    for (size_t i = 1; i < pool.namespaces.size(); ++i) {
        writeU8(out, pool.namespaces[i].kind);
        writeU30(out, pool.namespaces[i].name);
    }
    writeU30(out, poolCount(pool.nsSets.size()));
    for (size_t i = 1; i < pool.nsSets.size(); ++i) {
        writeU30(out, static_cast<uint32_t>(pool.nsSets[i].namespaces.size()));
        for (uint32_t ns : pool.nsSets[i].namespaces) writeU30(out, ns);
    }
    writeU30(out, poolCount(pool.multinames.size()));
    for (size_t i = 1; i < pool.multinames.size(); ++i) {
        const Multiname& name = pool.multinames[i];
        writeU8(out, name.kind);
        switch (name.kind) {
            case MN_QName:
            case MN_QNameA:
                writeU30(out, name.ns);
                writeU30(out, name.name);
                break;
            case MN_RTQName:
            case MN_RTQNameA:
                writeU30(out, name.name);
                break;
            case MN_RTQNameL:
            case MN_RTQNameLA:
                break;
            case MN_Multiname:
            case MN_MultinameA:
                writeU30(out, name.name);
                writeU30(out, name.nsSet);
                break;
            case MN_MultinameL:
            case MN_MultinameLA:
                writeU30(out, name.nsSet);
                break;
            case MN_TypeName:
                writeU30(out, name.base);
                writeU30(out, static_cast<uint32_t>(name.parameters.size()));
                for (uint32_t parameter : name.parameters) writeU30(out, parameter);
                break;
        }
    }

    writeU30(out, static_cast<uint32_t>(file.methods.size()));
    for (const MethodInfo& method : file.methods) {
        writeU30(out, static_cast<uint32_t>(method.paramTypes.size()));
        writeU30(out, method.returnType);
        for (uint32_t type : method.paramTypes) writeU30(out, type);
        writeU30(out, method.name);
        uint8_t flags = method.flags;
        if (!method.options.empty()) flags |= METHOD_HasOptional;
        else flags &= ~METHOD_HasOptional;
        if (!method.paramNames.empty()) flags |= METHOD_HasParamNames;
        else flags &= ~METHOD_HasParamNames;
        writeU8(out, flags);
        if (!method.options.empty()) {
            writeU30(out, static_cast<uint32_t>(method.options.size()));
            for (const OptionDetail& option : method.options) {
                writeU30(out, option.value);
                writeU8(out, option.kind);
            }
        }
        for (uint32_t name : method.paramNames) writeU30(out, name);
    }

    // Each tag's keys, then its values (as players read them; the
    // specification's pairs are not what is in files).
    writeU30(out, static_cast<uint32_t>(file.metadata.size()));
    for (const MetadataInfo& tag : file.metadata) {
        writeU30(out, tag.name);
        writeU30(out, static_cast<uint32_t>(tag.items.size()));
        for (const auto& item : tag.items) writeU30(out, item.first);
        for (const auto& item : tag.items) writeU30(out, item.second);
    }

    writeU30(out, static_cast<uint32_t>(file.instances.size()));
    for (const InstanceInfo& instance : file.instances) {
        writeU30(out, instance.name);
        writeU30(out, instance.superName);
        writeU8(out, instance.flags);
        if (instance.flags & INSTANCE_ProtectedNs) writeU30(out, instance.protectedNs);
        writeU30(out, static_cast<uint32_t>(instance.interfaces.size()));
        for (uint32_t face : instance.interfaces) writeU30(out, face);
        writeU30(out, instance.iinit);
        writeTraits(out, instance.traits);
    }
    for (const ClassInfo& cls : file.classes) {
        writeU30(out, cls.cinit);
        writeTraits(out, cls.traits);
    }

    writeU30(out, static_cast<uint32_t>(file.scripts.size()));
    for (const ScriptInfo& script : file.scripts) {
        writeU30(out, script.init);
        writeTraits(out, script.traits);
    }

    writeU30(out, static_cast<uint32_t>(file.bodies.size()));
    for (const MethodBody& body : file.bodies) {
        writeU30(out, body.method);
        writeU30(out, body.maxStack);
        writeU30(out, body.localCount);
        writeU30(out, body.initScopeDepth);
        writeU30(out, body.maxScopeDepth);
        writeU30(out, static_cast<uint32_t>(body.code.size()));
        out.insert(out.end(), body.code.begin(), body.code.end());
        writeU30(out, static_cast<uint32_t>(body.exceptions.size()));
        for (const ExceptionInfo& entry : body.exceptions) {
            writeU30(out, entry.from);
            writeU30(out, entry.to);
            writeU30(out, entry.target);
            writeU30(out, entry.excType);
            writeU30(out, entry.varName);
        }
        writeTraits(out, body.traits);
    }
    return out;
}

// Reading

namespace {

struct Reader {
    const std::vector<uint8_t>& bytes;
    size_t at = 0;

    struct Failed {
        std::string message;
    };

    void need(size_t count) {
        if (at + count > bytes.size()) {
            throw Failed{"the file ends early, at byte " + std::to_string(at)};
        }
    }
    uint8_t u8() {
        need(1);
        return bytes[at++];
    }
    uint16_t u16() {
        need(2);
        const uint16_t value = static_cast<uint16_t>(bytes[at] | (bytes[at + 1] << 8));
        at += 2;
        return value;
    }
    uint32_t u32() {
        uint32_t value = 0;
        for (int shift = 0; shift < 35; shift += 7) {
            const uint8_t byte = u8();
            value |= static_cast<uint32_t>(byte & 0x7F) << shift;
            if (!(byte & 0x80)) return value;
        }
        return value;
    }
    uint32_t u30() {
        const size_t start = at;
        const uint32_t value = u32();
        if (value > 0x3FFFFFFF) {
            throw Failed{"a u30 out of range at byte " + std::to_string(start)};
        }
        return value;
    }
    double d64() {
        need(8);
        double value;
        std::memcpy(&value, &bytes[at], 8);
        at += 8;
        return value;
    }
    std::string string() {
        const uint32_t size = u30();
        need(size);
        std::string text(reinterpret_cast<const char*>(&bytes[at]), size);
        at += size;
        return text;
    }
    // A count that cannot be more than the bytes left, to fail before
    // reserving room for a bad one.
    uint32_t count() {
        const uint32_t value = u30();
        if (value > bytes.size() - at + 1) {
            throw Failed{"a count of " + std::to_string(value) + " at byte " + std::to_string(at) +
                         " is more than the file holds"};
        }
        return value;
    }

    std::vector<Trait> traits() {
        std::vector<Trait> list(count());
        for (Trait& trait : list) {
            trait.name = u30();
            const uint8_t kind = u8();
            trait.kind = kind & 0x0F;
            trait.attributes = kind >> 4;
            switch (trait.kind) {
                case TRAIT_Slot:
                case TRAIT_Const:
                    trait.slotId = u30();
                    trait.typeName = u30();
                    trait.valueIndex = u30();
                    if (trait.valueIndex) trait.valueKind = u8();
                    break;
                case TRAIT_Method:
                case TRAIT_Getter:
                case TRAIT_Setter:
                case TRAIT_Class:
                case TRAIT_Function:
                    trait.slotId = u30();
                    trait.index = u30();
                    break;
                default:
                    throw Failed{"a trait of kind " + std::to_string(trait.kind) + " at byte " + std::to_string(at)};
            }
            if (trait.attributes & ATTR_Metadata) {
                trait.metadata.resize(count());
                for (uint32_t& index : trait.metadata) index = u30();
            }
        }
        return list;
    }
};

} // namespace

bool read(const std::vector<uint8_t>& bytes, AbcFile& file, std::string& error) {
    Reader in{bytes};
    file = AbcFile();
    try {
        file.minorVersion = in.u16();
        file.majorVersion = in.u16();
        ConstantPool& pool = file.pool;
        uint32_t n = in.count();
        for (uint32_t i = 1; i < n; ++i) pool.ints.push_back(static_cast<int32_t>(in.u32()));
        n = in.count();
        for (uint32_t i = 1; i < n; ++i) pool.uints.push_back(in.u32());
        n = in.count();
        for (uint32_t i = 1; i < n; ++i) pool.doubles.push_back(in.d64());
        n = in.count();
        for (uint32_t i = 1; i < n; ++i) pool.strings.push_back(in.string());
        n = in.count();
        for (uint32_t i = 1; i < n; ++i) {
            Namespace ns;
            ns.kind = in.u8();
            ns.name = in.u30();
            pool.namespaces.push_back(ns);
        }
        n = in.count();
        for (uint32_t i = 1; i < n; ++i) {
            NamespaceSet set;
            set.namespaces.resize(in.count());
            for (uint32_t& ns : set.namespaces) ns = in.u30();
            pool.nsSets.push_back(std::move(set));
        }
        n = in.count();
        for (uint32_t i = 1; i < n; ++i) {
            Multiname name;
            name.kind = in.u8();
            switch (name.kind) {
                case MN_QName:
                case MN_QNameA:
                    name.ns = in.u30();
                    name.name = in.u30();
                    break;
                case MN_RTQName:
                case MN_RTQNameA:
                    name.name = in.u30();
                    break;
                case MN_RTQNameL:
                case MN_RTQNameLA:
                    break;
                case MN_Multiname:
                case MN_MultinameA:
                    name.name = in.u30();
                    name.nsSet = in.u30();
                    break;
                case MN_MultinameL:
                case MN_MultinameLA:
                    name.nsSet = in.u30();
                    break;
                case MN_TypeName:
                    name.base = in.u30();
                    name.parameters.resize(in.count());
                    for (uint32_t& parameter : name.parameters) parameter = in.u30();
                    break;
                default:
                    throw Reader::Failed{"multiname " + std::to_string(i) + " has kind " + std::to_string(name.kind)};
            }
            pool.multinames.push_back(std::move(name));
        }

        file.methods.resize(in.count());
        for (MethodInfo& method : file.methods) {
            const uint32_t params = in.count();
            method.returnType = in.u30();
            method.paramTypes.resize(params);
            for (uint32_t& type : method.paramTypes) type = in.u30();
            method.name = in.u30();
            method.flags = in.u8();
            if (method.flags & METHOD_HasOptional) {
                method.options.resize(in.count());
                for (OptionDetail& option : method.options) {
                    option.value = in.u30();
                    option.kind = in.u8();
                }
            }
            if (method.flags & METHOD_HasParamNames) {
                method.paramNames.resize(params);
                for (uint32_t& name : method.paramNames) name = in.u30();
            }
        }

        file.metadata.resize(in.count());
        for (MetadataInfo& tag : file.metadata) {
            tag.name = in.u30();
            tag.items.resize(in.count());
            for (auto& item : tag.items) item.first = in.u30();
            for (auto& item : tag.items) item.second = in.u30();
        }

        const uint32_t classCount = in.count();
        file.instances.resize(classCount);
        for (InstanceInfo& instance : file.instances) {
            instance.name = in.u30();
            instance.superName = in.u30();
            instance.flags = in.u8();
            if (instance.flags & INSTANCE_ProtectedNs) instance.protectedNs = in.u30();
            instance.interfaces.resize(in.count());
            for (uint32_t& face : instance.interfaces) face = in.u30();
            instance.iinit = in.u30();
            instance.traits = in.traits();
        }
        file.classes.resize(classCount);
        for (ClassInfo& cls : file.classes) {
            cls.cinit = in.u30();
            cls.traits = in.traits();
        }

        file.scripts.resize(in.count());
        for (ScriptInfo& script : file.scripts) {
            script.init = in.u30();
            script.traits = in.traits();
        }

        file.bodies.resize(in.count());
        for (MethodBody& body : file.bodies) {
            body.method = in.u30();
            body.maxStack = in.u30();
            body.localCount = in.u30();
            body.initScopeDepth = in.u30();
            body.maxScopeDepth = in.u30();
            const uint32_t length = in.count();
            in.need(length);
            body.code.assign(bytes.begin() + static_cast<std::ptrdiff_t>(in.at),
                             bytes.begin() + static_cast<std::ptrdiff_t>(in.at + length));
            in.at += length;
            body.exceptions.resize(in.count());
            for (ExceptionInfo& entry : body.exceptions) {
                entry.from = in.u30();
                entry.to = in.u30();
                entry.target = in.u30();
                entry.excType = in.u30();
                entry.varName = in.u30();
            }
            body.traits = in.traits();
        }
        if (in.at != bytes.size()) {
            throw Reader::Failed{std::to_string(bytes.size() - in.at) + " bytes after the last method body"};
        }
    } catch (const Reader::Failed& failed) {
        error = failed.message;
        return false;
    }
    return true;
}

// Names

std::string namespaceName(const AbcFile& file, uint32_t index) {
    if (index >= file.pool.namespaces.size()) return "<bad namespace " + std::to_string(index) + ">";
    if (index == 0) return "*";
    const Namespace& ns = file.pool.namespaces[index];
    const std::string name = ns.name < file.pool.strings.size() ? file.pool.strings[ns.name] : "?";
    switch (ns.kind) {
        case NS_Private: return "private(" + name + ")";
        case NS_PackageInternal: return "internal(" + name + ")";
        case NS_Protected: return "protected(" + name + ")";
        case NS_StaticProtected: return "staticprotected(" + name + ")";
        case NS_Explicit: return "explicit(" + name + ")";
        case NS_Namespace: return "namespace(" + name + ")";
        default: return name;
    }
}

std::string multinameName(const AbcFile& file, uint32_t index) {
    if (index >= file.pool.multinames.size()) return "<bad multiname " + std::to_string(index) + ">";
    if (index == 0) return "*";
    const Multiname& name = file.pool.multinames[index];
    const auto text = [&file](uint32_t string) {
        if (string == 0) return std::string("*");
        return string < file.pool.strings.size() ? file.pool.strings[string] : std::string("?");
    };
    const std::string attribute = name.isAttribute() ? "@" : "";
    switch (name.kind) {
        case MN_QName:
        case MN_QNameA: {
            const std::string ns = namespaceName(file, name.ns);
            return attribute + (ns.empty() ? "" : ns + ":") + text(name.name);
        }
        case MN_RTQName:
        case MN_RTQNameA:
            return attribute + "[rt]:" + text(name.name);
        case MN_RTQNameL:
        case MN_RTQNameLA:
            return attribute + "[rt]:[rt]";
        case MN_Multiname:
        case MN_MultinameA:
            return attribute + "{" + std::to_string(name.nsSet) + "}:" + text(name.name);
        case MN_MultinameL:
        case MN_MultinameLA:
            return attribute + "{" + std::to_string(name.nsSet) + "}:[rt]";
        case MN_TypeName: {
            std::string result = multinameName(file, name.base) + ".<";
            for (size_t i = 0; i < name.parameters.size(); ++i) {
                if (i) result += ",";
                result += multinameName(file, name.parameters[i]);
            }
            return result + ">";
        }
        default:
            return "?";
    }
}

} // namespace abc
} // namespace as3
