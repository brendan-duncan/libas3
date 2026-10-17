#include "abc_verifier.h"
#include "abc_opcodes.h"

#include <cmath>
#include <map>
#include <set>
#include <sstream>

namespace as3 {
namespace abc {

namespace {

std::string traitKindName(uint8_t kind) {
    switch (kind) {
        case TRAIT_Slot: return "slot";
        case TRAIT_Method: return "method";
        case TRAIT_Getter: return "getter";
        case TRAIT_Setter: return "setter";
        case TRAIT_Class: return "class";
        case TRAIT_Function: return "function";
        case TRAIT_Const: return "const";
        default: return "?";
    }
}

std::string quoted(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') {
            out += "\\n";
            continue;
        }
        out += c;
    }
    return out + "\"";
}

class Verifier {
public:
    explicit Verifier(const AbcFile& file) : file(file), pool(file.pool), names(methodNames(file)) {}

    std::vector<std::string> run() {
        checkPools();
        checkMethods();
        checkMetadata();
        checkClasses();
        checkScripts();
        checkBodies();
        return problems;
    }

private:
    const AbcFile& file;
    const ConstantPool& pool;
    std::vector<std::string> names;
    std::vector<std::string> problems;
    // Methods whose body must be there, and those (an interface's) that
    // must not have one.
    std::set<uint32_t> needBody;
    std::set<uint32_t> noBody;
    std::vector<int> bodyOf;

    void problem(const std::string& message) {
        problems.push_back(message);
    }

    bool validString(uint32_t index) const { return index < pool.strings.size(); }
    bool validNamespace(uint32_t index) const { return index < pool.namespaces.size(); }
    bool validMultiname(uint32_t index) const { return index < pool.multinames.size(); }
    bool validMethod(uint32_t index) const { return index < file.methods.size(); }

    bool isQName(uint32_t index) const {
        return index > 0 && index < pool.multinames.size() && pool.multinames[index].kind == MN_QName;
    }

    std::string methodName(uint32_t index) const {
        return "method " + std::to_string(index) + (index < names.size() && !names[index].empty() ? " (" + names[index] + ")" : "");
    }

    void checkPools() {
        for (size_t i = 1; i < pool.namespaces.size(); ++i) {
            const Namespace& ns = pool.namespaces[i];
            if (!validString(ns.name)) problem("namespace " + std::to_string(i) + ": bad name " + std::to_string(ns.name));
            switch (ns.kind) {
                case NS_Private: case NS_Namespace: case NS_Package: case NS_PackageInternal:
                case NS_Protected: case NS_Explicit: case NS_StaticProtected:
                    break;
                default:
                    problem("namespace " + std::to_string(i) + ": kind " + std::to_string(ns.kind));
            }
        }
        for (size_t i = 1; i < pool.nsSets.size(); ++i) {
            if (pool.nsSets[i].namespaces.empty()) problem("namespace set " + std::to_string(i) + " is empty");
            for (uint32_t ns : pool.nsSets[i].namespaces) {
                if (ns == 0 || !validNamespace(ns)) {
                    problem("namespace set " + std::to_string(i) + ": bad namespace " + std::to_string(ns));
                }
            }
        }
        for (size_t i = 1; i < pool.multinames.size(); ++i) {
            const Multiname& name = pool.multinames[i];
            const std::string what = "multiname " + std::to_string(i);
            switch (name.kind) {
                case MN_QName: case MN_QNameA:
                    if (!validNamespace(name.ns)) problem(what + ": bad namespace " + std::to_string(name.ns));
                    if (!validString(name.name)) problem(what + ": bad name " + std::to_string(name.name));
                    break;
                case MN_RTQName: case MN_RTQNameA:
                    if (!validString(name.name)) problem(what + ": bad name " + std::to_string(name.name));
                    break;
                case MN_Multiname: case MN_MultinameA:
                    if (!validString(name.name)) problem(what + ": bad name " + std::to_string(name.name));
                    if (name.nsSet == 0 || name.nsSet >= pool.nsSets.size()) problem(what + ": bad namespace set " + std::to_string(name.nsSet));
                    break;
                case MN_MultinameL: case MN_MultinameLA:
                    if (name.nsSet == 0 || name.nsSet >= pool.nsSets.size()) problem(what + ": bad namespace set " + std::to_string(name.nsSet));
                    break;
                case MN_TypeName:
                    if (!isQName(name.base)) problem(what + ": the generic is not a QName");
                    if (name.parameters.size() != 1) problem(what + ": " + std::to_string(name.parameters.size()) + " type parameters");
                    for (uint32_t parameter : name.parameters) {
                        if (!validMultiname(parameter)) problem(what + ": bad type parameter " + std::to_string(parameter));
                    }
                    break;
                default:
                    break;
            }
        }
    }

    bool validConstant(uint8_t kind, uint32_t index) const {
        switch (kind) {
            case CONSTANT_Int: return index > 0 && index < pool.ints.size();
            case CONSTANT_UInt: return index > 0 && index < pool.uints.size();
            case CONSTANT_Double: return index > 0 && index < pool.doubles.size();
            case CONSTANT_Utf8: return index < pool.strings.size();
            case CONSTANT_True: case CONSTANT_False: case CONSTANT_Null: case CONSTANT_Undefined: return true;
            case CONSTANT_Namespace: case CONSTANT_PackageNamespace: case CONSTANT_PackageInternalNs:
            case CONSTANT_ProtectedNamespace: case CONSTANT_ExplicitNamespace: case CONSTANT_StaticProtectedNs:
            case CONSTANT_PrivateNs:
                return index > 0 && index < pool.namespaces.size();
            default:
                return false;
        }
    }

    void checkMethods() {
        for (size_t i = 0; i < file.methods.size(); ++i) {
            const MethodInfo& method = file.methods[i];
            const std::string what = methodName(static_cast<uint32_t>(i));
            for (uint32_t type : method.paramTypes) {
                if (!validMultiname(type)) problem(what + ": bad parameter type " + std::to_string(type));
            }
            if (!validMultiname(method.returnType)) problem(what + ": bad return type");
            if (!validString(method.name)) problem(what + ": bad name");
            if (method.options.size() > method.paramTypes.size()) problem(what + ": more defaults than parameters");
            for (const OptionDetail& option : method.options) {
                if (!validConstant(option.kind, option.value)) {
                    problem(what + ": bad default (kind " + std::to_string(option.kind) + ", " + std::to_string(option.value) + ")");
                }
            }
            if (!method.paramNames.empty() && method.paramNames.size() != method.paramTypes.size()) {
                problem(what + ": parameter names do not match the parameters");
            }
            for (uint32_t name : method.paramNames) {
                if (!validString(name)) problem(what + ": bad parameter name");
            }
            if ((method.flags & METHOD_NeedArguments) && (method.flags & METHOD_NeedRest)) {
                problem(what + ": needs both arguments and a rest array");
            }
        }
    }

    void checkMetadata() {
        for (size_t i = 0; i < file.metadata.size(); ++i) {
            const MetadataInfo& tag = file.metadata[i];
            if (!validString(tag.name) || tag.name == 0) problem("metadata " + std::to_string(i) + ": bad name");
            for (const auto& item : tag.items) {
                if (!validString(item.first) || !validString(item.second)) problem("metadata " + std::to_string(i) + ": bad item");
            }
        }
    }

    void checkTraits(const std::vector<Trait>& traits, const std::string& where, bool isInterface) {
        std::set<uint32_t> slots;
        std::set<std::pair<uint32_t, int>> seen;
        for (const Trait& trait : traits) {
            const std::string what = where + " trait " + multinameName(file, trait.name);
            if (!isQName(trait.name)) problem(what + ": its name is not a QName");
            // A getter and a setter may share a name; nothing else may.
            const int group = trait.kind == TRAIT_Getter ? 1 : trait.kind == TRAIT_Setter ? 2 : 0;
            if (!seen.insert({trait.name, group}).second) problem(what + ": defined twice");
            switch (trait.kind) {
                case TRAIT_Slot:
                case TRAIT_Const:
                    if (!validMultiname(trait.typeName)) problem(what + ": bad type");
                    if (trait.valueIndex && !validConstant(trait.valueKind, trait.valueIndex)) problem(what + ": bad value");
                    break;
                case TRAIT_Class:
                    if (trait.index >= file.classes.size()) problem(what + ": bad class " + std::to_string(trait.index));
                    break;
                case TRAIT_Method:
                case TRAIT_Getter:
                case TRAIT_Setter:
                case TRAIT_Function:
                    if (!validMethod(trait.index)) {
                        problem(what + ": bad method " + std::to_string(trait.index));
                    } else if (isInterface) {
                        noBody.insert(trait.index);
                    } else if (!(file.methods[trait.index].flags & METHOD_Native)) {
                        needBody.insert(trait.index);
                    }
                    break;
                default:
                    problem(what + ": kind " + std::to_string(trait.kind));
            }
            if (trait.kind == TRAIT_Slot || trait.kind == TRAIT_Const || trait.kind == TRAIT_Class ||
                trait.kind == TRAIT_Function) {
                if (trait.slotId && !slots.insert(trait.slotId).second) problem(what + ": slot " + std::to_string(trait.slotId) + " is taken");
            }
            for (uint32_t index : trait.metadata) {
                if (index >= file.metadata.size()) problem(what + ": bad metadata " + std::to_string(index));
            }
        }
    }

    void checkClasses() {
        if (file.instances.size() != file.classes.size()) problem("instance and class counts differ");
        for (size_t i = 0; i < file.instances.size(); ++i) {
            const InstanceInfo& instance = file.instances[i];
            const std::string what = "class " + multinameName(file, instance.name);
            const bool isInterface = (instance.flags & INSTANCE_Interface) != 0;
            if (!isQName(instance.name)) problem(what + ": its name is not a QName");
            if (!validMultiname(instance.superName)) problem(what + ": bad superclass");
            if (isInterface && instance.superName) problem(what + ": an interface with a superclass");
            if ((instance.flags & INSTANCE_ProtectedNs) && (!instance.protectedNs || !validNamespace(instance.protectedNs))) {
                problem(what + ": bad protected namespace");
            }
            for (uint32_t face : instance.interfaces) {
                if (face == 0 || !validMultiname(face)) problem(what + ": bad interface " + std::to_string(face));
            }
            if (!validMethod(instance.iinit)) {
                problem(what + ": bad constructor " + std::to_string(instance.iinit));
            } else if (!isInterface) {
                needBody.insert(instance.iinit);
            }
            checkTraits(instance.traits, what, isInterface);
            if (i < file.classes.size()) {
                const ClassInfo& cls = file.classes[i];
                if (!validMethod(cls.cinit)) problem(what + ": bad static initializer");
                else needBody.insert(cls.cinit);
                checkTraits(cls.traits, what + " static", false);
            }
        }
    }

    void checkScripts() {
        if (file.scripts.empty()) problem("no scripts");
        for (size_t i = 0; i < file.scripts.size(); ++i) {
            const ScriptInfo& script = file.scripts[i];
            if (!validMethod(script.init)) problem("script " + std::to_string(i) + ": bad init");
            else needBody.insert(script.init);
            checkTraits(script.traits, "script " + std::to_string(i), false);
        }
    }

    void checkBodies() {
        bodyOf.assign(file.methods.size(), -1);
        for (size_t i = 0; i < file.bodies.size(); ++i) {
            const uint32_t method = file.bodies[i].method;
            if (!validMethod(method)) {
                problem("body " + std::to_string(i) + ": bad method " + std::to_string(method));
                continue;
            }
            if (bodyOf[method] >= 0) problem(methodName(method) + ": two bodies");
            bodyOf[method] = static_cast<int>(i);
        }
        // Functions made by newfunction need bodies too; they are found
        // in the code, so the bodies are checked first.
        for (size_t i = 0; i < file.bodies.size(); ++i) {
            if (validMethod(file.bodies[i].method)) checkCode(file.bodies[i]);
        }
        for (uint32_t method : needBody) {
            if (method < bodyOf.size() && bodyOf[method] < 0 && !noBody.count(method)) {
                problem(methodName(method) + ": no body");
            }
        }
        for (uint32_t method : noBody) {
            if (method < bodyOf.size() && bodyOf[method] >= 0) problem(methodName(method) + ": an interface method with a body");
        }
    }

    struct State {
        int stack = -1;
        int scope = -1;
    };

    void checkCode(const MethodBody& body) {
        const MethodInfo& method = file.methods[body.method];
        const std::string what = methodName(body.method);
        const auto fail = [&](size_t offset, const std::string& message) {
            problem(what + " at " + std::to_string(offset) + ": " + message);
        };
        uint32_t params = static_cast<uint32_t>(method.paramTypes.size()) + 1;
        if (method.flags & (METHOD_NeedArguments | METHOD_NeedRest)) ++params;
        if (body.localCount < params) problem(what + ": " + std::to_string(body.localCount) + " locals for " + std::to_string(params) + " parameters");
        if (body.maxScopeDepth < body.initScopeDepth) problem(what + ": max_scope_depth below init_scope_depth");
        checkTraits(body.traits, what + " activation", false);
        if (!body.traits.empty() && !(method.flags & METHOD_NeedActivation)) problem(what + ": activation slots without NEED_ACTIVATION");
        if (body.code.empty()) {
            problem(what + ": no code");
            return;
        }

        // Decode it all, so that targets can be checked against the
        // instructions' starts.
        std::map<size_t, Instruction> code;
        for (size_t at = 0; at < body.code.size();) {
            Instruction instruction;
            std::string error;
            if (!decode(body.code, at, instruction, error)) {
                fail(at, error);
                return;
            }
            at += instruction.length;
            code[instruction.offset] = std::move(instruction);
        }

        const int maxScope = static_cast<int>(body.maxScopeDepth) - static_cast<int>(body.initScopeDepth);
        std::map<size_t, State> states;
        std::vector<size_t> work;
        const auto reach = [&](size_t from, size_t target, State state) {
            auto found = code.find(target);
            if (found == code.end()) {
                fail(from, "a branch to " + std::to_string(target) + ", not an instruction");
                return;
            }
            if (target <= from && found->second.op != OP_label && from != target) {
                // Players want a loop's head marked.
                fail(from, "a backward branch to " + std::to_string(target) + ", not a label");
            }
            State& known = states[target];
            if (known.stack < 0) {
                known = state;
                work.push_back(target);
            } else if (known.stack != state.stack || known.scope != state.scope) {
                fail(from, "reaches " + std::to_string(target) + " with stack " + std::to_string(state.stack) + " scope " +
                               std::to_string(state.scope) + ", where another path has stack " + std::to_string(known.stack) +
                               " scope " + std::to_string(known.scope));
            }
        };

        for (const ExceptionInfo& entry : body.exceptions) {
            const std::string where = "exception " + std::to_string(entry.from) + "-" + std::to_string(entry.to);
            if (entry.from > entry.to || entry.to > body.code.size()) problem(what + ": " + where + " is out of the code");
            if (!code.count(entry.from) || (entry.to < body.code.size() && !code.count(entry.to))) {
                problem(what + ": " + where + " does not start or end on an instruction");
            }
            if (!validMultiname(entry.excType)) problem(what + ": " + where + ": bad type");
            if (!validMultiname(entry.varName)) problem(what + ": " + where + ": bad name");
            reach(entry.target, entry.target, State{1, 0});
        }
        reach(0, 0, State{0, 0});

        while (!work.empty()) {
            const size_t at = work.back();
            work.pop_back();
            const Instruction& in = code[at];
            State state = states[at];
            const OpInfo& info = opInfo(in.op);

            // Operands.
            for (int i = 0; i < 2; ++i) {
                const uint32_t value = in.operands[i];
                switch (info.operands[i]) {
                    case Operand::Multiname:
                        if (value == 0 || !validMultiname(value)) fail(at, std::string(info.name) + ": bad multiname " + std::to_string(value));
                        break;
                    case Operand::String:
                        if (value == 0 || !validString(value)) fail(at, std::string(info.name) + ": bad string " + std::to_string(value));
                        break;
                    case Operand::Int:
                        if (value == 0 || value >= pool.ints.size()) fail(at, "bad int " + std::to_string(value));
                        break;
                    case Operand::UInt:
                        if (value == 0 || value >= pool.uints.size()) fail(at, "bad uint " + std::to_string(value));
                        break;
                    case Operand::Double:
                        if (value == 0 || value >= pool.doubles.size()) fail(at, "bad double " + std::to_string(value));
                        break;
                    case Operand::Namespace:
                        if (value == 0 || !validNamespace(value)) fail(at, "bad namespace " + std::to_string(value));
                        break;
                    case Operand::Method:
                        if (!validMethod(value)) fail(at, "bad method " + std::to_string(value));
                        else if (bodyOf[value] < 0 && in.op == OP_newfunction) fail(at, "newfunction of " + methodName(value) + ", which has no body");
                        break;
                    case Operand::Class:
                        if (value >= file.classes.size()) fail(at, "bad class " + std::to_string(value));
                        break;
                    case Operand::Exception:
                        if (value >= body.exceptions.size()) fail(at, "bad exception " + std::to_string(value));
                        break;
                    case Operand::Register:
                        if (value >= body.localCount) fail(at, std::string(info.name) + " of register " + std::to_string(value) + ", past local_count " + std::to_string(body.localCount));
                        break;
                    default:
                        break;
                }
            }
            if (in.op >= OP_getlocal0 && in.op <= OP_setlocal3) {
                const uint32_t reg = (in.op - OP_getlocal0) & 3;
                if (reg >= body.localCount) fail(at, std::string(info.name) + " past local_count");
            }
            if ((in.op == OP_getlex || in.op == OP_finddef) && validMultiname(in.operands[0]) &&
                pool.multinames[in.operands[0]].runtimeParts() > 0) {
                fail(at, std::string(info.name) + " of a runtime name");
            }
            if (in.op == OP_getscopeobject && static_cast<int>(in.operands[0]) >= state.scope) {
                fail(at, "getscopeobject " + std::to_string(in.operands[0]) + " with " + std::to_string(state.scope) + " scopes");
            }
            if (in.op == OP_newactivation && !(method.flags & METHOD_NeedActivation)) fail(at, "newactivation without NEED_ACTIVATION");

            const StackEffect effect = stackEffect(file, in);
            if (state.stack < effect.pops) {
                fail(at, std::string(info.name) + " takes " + std::to_string(effect.pops) + " from a stack of " + std::to_string(state.stack));
                continue;
            }
            state.stack += effect.pushes - effect.pops;
            if (state.stack > static_cast<int>(body.maxStack) || state.stack - effect.pushes + effect.pops > static_cast<int>(body.maxStack)) {
                fail(at, "the stack reaches " + std::to_string(std::max(state.stack, state.stack - effect.pushes + effect.pops)) + ", past max_stack " + std::to_string(body.maxStack));
                continue;
            }
            state.scope += effect.scope;
            if (state.scope < 0) {
                fail(at, "popscope with no scope");
                continue;
            }
            if (state.scope > maxScope) {
                fail(at, "the scope stack reaches " + std::to_string(state.scope) + ", past max_scope_depth - init_scope_depth " + std::to_string(maxScope));
                continue;
            }

            if (in.op == OP_lookupswitch) {
                for (int32_t delta : in.targets) {
                    reach(at, static_cast<size_t>(static_cast<int64_t>(at) + delta), state);
                }
            } else {
                for (int32_t target : in.targets) {
                    if (target < 0) fail(at, "a branch before the code");
                    else reach(at, static_cast<size_t>(target), state);
                }
            }
            if (!effect.ends) {
                const size_t next = at + in.length;
                if (next >= body.code.size()) fail(at, "the code runs off its end");
                else reach(at, next, state);
            }
        }
    }
};

void listTraits(std::ostringstream& out, const AbcFile& file, const std::vector<Trait>& traits, const char* indent) {
    for (const Trait& trait : traits) {
        out << indent << traitKindName(trait.kind) << " " << multinameName(file, trait.name);
        if (trait.attributes & ATTR_Override) out << " override";
        if (trait.attributes & ATTR_Final) out << " final";
        switch (trait.kind) {
            case TRAIT_Slot:
            case TRAIT_Const:
                out << " slot " << trait.slotId << " : " << multinameName(file, trait.typeName);
                if (trait.valueIndex) out << " = (" << int(trait.valueKind) << ") " << trait.valueIndex;
                break;
            case TRAIT_Class:
                out << " slot " << trait.slotId << " class " << trait.index;
                break;
            default:
                out << " disp " << trait.slotId << " method " << trait.index;
                break;
        }
        out << "\n";
    }
}

} // namespace

std::vector<std::string> verify(const AbcFile& file) {
    return Verifier(file).run();
}

std::vector<std::string> methodNames(const AbcFile& file) {
    std::vector<std::string> names(file.methods.size());
    const auto name = [&names](uint32_t method, const std::string& text) {
        if (method < names.size() && names[method].empty()) names[method] = text;
    };
    const auto shortName = [&file](uint32_t multiname) {
        std::string text = multinameName(file, multiname);
        return text;
    };
    const auto traits = [&](const std::vector<Trait>& list, const std::string& owner) {
        for (const Trait& trait : list) {
            if (trait.kind == TRAIT_Method || trait.kind == TRAIT_Getter || trait.kind == TRAIT_Setter ||
                trait.kind == TRAIT_Function) {
                const char* prefix = trait.kind == TRAIT_Getter ? "get " : trait.kind == TRAIT_Setter ? "set " : "";
                name(trait.index, owner + "/" + prefix + shortName(trait.name));
            }
        }
    };
    for (size_t i = 0; i < file.instances.size(); ++i) {
        const std::string owner = shortName(file.instances[i].name);
        name(file.instances[i].iinit, owner + " iinit");
        traits(file.instances[i].traits, owner);
        if (i < file.classes.size()) {
            name(file.classes[i].cinit, owner + " cinit");
            traits(file.classes[i].traits, owner + "$");
        }
    }
    for (size_t i = 0; i < file.scripts.size(); ++i) {
        name(file.scripts[i].init, "script " + std::to_string(i) + " init");
        traits(file.scripts[i].traits, "script " + std::to_string(i));
    }
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i].empty() && file.methods[i].name && file.methods[i].name < file.pool.strings.size()) {
            names[i] = file.pool.strings[file.methods[i].name];
        }
    }
    return names;
}

std::string disassembleBody(const AbcFile& file, const MethodBody& body) {
    std::ostringstream out;
    out << "    maxstack " << body.maxStack << " locals " << body.localCount << " scope " << body.initScopeDepth << "-"
        << body.maxScopeDepth << "\n";
    for (size_t at = 0; at < body.code.size();) {
        Instruction in;
        std::string error;
        if (!decode(body.code, at, in, error)) {
            out << "    " << at << ": <" << error << ">\n";
            break;
        }
        const OpInfo& info = opInfo(in.op);
        out << "    " << at << ": " << info.name;
        if (in.op == OP_lookupswitch) {
            for (int32_t delta : in.targets) out << " " << static_cast<int64_t>(at) + delta;
        } else if (in.op == OP_debug) {
            out << " " << in.debug[0] << " " << in.debug[1] << " " << in.debug[2] << " " << in.debug[3];
        } else {
            for (int i = 0; i < 2; ++i) {
                const uint32_t value = in.operands[i];
                switch (info.operands[i]) {
                    case Operand::None: break;
                    case Operand::Multiname: out << " " << multinameName(file, value); break;
                    case Operand::String:
                        out << " " << (value < file.pool.strings.size() ? quoted(file.pool.strings[value]) : "?");
                        break;
                    case Operand::Int: out << " " << (value < file.pool.ints.size() ? file.pool.ints[value] : 0); break;
                    case Operand::UInt: out << " " << (value < file.pool.uints.size() ? file.pool.uints[value] : 0); break;
                    case Operand::Double: out << " " << (value < file.pool.doubles.size() ? file.pool.doubles[value] : 0); break;
                    case Operand::Namespace: out << " " << namespaceName(file, value); break;
                    case Operand::SignedByte: out << " " << static_cast<int>(static_cast<int8_t>(value)); break;
                    case Operand::Short: out << " " << static_cast<int>(static_cast<int16_t>(value)); break;
                    case Operand::Branch: out << " " << in.targets[0]; break;
                    default: out << " " << value; break;
                }
            }
        }
        out << "\n";
        at += in.length;
    }
    for (const ExceptionInfo& entry : body.exceptions) {
        out << "    try " << entry.from << "-" << entry.to << " -> " << entry.target << " type "
            << multinameName(file, entry.excType) << " name " << multinameName(file, entry.varName) << "\n";
    }
    if (!body.traits.empty()) {
        out << "    activation\n";
        listTraits(out, file, body.traits, "      ");
    }
    return out.str();
}

std::string disassemble(const AbcFile& file) {
    std::ostringstream out;
    const std::vector<std::string> names = methodNames(file);
    std::vector<int> bodyOf(file.methods.size(), -1);
    for (size_t i = 0; i < file.bodies.size(); ++i) {
        if (file.bodies[i].method < bodyOf.size()) bodyOf[file.bodies[i].method] = static_cast<int>(i);
    }
    out << "abc " << file.majorVersion << "." << file.minorVersion << "\n";
    for (size_t i = 0; i < file.instances.size(); ++i) {
        const InstanceInfo& instance = file.instances[i];
        out << "class " << i << " " << multinameName(file, instance.name);
        if (instance.superName) out << " extends " << multinameName(file, instance.superName);
        for (uint32_t face : instance.interfaces) out << " implements " << multinameName(file, face);
        if (instance.flags & INSTANCE_Interface) out << " interface";
        if (instance.flags & INSTANCE_Final) out << " final";
        if (!(instance.flags & INSTANCE_Sealed)) out << " dynamic";
        if (instance.flags & INSTANCE_ProtectedNs) out << " protected " << namespaceName(file, instance.protectedNs);
        out << "\n  iinit " << instance.iinit << "\n";
        listTraits(out, file, instance.traits, "  ");
        if (i < file.classes.size()) {
            out << "  cinit " << file.classes[i].cinit << "\n";
            listTraits(out, file, file.classes[i].traits, "  static ");
        }
    }
    for (size_t i = 0; i < file.scripts.size(); ++i) {
        out << "script " << i << " init " << file.scripts[i].init << "\n";
        listTraits(out, file, file.scripts[i].traits, "  ");
    }
    for (size_t i = 0; i < file.methods.size(); ++i) {
        const MethodInfo& method = file.methods[i];
        out << "method " << i << " " << names[i] << " (";
        for (size_t p = 0; p < method.paramTypes.size(); ++p) {
            if (p) out << ", ";
            out << multinameName(file, method.paramTypes[p]);
        }
        out << "):" << multinameName(file, method.returnType);
        if (method.flags & METHOD_NeedActivation) out << " activation";
        if (method.flags & METHOD_NeedArguments) out << " arguments";
        if (method.flags & METHOD_NeedRest) out << " rest";
        if (!method.options.empty()) out << " defaults " << method.options.size();
        out << "\n";
        if (bodyOf[i] >= 0) out << disassembleBody(file, file.bodies[bodyOf[i]]);
    }
    return out.str();
}

} // namespace abc
} // namespace as3
