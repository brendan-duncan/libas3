#include "abc_domain.h"

#include "abc_opcodes.h"
#include "abc_verifier.h"

#include "../bytecode/vm.h"
#include "../runtime/profile.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <unordered_map>
#include <unordered_set>

namespace as3 {

// What the domain needs of the VM that scripts compiled from source reach
// through their own instructions.
struct VMAccess {
    static bool deleteProperty(VM& vm, const Val& object, const std::string& name) {
        return vm.deleteProp(object, name);
    }
    static bool deleteIndex(VM& vm, const Val& object, const Val& key) { return vm.deleteIndex(object, key); }
    static bool hasKey(VM& vm, const Val& object, const Val& key) { return vm.hasKey(object, key); }
};

namespace abc {

namespace {

// How many method calls deep code of a domain may go before that is a
// stack overflow, besides the VM's own measure of the C++ stack.
constexpr int kMaxDepth = 1800;

// A scope: an object whose properties names are found on, and whether it
// was pushed with pushwith (any property counts, not just declared ones).
struct Scope {
    Val object;
    bool with = false;
};
using ScopeChain = std::vector<Scope>;

// A name as code uses it (a multiname), worked out the first time.
struct Name {
    bool ready = false;
    bool runtimeNs = false;
    bool runtimeName = false;
    bool attribute = false;
    bool any = false;
    // The name, and the key members are found by: the name, or "@name" for
    // an attribute.
    std::string local;
    std::string key;
    // For a private namespace among its namespaces: the key of a member in
    // it ("pkg.Class::name").
    std::string privateKey;
    // The namespaces' URIs, and those of them that are packages (public or
    // internal), the top level last.
    std::vector<std::string> uris;
    std::vector<std::string> packages;
    // The package-qualified names a definition may have ("flash.display.Sprite").
    std::vector<std::string> qualified;
    // TypeName: the generic and its parameters.
    uint32_t base = 0;
    std::vector<uint32_t> parameters;
    // A definition found outside the code's scopes, in the VM: kept, as
    // definitions do not change.
    bool vmFound = false;
    Val vmValue;
};

// An instruction, its branch targets as instruction indexes.
struct Ins {
    uint8_t op = 0;
    uint32_t a = 0;
    uint32_t b = 0;
    int32_t target = -1;
    uint32_t offset = 0;
};

struct Handler {
    uint32_t from = 0;
    uint32_t to = 0;
    uint32_t target = 0;
    uint32_t type = 0;
    uint32_t var = 0;
    std::shared_ptr<as3::ClassInfo> scopeClass;
};

// A method body made ready to run.
struct Code {
    std::vector<Ins> ins;
    std::vector<std::vector<int32_t>> switches;
    std::vector<Handler> handlers;
    uint32_t locals = 1;
    uint32_t maxStack = 1;
    uint32_t maxScope = 1;
    std::shared_ptr<as3::ClassInfo> activation;
};

// The slots of a class's instances (or of an activation, a catch scope, a
// script's global) and of its statics, by slot id.
struct SlotMap {
    std::unordered_map<uint32_t, uint32_t> instance;
    std::unordered_map<uint32_t, uint32_t> statics;
};

struct File;
struct ClassRecord;

// A method as a function value has it: its code, the scopes it was made
// in, and the class it belongs to (for super).
struct MethodRef {
    File* file = nullptr;
    uint32_t method = 0;
    std::shared_ptr<const ScopeChain> scope;
    ClassRecord* owner = nullptr;
    std::string name;
};

struct ClassRecord {
    File* file = nullptr;
    uint32_t index = 0;
    std::shared_ptr<as3::ClassInfo> info;
    Val object;
    std::shared_ptr<const ScopeChain> scope;
};

struct Script {
    File* file = nullptr;
    uint32_t index = 0;
    bool started = false;
    Val global;
    std::shared_ptr<as3::ClassInfo> globalClass;
    // Each trait's key, and its namespace's URI.
    std::unordered_map<std::string, std::string> uriOf;
};

struct File {
    AbcFile abc;
    std::vector<Name> names;
    std::vector<int32_t> bodyOf;
    std::vector<std::unique_ptr<Code>> code;
    // Per namespace: for a class's private one, "pkg.Class::"; for another
    // private one, a prefix of its own.
    std::vector<std::string> privatePrefix;
    std::vector<std::string> methodNames;
    std::vector<std::unique_ptr<ClassRecord>> classes;
    std::vector<std::unique_ptr<Script>> scripts;
    // Each method's parameters' coercions and its result's, worked out
    // the first time it runs.
    std::vector<std::unique_ptr<std::vector<Coerce>>> coercions;
};

// For-in over an object: the keys it had when the loop began.
struct Iteration {
    Value* object = nullptr;
    Val hold;
    Val keys;
};

// Values for frames, taken and given back last first.
class Arena {
public:
    Val* take(size_t count) {
        if (_block < _blocks.size() && _used + count <= _blocks[_block].size) {
            Val* at = _blocks[_block].data.get() + _used;
            _marks.push_back({_block, _used});
            _used += count;
            return at;
        }
        _marks.push_back({_block, _used});
        size_t next = _block + (_blocks.empty() ? 0 : 1);
        while (next < _blocks.size() && _blocks[next].size < count) ++next;
        if (next >= _blocks.size()) {
            const size_t size = std::max<size_t>(count, 16384);
            _blocks.push_back({std::unique_ptr<Val[]>(new Val[size]), size});
            next = _blocks.size() - 1;
        }
        _block = next;
        _used = count;
        return _blocks[_block].data.get();
    }
    void give(Val* at, size_t count) {
        for (size_t i = 0; i < count; ++i) at[i] = Val();
        const Mark mark = _marks.back();
        _marks.pop_back();
        _block = mark.block;
        _used = mark.used;
    }

private:
    struct Block {
        std::unique_ptr<Val[]> data;
        size_t size = 0;
    };
    struct Mark {
        size_t block;
        size_t used;
    };
    std::vector<Block> _blocks;
    std::vector<Mark> _marks;
    size_t _block = 0;
    size_t _used = 0;
};

std::string joinName(const std::string& uri, const std::string& local) {
    return uri.empty() ? local : uri + "." + local;
}

bool isPackage(uint8_t kind) {
    return kind == NS_Package || kind == NS_PackageInternal;
}

} // namespace

// A frame of a method running.
struct Frame {
    File* file = nullptr;
    Code* code = nullptr;
    const MethodRef* method = nullptr;
    Val* regs = nullptr;
    Val* stack = nullptr;
    Val* stackEnd = nullptr;
    Val* sp = nullptr;
    Val* scopes = nullptr;
    uint32_t scopeDepth = 0;
    uint64_t withMask = 0;
    size_t pc = 0;
    int line = 0;
    std::unique_ptr<std::vector<Iteration>> iterations;
};

struct Domain::State : std::enable_shared_from_this<Domain::State> {
    explicit State(VM& vm) : vm(vm) {}

    VM& vm;
    bool dead = false;
    std::vector<std::unique_ptr<File>> files;
    // Each definition by qualified name: the script that has it, and the
    // trait's key on that script's global.
    std::unordered_map<std::string, std::pair<Script*, std::string>> index;
    std::unordered_map<std::string, std::string> shortNames;
    std::vector<std::string> order;
    std::unordered_map<const as3::ClassInfo*, SlotMap> slotMaps;
    std::unordered_map<const as3::ClassInfo*, Script*> globalClasses;
    // What a name not in any scope or script is looked for on.
    Val domainObject;
    Arena arena;
    int depth = 0;
    uint64_t instructions = 0;
    std::string errorStack;
    size_t errorDepth = 0;
    std::vector<const Frame*> running;

    // -- Loading ------------------------------------------------------------

    bool load(AbcFile abc, std::string& error);
    const Name& name(File& file, uint32_t index);
    Code& prepare(File& file, uint32_t body);
    Val constant(File& file, uint8_t kind, uint32_t index);
    Coerce coerceOf(File& file, uint32_t typeName);
    Val defaultFor(File& file, uint32_t typeName);
    std::string keyOf(File& file, uint32_t qname);
    std::string uriOfName(File& file, uint32_t qname);
    std::string qualifiedOf(File& file, uint32_t qname);

    // -- Scripts and classes ---------------------------------------------

    void start(Script& script);
    Val makeMethod(File& file, uint32_t method, std::shared_ptr<const ScopeChain> scope, ClassRecord* owner,
                   const std::string& name);
    Val newClass(Frame& frame, uint32_t index, const Val& base);
    void addTraits(File& file, const std::vector<Trait>& traits, as3::ClassInfo& cls, bool statics, SlotMap& slots,
                   const std::shared_ptr<const ScopeChain>& scope, ClassRecord* owner, Val* staticHolder);
    std::shared_ptr<as3::ClassInfo> slotClass(File& file, const std::vector<Trait>& traits, const std::string& name);
    bool provide(const std::string& qualifiedName, Val& value);

    // -- Running --------------------------------------------------------------

    Val invoke(const MethodRef& method, const Val& self, Args args);
    Val run(Frame& frame);
    Val loop(Frame& frame);
    bool handle(Frame& frame, const ScriptException& exception);
    void noteUnwind(const Frame& frame, const ScriptException* exception);

    // -- Names ------------------------------------------------------------------

    std::string runtimeKey(const Name& n, const Val& name);
    bool scopeHas(const Val& object, bool with, const Name& n, const std::string& key, const std::string* priv);
    bool findInDomain(const Name& n, const std::string& local, Val& holder, Val* value);
    Val findProperty(Frame& frame, const Name& n, const std::string& key, bool strict);
    Val getProperty(const Val& object, const Name& n, const std::string& key, const Val* runtimeName);
    void setProperty(const Val& object, const Name& n, const std::string& key, const Val* runtimeName,
                     const Val& value);
    Val callProperty(const Val& object, const Name& n, const std::string& key, const Val* runtimeName, Args args,
                     bool lex);
    const as3::Trait* superTrait(Frame& frame, const std::string& key);
    Val resolveType(File& file, uint32_t multiname);
    Val slotOf(const Val& object, uint32_t slot, Val** where);
    Iteration& iteration(Frame& frame, const Val& object, bool fresh);
    Val globalOf(Frame& frame);
    [[noreturn]] void referenceError(const std::string& name);
};

namespace {

std::string nsUri(const AbcFile& file, uint32_t ns) {
    if (ns == 0 || ns >= file.pool.namespaces.size()) return std::string();
    const uint32_t name = file.pool.namespaces[ns].name;
    return name < file.pool.strings.size() ? file.pool.strings[name] : std::string();
}

uint8_t nsKind(const AbcFile& file, uint32_t ns) {
    if (ns == 0 || ns >= file.pool.namespaces.size()) return NS_Package;
    return file.pool.namespaces[ns].kind;
}

const std::string& poolString(const AbcFile& file, uint32_t index) {
    static const std::string empty;
    return index < file.pool.strings.size() ? file.pool.strings[index] : empty;
}

} // namespace

// -- Loading ----------------------------------------------------------------

bool Domain::State::load(AbcFile abc, std::string& error) {
    auto file = std::make_unique<File>();
    file->abc = std::move(abc);
    const AbcFile& a = file->abc;
    file->names.resize(a.pool.multinames.size());
    file->bodyOf.assign(a.methods.size(), -1);
    for (size_t i = 0; i < a.bodies.size(); ++i) {
        const uint32_t method = a.bodies[i].method;
        if (method >= a.methods.size()) {
            error = "a method body is for method " + std::to_string(method) + ", which there is not";
            return false;
        }
        file->bodyOf[method] = static_cast<int32_t>(i);
    }
    file->code.resize(a.bodies.size());
    file->coercions.resize(a.methods.size());
    file->methodNames = methodNames(a);
    if (a.instances.size() != a.classes.size()) {
        error = "the file has " + std::to_string(a.instances.size()) + " instances for " +
                std::to_string(a.classes.size()) + " classes";
        return false;
    }

    // Private namespaces: a class's (the namespace its private members
    // are in) by the class's name; others by their place in the pool.
    file->privatePrefix.resize(a.pool.namespaces.size());
    for (size_t i = 0; i < a.pool.namespaces.size(); ++i) {
        if (a.pool.namespaces[i].kind == NS_Private) {
            file->privatePrefix[i] = "private#" + std::to_string(files.size()) + "." + std::to_string(i) + "::";
        }
    }
    const auto claim = [&](const std::vector<Trait>& traits, const std::string& className) {
        for (const Trait& trait : traits) {
            if (trait.name == 0 || trait.name >= a.pool.multinames.size()) continue;
            const uint32_t ns = a.pool.multinames[trait.name].ns;
            if (ns < a.pool.namespaces.size() && a.pool.namespaces[ns].kind == NS_Private &&
                file->privatePrefix[ns].rfind("private#", 0) == 0) {
                file->privatePrefix[ns] = className + "::";
            }
        }
    };
    for (size_t i = 0; i < a.instances.size(); ++i) {
        const std::string className = qualifiedOf(*file, a.instances[i].name);
        claim(a.instances[i].traits, className);
        claim(a.classes[i].traits, className);
    }
    file->classes.resize(a.classes.size());

    // Scripts: each definition by its qualified name.
    File* at = file.get();
    files.push_back(std::move(file));
    for (size_t i = 0; i < a.scripts.size(); ++i) {
        auto script = std::make_unique<Script>();
        script->file = at;
        script->index = static_cast<uint32_t>(i);
        for (const Trait& trait : a.scripts[i].traits) {
            const std::string key = keyOf(*at, trait.name);
            script->uriOf[key] = uriOfName(*at, trait.name);
            if (trait.name >= a.pool.multinames.size()) continue;
            const Multiname& m = a.pool.multinames[trait.name];
            if (!isPackage(nsKind(a, m.ns))) continue;
            const std::string qualified = joinName(nsUri(a, m.ns), poolString(a, m.name));
            if (index.count(qualified)) continue;
            index[qualified] = {script.get(), key};
            order.push_back(qualified);
            const std::string& local = poolString(a, m.name);
            auto shortName = shortNames.find(local);
            if (shortName == shortNames.end()) {
                shortNames[local] = qualified;
            } else if (shortName->second != qualified) {
                shortName->second.clear();
            }
        }
        at->scripts.push_back(std::move(script));
    }
    return true;
}

std::string Domain::State::keyOf(File& file, uint32_t qname) {
    const AbcFile& a = file.abc;
    if (qname == 0 || qname >= a.pool.multinames.size()) return std::string();
    const Multiname& m = a.pool.multinames[qname];
    const std::string& local = poolString(a, m.name);
    if (m.ns < a.pool.namespaces.size() && a.pool.namespaces[m.ns].kind == NS_Private) {
        return file.privatePrefix[m.ns] + local;
    }
    return m.isAttribute() ? "@" + local : local;
}

std::string Domain::State::uriOfName(File& file, uint32_t qname) {
    const AbcFile& a = file.abc;
    if (qname == 0 || qname >= a.pool.multinames.size()) return std::string();
    return nsUri(a, a.pool.multinames[qname].ns);
}

std::string Domain::State::qualifiedOf(File& file, uint32_t qname) {
    const AbcFile& a = file.abc;
    if (qname == 0 || qname >= a.pool.multinames.size()) return "*";
    const Multiname& m = a.pool.multinames[qname];
    const std::string& local = poolString(a, m.name);
    if (!isPackage(nsKind(a, m.ns))) return local;
    return joinName(nsUri(a, m.ns), local);
}

const Name& Domain::State::name(File& file, uint32_t index) {
    static Name none;
    if (index >= file.names.size()) {
        none.ready = true;
        none.any = true;
        return none;
    }
    Name& n = file.names[index];
    if (n.ready) return n;
    n.ready = true;
    const AbcFile& a = file.abc;
    const Multiname& m = a.pool.multinames[index];
    n.attribute = m.isAttribute();
    const auto addNamespace = [&](uint32_t ns) {
        const std::string uri = nsUri(a, ns);
        const uint8_t kind = nsKind(a, ns);
        n.uris.push_back(uri);
        if (kind == NS_Private && n.privateKey.empty() && ns < file.privatePrefix.size()) {
            n.privateKey = file.privatePrefix[ns];
        } else if (isPackage(kind)) {
            n.packages.push_back(uri);
        }
    };
    switch (m.kind) {
        case MN_QName:
        case MN_QNameA:
            addNamespace(m.ns);
            n.local = poolString(a, m.name);
            n.any = m.name == 0;
            break;
        case MN_RTQName:
        case MN_RTQNameA:
            n.runtimeNs = true;
            n.local = poolString(a, m.name);
            break;
        case MN_RTQNameL:
        case MN_RTQNameLA:
            n.runtimeNs = true;
            n.runtimeName = true;
            break;
        case MN_Multiname:
        case MN_MultinameA:
            if (m.nsSet < a.pool.nsSets.size()) {
                for (uint32_t ns : a.pool.nsSets[m.nsSet].namespaces) addNamespace(ns);
            }
            n.local = poolString(a, m.name);
            n.any = m.name == 0;
            break;
        case MN_MultinameL:
        case MN_MultinameLA:
            n.runtimeName = true;
            if (m.nsSet < a.pool.nsSets.size()) {
                for (uint32_t ns : a.pool.nsSets[m.nsSet].namespaces) addNamespace(ns);
            }
            break;
        case MN_TypeName:
            n.base = m.base;
            n.parameters = m.parameters;
            if (m.base < a.pool.multinames.size()) {
                const Name& base = name(file, m.base);
                Name& self = file.names[index];
                self.local = base.local;
                self.uris = base.uris;
                self.packages = base.packages;
            }
            break;
    }
    Name& self = file.names[index];
    // The top level last: a name in a package found before an alias of it.
    std::stable_partition(self.packages.begin(), self.packages.end(), [](const std::string& uri) { return !uri.empty(); });
    if (!self.privateKey.empty()) self.privateKey += self.local;
    self.key = self.attribute ? "@" + self.local : self.local;
    if (!self.runtimeName) {
        for (const std::string& uri : self.packages) self.qualified.push_back(joinName(uri, self.local));
    }
    return self;
}

Code& Domain::State::prepare(File& file, uint32_t bodyIndex) {
    std::unique_ptr<Code>& made = file.code[bodyIndex];
    if (made) return *made;
    const MethodBody& body = file.abc.bodies[bodyIndex];
    std::vector<Instruction> decoded;
    std::string error;
    if (!decodeAll(body.code, decoded, error)) {
        vm.throwError("VerifyError", "Error #1011: Method " + file.methodNames[body.method] +
                                         " contained an illegal opcode (" + error + ").", 1011);
    }
    auto code = std::make_unique<Code>();
    code->locals = std::max<uint32_t>(body.localCount, 1);
    code->maxStack = body.maxStack + 2;
    code->maxScope = std::max<uint32_t>(body.maxScopeDepth > body.initScopeDepth ? body.maxScopeDepth - body.initScopeDepth : 0, 1) + 1;
    std::vector<int32_t> at(body.code.size() + 1, -1);
    for (size_t i = 0; i < decoded.size(); ++i) at[decoded[i].offset] = static_cast<int32_t>(i);
    at[body.code.size()] = static_cast<int32_t>(decoded.size());
    const auto indexOf = [&](int64_t offset) -> int32_t {
        if (offset < 0 || offset > static_cast<int64_t>(body.code.size()) || at[offset] < 0) {
            vm.throwError("VerifyError", "Error #1021: At least one branch target was not on a valid instruction in " +
                                             file.methodNames[body.method] + ".", 1021);
        }
        return at[offset];
    };
    // An offset as an instruction: the first at or after it.
    const auto nearest = [&](uint32_t offset) -> uint32_t {
        for (size_t o = std::min<size_t>(offset, body.code.size()); o <= body.code.size(); ++o) {
            if (at[o] >= 0) return static_cast<uint32_t>(at[o]);
        }
        return static_cast<uint32_t>(decoded.size());
    };
    code->ins.reserve(decoded.size() + 1);
    for (const Instruction& in : decoded) {
        Ins out;
        out.op = in.op;
        out.a = in.operands[0];
        out.b = in.operands[1];
        out.offset = static_cast<uint32_t>(in.offset);
        if (in.op == OP_lookupswitch) {
            std::vector<int32_t> targets;
            for (int32_t delta : in.targets) targets.push_back(indexOf(static_cast<int64_t>(in.offset) + delta));
            out.a = static_cast<uint32_t>(code->switches.size());
            code->switches.push_back(std::move(targets));
        } else if (!in.targets.empty()) {
            out.target = indexOf(in.targets[0]);
        }
        code->ins.push_back(out);
    }
    // Falling off the end returns.
    Ins end;
    end.op = OP_returnvoid;
    end.offset = static_cast<uint32_t>(body.code.size());
    code->ins.push_back(end);
    for (const ExceptionInfo& exception : body.exceptions) {
        Handler handler;
        handler.from = nearest(exception.from);
        handler.to = nearest(exception.to);
        handler.target = static_cast<uint32_t>(indexOf(exception.target));
        handler.type = exception.excType;
        handler.var = exception.varName;
        code->handlers.push_back(handler);
    }
    made = std::move(code);
    return *made;
}

Val Domain::State::constant(File& file, uint8_t kind, uint32_t index) {
    const ConstantPool& pool = file.abc.pool;
    switch (kind) {
        case CONSTANT_Int:
            return Val::number(index < pool.ints.size() ? pool.ints[index] : 0);
        case CONSTANT_UInt:
            return Val::number(index < pool.uints.size() ? pool.uints[index] : 0);
        case CONSTANT_Double:
            return Val::number(index < pool.doubles.size() ? pool.doubles[index] : 0);
        case CONSTANT_Utf8:
            return Val::string(poolString(file.abc, index));
        case CONSTANT_True:
            return Val::boolean(true);
        case CONSTANT_False:
            return Val::boolean(false);
        case CONSTANT_Null:
            return Val::null();
        case CONSTANT_Undefined:
            return Val();
        default:
            // A namespace: its URI.
            return Val::string(nsUri(file.abc, index));
    }
}

Coerce Domain::State::coerceOf(File& file, uint32_t typeName) {
    if (typeName == 0 || typeName >= file.abc.pool.multinames.size()) return Coerce::None;
    const Multiname& m = file.abc.pool.multinames[typeName];
    if (m.kind == MN_TypeName) return Coerce::Nullable;
    const std::string& local = poolString(file.abc, m.name);
    if (nsUri(file.abc, m.ns).empty()) {
        if (local == "int") return Coerce::Int;
        if (local == "uint") return Coerce::Uint;
        if (local == "Number") return Coerce::Number;
        if (local == "String") return Coerce::String;
        if (local == "Boolean") return Coerce::Boolean;
        if (local == "void" || local == "*") return Coerce::None;
    }
    return Coerce::Nullable;
}

Val Domain::State::defaultFor(File& file, uint32_t typeName) {
    return defaultForCoerce(coerceOf(file, typeName));
}

// -- Scripts and classes ------------------------------------------------------

std::shared_ptr<as3::ClassInfo> Domain::State::slotClass(File& file, const std::vector<Trait>& traits,
                                                    const std::string& className) {
    auto cls = std::make_shared<as3::ClassInfo>();
    cls->name = className;
    cls->shortName = className;
    cls->isDynamic = true;
    cls->base = nullptr;
    SlotMap& slots = slotMaps[cls.get()];
    uint32_t next = 1;
    for (const Trait& trait : traits) {
        if (trait.kind != TRAIT_Slot && trait.kind != TRAIT_Const) continue;
        as3::Trait made;
        made.kind = as3::TraitKind::Slot;
        made.isConst = trait.kind == TRAIT_Const;
        made.coerce = coerceOf(file, trait.typeName);
        made.slot = static_cast<uint32_t>(cls->slotDefaults.size());
        made.declaredBy = cls.get();
        cls->slotDefaults.push_back(trait.valueKind || trait.valueIndex ? constant(file, trait.valueKind, trait.valueIndex)
                                                                        : defaultFor(file, trait.typeName));
        const uint32_t id = trait.slotId ? trait.slotId : next;
        next = std::max(next, id) + 1;
        slots.instance[id] = made.slot;
        cls->traits[keyOf(file, trait.name)] = made;
    }
    return cls;
}

void Domain::State::addTraits(File& file, const std::vector<Trait>& traits, as3::ClassInfo& cls, bool statics,
                              SlotMap& slots, const std::shared_ptr<const ScopeChain>& scope, ClassRecord* owner,
                              Val* staticHolder) {
    auto& table = statics ? cls.statics : cls.traits;
    auto& values = statics ? cls.staticSlots : cls.slotDefaults;
    auto& ids = statics ? slots.statics : slots.instance;
    uint32_t next = 1;
    for (const auto& entry : ids) next = std::max(next, entry.first + 1);
    const std::string prefix = cls.name + "/";
    for (const Trait& trait : traits) {
        const std::string key = keyOf(file, trait.name);
        const uint32_t nameIndex =
            trait.name < file.abc.pool.multinames.size() ? file.abc.pool.multinames[trait.name].name : 0;
        const std::string& local = poolString(file.abc, nameIndex);
        switch (trait.kind) {
            case TRAIT_Slot:
            case TRAIT_Const:
            case TRAIT_Class:
            case TRAIT_Function: {
                as3::Trait made;
                made.kind = as3::TraitKind::Slot;
                made.isConst = trait.kind == TRAIT_Const;
                made.declaredBy = &cls;
                made.isPublic = nsKind(file.abc, file.abc.pool.multinames[trait.name].ns) == NS_Package;
                Val initial;
                if (trait.kind == TRAIT_Slot || trait.kind == TRAIT_Const) {
                    made.coerce = coerceOf(file, trait.typeName);
                    made.typeName = qualifiedOf(file, trait.typeName);
                    initial = trait.valueKind || trait.valueIndex ? constant(file, trait.valueKind, trait.valueIndex)
                                                                  : defaultFor(file, trait.typeName);
                    if (made.coerce != Coerce::None) initial = coerceValue(initial, made.coerce);
                } else if (trait.kind == TRAIT_Function) {
                    initial = makeMethod(file, trait.index, scope, owner, prefix + local);
                } else {
                    initial = Val::null();
                }
                made.slot = static_cast<uint32_t>(values.size());
                values.push_back(initial);
                const uint32_t id = trait.slotId ? trait.slotId : next;
                next = std::max(next, id) + 1;
                ids[id] = made.slot;
                table[key] = made;
                break;
            }
            case TRAIT_Method: {
                if (cls.isInterface) break;
                as3::Trait made;
                made.kind = as3::TraitKind::Method;
                made.isFinal = (trait.attributes & ATTR_Final) != 0;
                made.declaredBy = &cls;
                made.isPublic = nsKind(file.abc, file.abc.pool.multinames[trait.name].ns) == NS_Package;
                made.method = makeMethod(file, trait.index, scope, owner, prefix + local);
                table[key] = made;
                break;
            }
            case TRAIT_Getter:
            case TRAIT_Setter: {
                if (cls.isInterface) break;
                as3::Trait made;
                auto existing = table.find(key);
                if (existing != table.end() && existing->second.kind == as3::TraitKind::Accessor) made = existing->second;
                made.kind = as3::TraitKind::Accessor;
                made.declaredBy = &cls;
                made.isPublic = nsKind(file.abc, file.abc.pool.multinames[trait.name].ns) == NS_Package;
                Val function = makeMethod(file, trait.index, scope, owner,
                                          prefix + (trait.kind == TRAIT_Getter ? "get " : "set ") + local);
                if (trait.kind == TRAIT_Getter) {
                    made.getter = function;
                    made.typeName = qualifiedOf(file, file.abc.methods[trait.index].returnType);
                } else {
                    made.setter = function;
                }
                table[key] = made;
                break;
            }
            default:
                break;
        }
    }
    (void)staticHolder;
}

Val Domain::State::makeMethod(File& file, uint32_t method, std::shared_ptr<const ScopeChain> scope,
                              ClassRecord* owner, const std::string& name) {
    auto ref = std::make_shared<MethodRef>();
    ref->file = &file;
    ref->method = method;
    ref->scope = std::move(scope);
    ref->owner = owner;
    ref->name = name;
    const int length = method < file.abc.methods.size() ? static_cast<int>(file.abc.methods[method].paramTypes.size()) : 0;
    std::weak_ptr<State> weak = shared_from_this();
    std::string shortName = name.substr(name.find_last_of("/:") == std::string::npos ? 0 : name.find_last_of("/:") + 1);
    return vm.makeFunction(shortName,
                           [weak, ref](VM& vm, const Val& self, Args args) -> Val {
                               std::shared_ptr<State> state = weak.lock();
                               if (!state || state->dead) {
                                   vm.throwError("Error", "The ABC this function came from is no longer loaded.");
                               }
                               return state->invoke(*ref, self, args);
                           },
                           length);
}

void Domain::State::start(Script& script) {
    if (script.started) return;
    script.started = true;
    File& file = *script.file;
    const ScriptInfo& info = file.abc.scripts[script.index];
    // Its global: a slot for each of its definitions.
    auto cls = std::make_shared<as3::ClassInfo>();
    cls->name = "global";
    cls->shortName = "global";
    cls->isDynamic = true;
    cls->base = vm.builtins().object;
    globalClasses[cls.get()] = &script;
    script.globalClass = cls;
    auto object = std::make_shared<Value>();
    object->type = ValueType::Object;
    object->cls = cls;
    vm.track(object);
    script.global = Val(object);
    auto scope = std::make_shared<ScopeChain>();
    scope->push_back({script.global, false});
    addTraits(file, info.traits, *cls, false, slotMaps[cls.get()], scope, nullptr, nullptr);
    object->slots = cls->slotDefaults;

    MethodRef init;
    init.file = &file;
    init.method = info.init;
    init.scope = std::make_shared<ScopeChain>();
    init.name = "script" + std::to_string(script.index) + "$init";
    invoke(init, script.global, Args());

    // Its classes and functions, defined for the VM's scripts and host.
    for (const Trait& trait : info.traits) {
        if (trait.kind != TRAIT_Class && trait.kind != TRAIT_Function && trait.kind != TRAIT_Method &&
            trait.kind != TRAIT_Const) {
            continue;
        }
        const Multiname& m = file.abc.pool.multinames[trait.name];
        if (!isPackage(nsKind(file.abc, m.ns))) continue;
        Val value = vm.getProperty(script.global, keyOf(file, trait.name));
        if (!value.isNullish()) vm.registerDefinition(qualifiedOf(file, trait.name), value);
    }
}

bool Domain::State::provide(const std::string& qualifiedName, Val& value) {
    if (dead) return false;
    std::string name = qualifiedName;
    auto found = index.find(name);
    if (found == index.end()) {
        auto shortName = shortNames.find(name);
        if (shortName == shortNames.end() || shortName->second.empty()) return false;
        found = index.find(shortName->second);
        if (found == index.end()) return false;
    }
    Script& script = *found->second.first;
    start(script);
    Val got = vm.getProperty(script.global, found->second.second);
    if (got.isNullish()) return false;
    value = got;
    return true;
}

Val Domain::State::newClass(Frame& frame, uint32_t index, const Val& base) {
    File& file = *frame.file;
    if (index >= file.abc.instances.size()) vm.throwError("VerifyError", "Error #1060: Class index out of range.", 1060);
    const InstanceInfo& instance = file.abc.instances[index];
    const as3::ClassInfo* baseInfo = base.isClass() ? base->cls.get() : nullptr;
    auto record = std::make_unique<ClassRecord>();
    record->file = &file;
    record->index = index;
    auto cls = std::make_shared<as3::ClassInfo>();
    const Multiname& m = file.abc.pool.multinames[instance.name];
    cls->name = qualifiedOf(file, instance.name);
    cls->shortName = poolString(file.abc, m.name);
    cls->packageName = isPackage(nsKind(file.abc, m.ns)) ? nsUri(file.abc, m.ns) : std::string();
    cls->isInterface = (instance.flags & INSTANCE_Interface) != 0;
    cls->isDynamic = (instance.flags & INSTANCE_Sealed) == 0;
    cls->isFinal = (instance.flags & INSTANCE_Final) != 0;
    if (!cls->isInterface) {
        cls->base = baseInfo ? base->cls : vm.builtins().object;
    }
    if (cls->base) {
        cls->traits = cls->base->traits;
        cls->slotDefaults = cls->base->slotDefaults;
        cls->instanceType = cls->base->instanceType;
    }
    for (uint32_t face : instance.interfaces) {
        Val found = resolveType(file, face);
        if (found.isClass()) cls->interfaces.push_back(found->cls);
    }
    auto object = std::make_shared<Value>();
    object->type = ValueType::Class;
    object->cls = cls;
    cls->classObject = object;
    vm.track(object);
    record->info = cls;
    record->object = Val(object);

    // Its methods' scopes: those it was made in, and the class.
    auto scope = std::make_shared<ScopeChain>();
    if (frame.method && frame.method->scope) *scope = *frame.method->scope;
    for (uint32_t i = 0; i < frame.scopeDepth; ++i) {
        scope->push_back({frame.scopes[i], ((frame.withMask >> std::min<uint32_t>(i, 63)) & 1) != 0});
    }
    scope->push_back({record->object, false});
    record->scope = scope;

    SlotMap& slots = slotMaps[cls.get()];
    if (baseInfo) {
        auto inherited = slotMaps.find(baseInfo);
        if (inherited != slotMaps.end()) slots.instance = inherited->second.instance;
    }
    ClassRecord* owner = record.get();
    addTraits(file, instance.traits, *cls, false, slots, scope, owner, nullptr);
    addTraits(file, file.abc.classes[index].traits, *cls, true, slots, scope, owner, nullptr);

    // Its constructor: the instance initializer, run as a native class's
    // constructor is (by new, a subclass's super() or the host).
    if (!cls->isInterface) {
        std::weak_ptr<State> weak = shared_from_this();
        auto ref = std::make_shared<MethodRef>();
        ref->file = &file;
        ref->method = instance.iinit;
        ref->scope = scope;
        ref->owner = owner;
        ref->name = cls->name + "()";
        cls->nativeConstructor = [weak, ref](VM& vm, const Val& self, Args args) -> Val {
            std::shared_ptr<State> state = weak.lock();
            if (!state || state->dead) {
                vm.throwError("Error", "The ABC this class came from is no longer loaded.");
            }
            state->invoke(*ref, self, args);
            return Val();
        };
    }
    if (file.classes[index]) {
        vm.throwError("VerifyError", "Error #1131: The class " + cls->name + " is defined twice.", 1131);
    }
    file.classes[index] = std::move(record);
    if (isPackage(nsKind(file.abc, m.ns))) {
        vm.registerDefinition(cls->name, Val(object));
    }

    // The class initializer.
    MethodRef cinit;
    cinit.file = &file;
    cinit.method = file.abc.classes[index].cinit;
    cinit.scope = scope;
    cinit.owner = owner;
    cinit.name = cls->name + "$cinit";
    invoke(cinit, Val(object), Args());
    return Val(object);
}

// -- Names --------------------------------------------------------------------

std::string Domain::State::runtimeKey(const Name& n, const Val& name) {
    const std::string text = name.isString() ? name.str() : vm.toString(name);
    return n.attribute ? "@" + text : text;
}

bool Domain::State::scopeHas(const Val& object, bool with, const Name& n, const std::string& key,
                             const std::string* priv) {
    if (!object.isObject()) return false;
    Value* self = object.ref.get();
    if (with) return VMAccess::hasKey(vm, object, Val::string(key));
    if (self->type == ValueType::Class) {
        const as3::ClassInfo* cls = self->cls.get();
        if (!cls) return false;
        if (priv && cls->findStatic(*priv)) return true;
        return cls->findStatic(key) != nullptr;
    }
    const as3::ClassInfo* cls = self->cls.get();
    auto global = cls ? globalClasses.find(cls) : globalClasses.end();
    if (global != globalClasses.end()) {
        if (priv && cls->findTrait(*priv)) return true;
        if (cls->findTrait(key)) {
            // A script's definition, in a namespace the name has.
            if (n.uris.empty() || n.runtimeNs) return true;
            auto uri = global->second->uriOf.find(key);
            if (uri == global->second->uriOf.end()) return true;
            for (const std::string& has : n.uris) {
                if (has == uri->second) return true;
            }
            return false;
        }
        return self->properties.count(key) > 0;
    }
    const as3::ClassInfo* of = vm.classOf(object);
    if (of) {
        if (priv && of->findTrait(*priv)) return true;
        if (of->findTrait(key)) return true;
    }
    if (self->properties.count(key)) return true;
    for (const as3::ClassInfo* at = of; at; at = at->base.get()) {
        if (at->prototype.count(key)) return true;
    }
    return false;
}

bool Domain::State::findInDomain(const Name& n, const std::string& local, Val& holder, Val* value) {
    std::vector<std::string> made;
    const std::vector<std::string>* names = &n.qualified;
    if (n.runtimeName || n.qualified.empty()) {
        if (n.packages.empty() && n.uris.empty()) {
            made.push_back(local);
        } else {
            for (const std::string& uri : n.packages) made.push_back(joinName(uri, local));
        }
        names = &made;
    }
    // A script of the domain's (run first if need be).
    for (const std::string& qualified : *names) {
        auto found = index.find(qualified);
        if (found == index.end()) continue;
        Script& script = *found->second.first;
        start(script);
        holder = script.global;
        if (value) *value = vm.getProperty(script.global, found->second.second);
        return true;
    }
    // One of the VM's: its own classes, the host's, then what the class
    // resolver finds.
    if (n.vmFound && names == &n.qualified) {
        holder = domainObject;
        if (value) *value = n.vmValue;
        return true;
    }
    // The global object's constants.
    for (const std::string& qualified : *names) {
        Val constant;
        if (qualified == "undefined") {
            constant = Val();
        } else if (qualified == "NaN") {
            constant = Val::number(std::nan(""));
        } else if (qualified == "Infinity") {
            constant = Val::number(HUGE_VAL);
        } else {
            continue;
        }
        holder = domainObject;
        if (value) *value = constant;
        if (names == &n.qualified) {
            Name& kept = const_cast<Name&>(n);
            kept.vmFound = true;
            kept.vmValue = constant;
        }
        return true;
    }
    for (int pass = 0; pass < 2; ++pass) {
        for (const std::string& qualified : *names) {
            Val found = vm.lookupQualified(qualified, pass == 1);
            if (found.isUndefined()) continue;
            if (names == &n.qualified) {
                Name& kept = const_cast<Name&>(n);
                kept.vmFound = true;
                kept.vmValue = found;
            }
            holder = domainObject;
            if (value) *value = found;
            return true;
        }
    }
    return false;
}

Val Domain::State::globalOf(Frame& frame) {
    if (frame.method && frame.method->scope && !frame.method->scope->empty()) {
        return frame.method->scope->front().object;
    }
    if (frame.scopeDepth > 0) return frame.scopes[0];
    return frame.regs[0];
}

Val Domain::State::findProperty(Frame& frame, const Name& n, const std::string& key, bool strict) {
    const std::string* priv = n.privateKey.empty() ? nullptr : &n.privateKey;
    for (uint32_t i = frame.scopeDepth; i-- > 0;) {
        const bool with = ((frame.withMask >> std::min<uint32_t>(i, 63)) & 1) != 0;
        if (scopeHas(frame.scopes[i], with, n, key, priv)) return frame.scopes[i];
    }
    if (frame.method && frame.method->scope) {
        const ScopeChain& chain = *frame.method->scope;
        for (size_t i = chain.size(); i-- > 0;) {
            if (scopeHas(chain[i].object, chain[i].with, n, key, priv)) return chain[i].object;
        }
    }
    Val holder;
    if (findInDomain(n, n.runtimeName ? key : n.local, holder, nullptr)) return holder;
    if (strict) referenceError(key);
    return globalOf(frame);
}

void Domain::State::referenceError(const std::string& name) {
    vm.throwError("ReferenceError", "Error #1065: Variable " + name + " is not defined.", 1065);
}

Val Domain::State::getProperty(const Val& object, const Name& n, const std::string& key, const Val* runtimeName) {
    if (object.isObject()) {
        Value* self = object.ref.get();
        if (self == domainObject.get()) {
            Val holder, value;
            if (findInDomain(n, n.runtimeName ? key : n.local, holder, &value)) return value;
            referenceError(key);
        }
        if (!n.privateKey.empty()) {
            const bool isClass = self->type == ValueType::Class;
            const as3::ClassInfo* cls = isClass ? self->cls.get() : vm.classOf(object);
            const as3::Trait* trait = cls ? (isClass ? cls->findStatic(n.privateKey) : cls->findTrait(n.privateKey)) : nullptr;
            if (trait) return vm.readTrait(object, *trait, isClass);
        }
    }
    if (runtimeName && !runtimeName->isString() && !n.attribute) return vm.getIndex(object, *runtimeName);
    if (runtimeName) return vm.getIndex(object, Val::string(key));
    return vm.getProperty(object, key);
}

void Domain::State::setProperty(const Val& object, const Name& n, const std::string& key, const Val* runtimeName,
                                const Val& value) {
    if (object.isObject() && !n.privateKey.empty()) {
        Value* self = object.ref.get();
        const bool isClass = self->type == ValueType::Class;
        const as3::ClassInfo* cls = isClass ? self->cls.get() : vm.classOf(object);
        const as3::Trait* trait = cls ? (isClass ? cls->findStatic(n.privateKey) : cls->findTrait(n.privateKey)) : nullptr;
        if (trait) {
            vm.writeTrait(object, *trait, value, key, isClass);
            return;
        }
    }
    if (runtimeName && !runtimeName->isString() && !n.attribute) {
        vm.setIndex(object, *runtimeName, value);
        return;
    }
    if (runtimeName) {
        vm.setIndex(object, Val::string(key), value);
        return;
    }
    vm.setProperty(object, key, value);
}

Val Domain::State::callProperty(const Val& object, const Name& n, const std::string& key, const Val* runtimeName,
                                Args args, bool lex) {
    if (object.isNullish()) {
        vm.throwError("TypeError", "Error #1009: Cannot access a property or method of a null object reference.", 1009);
    }
    Val function;
    Val receiver = lex ? Val::null() : object;
    bool found = false;
    if (object.isObject()) {
        Value* self = object.ref.get();
        const bool isClass = self->type == ValueType::Class;
        const as3::ClassInfo* cls = isClass ? self->cls.get() : vm.classOf(object);
        if (self == domainObject.get()) {
            function = getProperty(object, n, key, runtimeName);
            receiver = Val::null();
            found = true;
        } else if (cls) {
            const as3::Trait* trait = nullptr;
            if (!n.privateKey.empty()) trait = isClass ? cls->findStatic(n.privateKey) : cls->findTrait(n.privateKey);
            if (!trait && !runtimeName) trait = isClass ? cls->findStatic(key) : cls->findTrait(key);
            if (trait) {
                function = trait->kind == as3::TraitKind::Method ? trait->method : vm.readTrait(object, *trait, isClass);
                found = true;
            }
        }
    }
    if (!found) function = getProperty(object, n, key, runtimeName);
    if (!function.isFunction() && !function.isClass()) {
        vm.throwError("TypeError", "Error #1006: " + key + " is not a function.", 1006);
    }
    return vm.callFunction(function, receiver, args);
}

const as3::Trait* Domain::State::superTrait(Frame& frame, const std::string& key) {
    const ClassRecord* owner = frame.method ? frame.method->owner : nullptr;
    const as3::ClassInfo* base = owner && owner->info ? owner->info->base.get() : nullptr;
    if (!base) vm.throwError("ReferenceError", "Error #1070: Method " + key + " not found on the superclass.", 1070);
    const as3::Trait* trait = base->findTrait(key);
    if (!trait) {
        vm.throwError("ReferenceError", "Error #1070: Method " + key + " not found on " + base->name + ".", 1070);
    }
    return trait;
}

Val Domain::State::resolveType(File& file, uint32_t multiname) {
    if (multiname == 0) return Val::null();
    const Name& n = name(file, multiname);
    if (multiname < file.abc.pool.multinames.size() && file.abc.pool.multinames[multiname].kind == MN_TypeName) {
        Val base = resolveType(file, n.base);
        std::vector<Val> types;
        for (uint32_t parameter : n.parameters) types.push_back(resolveType(file, parameter));
        return vm.applyType(base, types);
    }
    if (n.local == "*") return Val::null();
    Val holder, value;
    if (findInDomain(n, n.local, holder, &value)) return value;
    // A class of a script's own, not in a package.
    for (const auto& script : file.scripts) {
        if (!script->started) continue;
        const as3::ClassInfo* cls = script->globalClass.get();
        if (cls && cls->findTrait(n.key)) return vm.getProperty(script->global, n.key);
    }
    return Val();
}

Val Domain::State::slotOf(const Val& object, uint32_t slot, Val** where) {
    if (object.isNullish()) {
        vm.throwError("TypeError", "Error #1009: Cannot access a property or method of a null object reference.", 1009);
    }
    if (object.isObject()) {
        Value* self = object.ref.get();
        const bool isClass = self->type == ValueType::Class;
        as3::ClassInfo* cls = self->cls.get();
        auto found = cls ? slotMaps.find(cls) : slotMaps.end();
        if (found != slotMaps.end()) {
            const auto& ids = isClass ? found->second.statics : found->second.instance;
            auto id = ids.find(slot);
            if (id != ids.end()) {
                std::vector<Val>& slots = isClass ? cls->staticSlots : self->slots;
                if (id->second < slots.size()) {
                    *where = &slots[id->second];
                    return slots[id->second];
                }
            }
        }
    }
    vm.throwError("ReferenceError", "Error #1069: Slot " + std::to_string(slot) + " is not there.", 1069);
}

Iteration& Domain::State::iteration(Frame& frame, const Val& object, bool fresh) {
    if (!frame.iterations) frame.iterations = std::make_unique<std::vector<Iteration>>();
    for (Iteration& at : *frame.iterations) {
        if (at.object == object.get()) {
            if (fresh) at.keys = vm.makeIterator(object, false);
            return at;
        }
    }
    Iteration made;
    made.object = object.get();
    made.hold = object;
    made.keys = vm.makeIterator(object, false);
    frame.iterations->push_back(std::move(made));
    return frame.iterations->back();
}

// -- Running --------------------------------------------------------------------

Val Domain::State::invoke(const MethodRef& method, const Val& self, Args args) {
    File& file = *method.file;
    if (method.method >= file.abc.methods.size()) {
        vm.throwError("VerifyError", "Error #1027: Method index out of range.", 1027);
    }
    const int32_t bodyIndex = file.bodyOf[method.method];
    if (bodyIndex < 0) {
        vm.throwError("VerifyError", "Error #1079: Native methods are not allowed in loaded code (" +
                                         file.methodNames[method.method] + ").", 1079);
    }
    if (depth >= kMaxDepth) vm.throwError("Error", "Error #1023: Stack overflow occurred.", 1023);
    Code& code = prepare(file, static_cast<uint32_t>(bodyIndex));
    const MethodInfo& info = file.abc.methods[method.method];
    std::unique_ptr<std::vector<Coerce>>& coercions = file.coercions[method.method];
    if (!coercions) {
        coercions = std::make_unique<std::vector<Coerce>>();
        for (uint32_t type : info.paramTypes) coercions->push_back(coerceOf(file, type));
        coercions->push_back(coerceOf(file, info.returnType));
    }

    const size_t size = code.locals + code.maxStack + code.maxScope;
    Frame frame;
    frame.file = &file;
    frame.code = &code;
    frame.method = &method;
    frame.regs = arena.take(size);
    frame.stack = frame.regs + code.locals;
    frame.stackEnd = frame.stack + code.maxStack;
    frame.sp = frame.stack;
    frame.scopes = frame.stackEnd;
    struct Guard {
        State& state;
        Frame& frame;
        size_t size;
        int exceptions;
        ~Guard() {
            if (std::uncaught_exceptions() > exceptions) state.noteUnwind(frame, nullptr);
            state.running.pop_back();
            --state.depth;
            state.arena.give(frame.regs, size);
        }
    } guard{*this, frame, size, std::uncaught_exceptions()};
    ++depth;
    running.push_back(&frame);

    // this, the parameters (coerced to their types, the missing ones their
    // defaults), then the rest or the arguments.
    frame.regs[0] = self.isNullish() ? globalOf(frame) : self;
    const size_t count = info.paramTypes.size();
    const size_t optionalFrom = count - std::min(count, info.options.size());
    // Too few arguments or too many are let by, the missing ones undefined
    // (the host may call a listener with less than a player would).
    for (size_t i = 0; i < count && i + 1 < code.locals; ++i) {
        Val value;
        if (i < args.size()) {
            value = args[i];
        } else if (i >= optionalFrom) {
            const OptionDetail& option = info.options[i - optionalFrom];
            value = constant(file, option.kind, option.value);
        }
        const Coerce coerce = (*coercions)[i];
        frame.regs[i + 1] = coerce == Coerce::None ? value : coerceValue(value, coerce);
    }
    if ((info.flags & (METHOD_NeedRest | METHOD_NeedArguments)) && count + 1 < code.locals) {
        std::vector<Val> extra;
        const size_t from = (info.flags & METHOD_NeedRest) ? count : 0;
        for (size_t i = from; i < args.size(); ++i) extra.push_back(args[i]);
        frame.regs[count + 1] = vm.newArray(std::move(extra));
    }
    Val result = run(frame);
    const Coerce coerce = coercions->back();
    return coerce == Coerce::None ? result : coerceValue(result, coerce);
}

void Domain::State::noteUnwind(const Frame& frame, const ScriptException*) {
    // An error goes out from the innermost method: one leaving a method no
    // further out than the last that was noted is another error.
    if (static_cast<size_t>(depth) >= errorDepth) errorStack.clear();
    errorDepth = static_cast<size_t>(depth);
    const std::string name = frame.method ? frame.method->name : std::string("?");
    // Where in it: the line when the code has debugline, else the offset
    // of the instruction.
    const size_t at = frame.code && frame.pc < frame.code->ins.size() ? frame.code->ins[frame.pc].offset : 0;
    errorStack += "\tat " + name + "()" +
                  (frame.line ? "[line " + std::to_string(frame.line) + "]" : "[@" + std::to_string(at) + "]") + "\n";
}

Val Domain::State::run(Frame& frame) {
    if (frame.code->handlers.empty()) return loop(frame);
    for (;;) {
        try {
            return loop(frame);
        } catch (ScriptException& exception) {
            if (!handle(frame, exception)) throw;
        }
    }
}

bool Domain::State::handle(Frame& frame, const ScriptException& exception) {
    const size_t at = frame.pc;
    for (Handler& handler : frame.code->handlers) {
        if (at < handler.from || at >= handler.to) continue;
        if (handler.type != 0) {
            Val type = resolveType(*frame.file, handler.type);
            if (type.isClass() && !vm.isType(exception.value, type)) continue;
        }
        for (Val* p = frame.stack; p < frame.sp; ++p) *p = Val();
        frame.sp = frame.stack;
        for (uint32_t i = 0; i < frame.scopeDepth; ++i) frame.scopes[i] = Val();
        frame.scopeDepth = 0;
        frame.withMask = 0;
        *frame.sp++ = exception.value;
        frame.pc = handler.target;
        errorStack.clear();
        errorDepth = 0;
        return true;
    }
    return false;
}

namespace {

double num(VM& vm, const Val& value) {
    return value.isNumber() ? value.num : vm.toNumber(value);
}

std::string escapeXml(const std::string& text, bool attribute) {
    std::string out;
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += attribute ? std::string(">") : std::string("&gt;"); break;
            case '"': out += attribute ? std::string("&quot;") : std::string("\""); break;
            default: out += c;
        }
    }
    return out;
}

} // namespace

Val Domain::State::loop(Frame& frame) {
    File& file = *frame.file;
    const std::vector<Ins>& ins = frame.code->ins;
    Val* regs = frame.regs;
    Val*& sp = frame.sp;
    const auto push = [&](Val value) {
        if (sp >= frame.stackEnd) vm.throwError("VerifyError", "Error #1023: Operand stack overflow.", 1023);
        *sp++ = std::move(value);
    };
    const auto pop = [&]() -> Val {
        if (sp <= frame.stack) vm.throwError("VerifyError", "Error #1024: Operand stack underflow.", 1024);
        Val value = std::move(*--sp);
        *sp = Val();
        return value;
    };
    // A multiname's parts on the stack, taken off: its key, and the name
    // when it is a value.
    struct Parts {
        const Name* n;
        std::string key;
        Val runtime;
        bool hasRuntime = false;
    };
    const auto parts = [&](uint32_t index) -> Parts {
        Parts p;
        p.n = &name(file, index);
        if (p.n->runtimeName) {
            p.runtime = pop();
            p.hasRuntime = true;
            p.key = runtimeKey(*p.n, p.runtime);
        } else {
            p.key = p.n->key;
        }
        if (p.n->runtimeNs) pop();
        return p;
    };
    const auto compare = [&](const Val& a, const Val& b, int how) -> bool {
        // 0 <, 1 <=, 2 >, 3 >=; false when either is NaN.
        bool undefinedResult = false;
        switch (how) {
            case 0: {
                const bool less = vm.lessThan(a, b, undefinedResult);
                return !undefinedResult && less;
            }
            case 1: {
                const bool greater = vm.lessThan(b, a, undefinedResult);
                return !undefinedResult && !greater;
            }
            case 2: {
                const bool greater = vm.lessThan(b, a, undefinedResult);
                return !undefinedResult && greater;
            }
            default: {
                const bool less = vm.lessThan(a, b, undefinedResult);
                return !undefinedResult && !less;
            }
        }
    };

    for (;;) {
        const Ins& in = ins[frame.pc];
        ++instructions;
        size_t next = frame.pc + 1;
        switch (in.op) {
            case OP_nop:
            case OP_label:
            case OP_bkpt:
            case OP_bkptline:
            case OP_timestamp:
            case OP_debug:
            case OP_debugfile:
            case OP_checkfilter:
                break;
            case OP_debugline:
                frame.line = static_cast<int>(in.a);
                break;
            case OP_dxns:
                break;
            case OP_dxnslate:
                pop();
                break;
            case OP_throw: {
                ScriptException exception;
                exception.value = pop();
                exception.line = frame.line;
                throw exception;
            }
            case OP_kill:
                regs[in.a] = Val();
                break;

            // Branches
            case OP_jump:
                next = static_cast<size_t>(in.target);
                break;
            case OP_iftrue:
                if (pop().toBoolean()) next = static_cast<size_t>(in.target);
                break;
            case OP_iffalse:
                if (!pop().toBoolean()) next = static_cast<size_t>(in.target);
                break;
            case OP_ifeq:
            case OP_ifne: {
                Val b = pop();
                Val a = pop();
                if (vm.equalsLoosely(a, b) == (in.op == OP_ifeq)) next = static_cast<size_t>(in.target);
                break;
            }
            case OP_ifstricteq:
            case OP_ifstrictne: {
                Val b = pop();
                Val a = pop();
                if (a.strictEquals(b) == (in.op == OP_ifstricteq)) next = static_cast<size_t>(in.target);
                break;
            }
            case OP_iflt:
            case OP_ifle:
            case OP_ifgt:
            case OP_ifge:
            case OP_ifnlt:
            case OP_ifnle:
            case OP_ifngt:
            case OP_ifnge: {
                Val b = pop();
                Val a = pop();
                bool result;
                bool negate = false;
                switch (in.op) {
                    case OP_iflt: result = compare(a, b, 0); break;
                    case OP_ifle: result = compare(a, b, 1); break;
                    case OP_ifgt: result = compare(a, b, 2); break;
                    case OP_ifge: result = compare(a, b, 3); break;
                    case OP_ifnlt: result = compare(a, b, 0); negate = true; break;
                    case OP_ifnle: result = compare(a, b, 1); negate = true; break;
                    case OP_ifngt: result = compare(a, b, 2); negate = true; break;
                    default: result = compare(a, b, 3); negate = true; break;
                }
                if (result != negate) next = static_cast<size_t>(in.target);
                break;
            }
            case OP_lookupswitch: {
                const std::vector<int32_t>& targets = frame.code->switches[in.a];
                const Val value = pop();
                const double at = num(vm, value);
                size_t chosen = 0;
                if (at >= 0 && at < static_cast<double>(targets.size() - 1) && at == std::floor(at)) {
                    chosen = static_cast<size_t>(at) + 1;
                }
                next = static_cast<size_t>(targets[chosen]);
                break;
            }

            // Scopes
            case OP_pushscope:
            case OP_pushwith: {
                Val object = pop();
                if (object.isNullish()) {
                    vm.throwError("TypeError", "Error #1009: Cannot access a property or method of a null object reference.", 1009);
                }
                if (frame.scopeDepth >= frame.code->maxScope) {
                    vm.throwError("VerifyError", "Error #1017: Scope stack overflow occurred.", 1017);
                }
                const uint32_t at = frame.scopeDepth++;
                frame.scopes[at] = std::move(object);
                const uint64_t bit = uint64_t(1) << std::min<uint32_t>(at, 63);
                if (in.op == OP_pushwith) {
                    frame.withMask |= bit;
                } else if (at < 63) {
                    frame.withMask &= ~bit;
                }
                break;
            }
            case OP_popscope:
                if (frame.scopeDepth == 0) vm.throwError("VerifyError", "Error #1018: Scope stack underflow occurred.", 1018);
                frame.scopes[--frame.scopeDepth] = Val();
                break;
            case OP_getscopeobject:
                push(in.a < frame.scopeDepth ? frame.scopes[in.a] : Val());
                break;
            case OP_getouterscope: {
                const ScopeChain* chain = frame.method->scope.get();
                push(chain && in.a < chain->size() ? (*chain)[in.a].object : Val());
                break;
            }
            case OP_getglobalscope:
                push(globalOf(frame));
                break;

            // Pushing values
            case OP_pushnull:
                push(Val::null());
                break;
            case OP_pushundefined:
                push(Val());
                break;
            case OP_pushtrue:
                push(Val::boolean(true));
                break;
            case OP_pushfalse:
                push(Val::boolean(false));
                break;
            case OP_pushnan:
                push(Val::number(std::nan("")));
                break;
            case OP_pushbyte:
                push(Val::number(static_cast<int8_t>(in.a)));
                break;
            case OP_pushshort:
                push(Val::number(static_cast<int16_t>(in.a)));
                break;
            case OP_pushint:
                push(Val::number(in.a < file.abc.pool.ints.size() ? file.abc.pool.ints[in.a] : 0));
                break;
            case OP_pushuint:
                push(Val::number(in.a < file.abc.pool.uints.size() ? file.abc.pool.uints[in.a] : 0));
                break;
            case OP_pushdouble:
                push(Val::number(in.a < file.abc.pool.doubles.size() ? file.abc.pool.doubles[in.a] : 0));
                break;
            case OP_pushstring:
                push(Val::string(poolString(file.abc, in.a)));
                break;
            case OP_pushnamespace:
                push(Val::string(nsUri(file.abc, in.a)));
                break;

            // The stack
            case OP_pop:
                pop();
                break;
            case OP_dup: {
                if (sp <= frame.stack) vm.throwError("VerifyError", "Error #1024: Operand stack underflow.", 1024);
                Val top = sp[-1];
                push(std::move(top));
                break;
            }
            case OP_swap: {
                if (sp - frame.stack < 2) vm.throwError("VerifyError", "Error #1024: Operand stack underflow.", 1024);
                std::swap(sp[-1], sp[-2]);
                break;
            }

            // Locals
            case OP_getlocal:
                push(regs[in.a]);
                break;
            case OP_getlocal0:
            case OP_getlocal1:
            case OP_getlocal2:
            case OP_getlocal3:
                push(regs[in.op - OP_getlocal0]);
                break;
            case OP_setlocal:
                regs[in.a] = pop();
                break;
            case OP_setlocal0:
            case OP_setlocal1:
            case OP_setlocal2:
            case OP_setlocal3:
                regs[in.op - OP_setlocal0] = pop();
                break;
            case OP_inclocal:
            case OP_declocal:
                regs[in.a] = Val::number(num(vm, regs[in.a]) + (in.op == OP_inclocal ? 1 : -1));
                break;
            case OP_inclocal_i:
            case OP_declocal_i:
                regs[in.a] = Val::number(static_cast<int32_t>(
                    static_cast<uint32_t>(toInt32(num(vm, regs[in.a]))) + (in.op == OP_inclocal_i ? 1u : 0xFFFFFFFFu)));
                break;

            // Arithmetic
            case OP_add: {
                Val b = pop();
                Val a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(Val::number(a.num + b.num));
                } else {
                    push(vm.add(a, b));
                }
                break;
            }
            case OP_subtract:
            case OP_multiply:
            case OP_divide:
            case OP_modulo: {
                Val bv = pop();
                Val av = pop();
                const double a = num(vm, av);
                const double b = num(vm, bv);
                double r;
                switch (in.op) {
                    case OP_subtract: r = a - b; break;
                    case OP_multiply: r = a * b; break;
                    case OP_divide: r = a / b; break;
                    default: r = std::fmod(a, b); break;
                }
                push(Val::number(r));
                break;
            }
            case OP_add_i:
            case OP_subtract_i:
            case OP_multiply_i: {
                Val bv = pop();
                Val av = pop();
                const uint32_t a = static_cast<uint32_t>(toInt32(num(vm, av)));
                const uint32_t b = static_cast<uint32_t>(toInt32(num(vm, bv)));
                uint32_t r;
                switch (in.op) {
                    case OP_add_i: r = a + b; break;
                    case OP_subtract_i: r = a - b; break;
                    default: r = a * b; break;
                }
                push(Val::number(static_cast<int32_t>(r)));
                break;
            }
            case OP_negate:
                push(Val::number(-num(vm, pop())));
                break;
            case OP_negate_i:
                push(Val::number(static_cast<int32_t>(0u - static_cast<uint32_t>(toInt32(num(vm, pop()))))));
                break;
            case OP_increment:
                push(Val::number(num(vm, pop()) + 1));
                break;
            case OP_decrement:
                push(Val::number(num(vm, pop()) - 1));
                break;
            case OP_increment_i:
                push(Val::number(static_cast<int32_t>(static_cast<uint32_t>(toInt32(num(vm, pop()))) + 1u)));
                break;
            case OP_decrement_i:
                push(Val::number(static_cast<int32_t>(static_cast<uint32_t>(toInt32(num(vm, pop()))) - 1u)));
                break;
            case OP_lshift:
            case OP_rshift:
            case OP_urshift:
            case OP_bitand:
            case OP_bitor:
            case OP_bitxor: {
                Val bv = pop();
                Val av = pop();
                const double a = num(vm, av);
                const double b = num(vm, bv);
                const uint32_t shift = toUint32(b) & 31;
                double r;
                switch (in.op) {
                    case OP_lshift: r = static_cast<int32_t>(static_cast<uint32_t>(toInt32(a)) << shift); break;
                    case OP_rshift: r = toInt32(a) >> shift; break;
                    case OP_urshift: r = toUint32(a) >> shift; break;
                    case OP_bitand: r = toInt32(a) & toInt32(b); break;
                    case OP_bitor: r = toInt32(a) | toInt32(b); break;
                    default: r = toInt32(a) ^ toInt32(b); break;
                }
                push(Val::number(r));
                break;
            }
            case OP_bitnot:
                push(Val::number(~toInt32(num(vm, pop()))));
                break;
            case OP_not:
                push(Val::boolean(!pop().toBoolean()));
                break;
            case OP_sxi1:
                push(Val::number((toInt32(num(vm, pop())) & 1) ? -1 : 0));
                break;
            case OP_sxi8:
                push(Val::number(static_cast<int8_t>(toInt32(num(vm, pop())) & 0xFF)));
                break;
            case OP_sxi16:
                push(Val::number(static_cast<int16_t>(toInt32(num(vm, pop())) & 0xFFFF)));
                break;

            // Comparison
            case OP_equals: {
                Val b = pop();
                Val a = pop();
                push(Val::boolean(vm.equalsLoosely(a, b)));
                break;
            }
            case OP_strictequals: {
                Val b = pop();
                Val a = pop();
                push(Val::boolean(a.strictEquals(b)));
                break;
            }
            case OP_lessthan:
            case OP_lessequals:
            case OP_greaterthan:
            case OP_greaterequals: {
                Val b = pop();
                Val a = pop();
                push(Val::boolean(compare(a, b, in.op - OP_lessthan)));
                break;
            }

            // Types
            case OP_typeof:
                push(Val::string(vm.typeOf(pop())));
                break;
            case OP_convert_s:
            case OP_esc_xelem:
            case OP_esc_xattr:
            case OP_coerce_s: {
                Val value = pop();
                if (in.op == OP_coerce_s && value.isNullish()) {
                    push(Val::null());
                } else if (in.op == OP_esc_xelem || in.op == OP_esc_xattr) {
                    push(Val::string(escapeXml(vm.toString(value), in.op == OP_esc_xattr)));
                } else {
                    push(value.isString() ? value : Val::string(vm.toString(value)));
                }
                break;
            }
            case OP_convert_i:
            case OP_coerce_i:
                push(Val::number(toInt32(num(vm, pop()))));
                break;
            case OP_convert_u:
            case OP_coerce_u:
                push(Val::number(toUint32(num(vm, pop()))));
                break;
            case OP_convert_d:
            case OP_coerce_d:
                push(Val::number(num(vm, pop())));
                break;
            case OP_convert_b:
            case OP_coerce_b:
                push(Val::boolean(pop().toBoolean()));
                break;
            case OP_convert_o: {
                if (sp > frame.stack && sp[-1].isNullish()) {
                    vm.throwError("TypeError", "Error #1009: Cannot access a property or method of a null object reference.", 1009);
                }
                break;
            }
            case OP_coerce_a:
                break;
            case OP_coerce_o: {
                Val value = pop();
                push(value.isUndefined() ? Val::null() : value);
                break;
            }
            case OP_coerce: {
                Val value = pop();
                const Coerce coerce = coerceOf(file, in.a);
                push(coerce == Coerce::None ? value : coerceValue(value, coerce));
                break;
            }
            case OP_astype: {
                Val value = pop();
                Val type = resolveType(file, in.a);
                push(type.isNullish() || vm.isType(value, type) ? value : Val::null());
                break;
            }
            case OP_astypelate: {
                Val type = pop();
                Val value = pop();
                push(vm.isType(value, type) ? value : Val::null());
                break;
            }
            case OP_istype: {
                Val value = pop();
                Val type = resolveType(file, in.a);
                push(Val::boolean(!type.isNullish() && vm.isType(value, type)));
                break;
            }
            case OP_istypelate: {
                Val type = pop();
                Val value = pop();
                push(Val::boolean(vm.isType(value, type)));
                break;
            }
            case OP_instanceof: {
                Val type = pop();
                Val value = pop();
                push(Val::boolean(type.isClass() && vm.isType(value, type)));
                break;
            }
            case OP_in: {
                Val object = pop();
                Val key = pop();
                push(Val::boolean(VMAccess::hasKey(vm, object, key)));
                break;
            }

            // Properties
            case OP_getlex: {
                const Name& n = name(file, in.a);
                Val holder = findProperty(frame, n, n.key, true);
                push(getProperty(holder, n, n.key, nullptr));
                break;
            }
            case OP_findpropstrict:
            case OP_findproperty: {
                Parts p = parts(in.a);
                push(findProperty(frame, *p.n, p.key, in.op == OP_findpropstrict));
                break;
            }
            case OP_finddef: {
                const Name& n = name(file, in.a);
                Val holder;
                if (!findInDomain(n, n.local, holder, nullptr)) holder = globalOf(frame);
                push(holder);
                break;
            }
            case OP_getproperty: {
                Parts p = parts(in.a);
                Val object = pop();
                push(getProperty(object, *p.n, p.key, p.hasRuntime ? &p.runtime : nullptr));
                break;
            }
            case OP_setproperty:
            case OP_initproperty: {
                Val value = pop();
                Parts p = parts(in.a);
                Val object = pop();
                setProperty(object, *p.n, p.key, p.hasRuntime ? &p.runtime : nullptr, value);
                break;
            }
            case OP_deleteproperty: {
                Parts p = parts(in.a);
                Val object = pop();
                const bool done = p.hasRuntime && !p.runtime.isString()
                                      ? VMAccess::deleteIndex(vm, object, p.runtime)
                                      : VMAccess::deleteProperty(vm, object, p.key);
                push(Val::boolean(done));
                break;
            }
            case OP_getdescendants: {
                Parts p = parts(in.a);
                Val object = pop();
                const std::string what = p.key;
                Val argument = Val::string(what.empty() ? std::string("*") : what);
                push(vm.callMethod(object, "descendants", Args(&argument, 1)));
                break;
            }
            case OP_getslot: {
                Val object = pop();
                Val* where = nullptr;
                push(slotOf(object, in.a, &where));
                break;
            }
            case OP_setslot: {
                Val value = pop();
                Val object = pop();
                Val* where = nullptr;
                slotOf(object, in.a, &where);
                *where = std::move(value);
                break;
            }
            case OP_getglobalslot: {
                Val* where = nullptr;
                push(slotOf(globalOf(frame), in.a, &where));
                break;
            }
            case OP_setglobalslot: {
                Val value = pop();
                Val* where = nullptr;
                slotOf(globalOf(frame), in.a, &where);
                *where = std::move(value);
                break;
            }
            case OP_getsuper: {
                Parts p = parts(in.a);
                Val object = pop();
                const as3::Trait* trait = superTrait(frame, p.key);
                push(vm.readTrait(object, *trait, false));
                break;
            }
            case OP_setsuper: {
                Val value = pop();
                Parts p = parts(in.a);
                Val object = pop();
                const as3::Trait* trait = superTrait(frame, p.key);
                vm.writeTrait(object, *trait, value, p.key, false);
                break;
            }

            // Calls
            case OP_call: {
                const uint32_t argc = in.a;
                Val* args = sp - argc;
                Val receiver = args[-1];
                Val function = args[-2];
                Val result = vm.callFunction(function, receiver, Args(args, argc));
                for (Val* p = args - 2; p < sp; ++p) *p = Val();
                sp = args - 2;
                push(std::move(result));
                break;
            }
            case OP_construct: {
                const uint32_t argc = in.a;
                Val* args = sp - argc;
                Val function = args[-1];
                Val result = vm.construct(function, Args(args, argc));
                for (Val* p = args - 1; p < sp; ++p) *p = Val();
                sp = args - 1;
                push(std::move(result));
                break;
            }
            case OP_callproperty:
            case OP_callproplex:
            case OP_callpropvoid:
            case OP_constructprop:
            case OP_callsuper:
            case OP_callsupervoid: {
                const uint32_t argc = in.b;
                if (static_cast<uint32_t>(sp - frame.stack) < argc + 1) {
                    vm.throwError("VerifyError", "Error #1024: Operand stack underflow.", 1024);
                }
                const Name& n = name(file, in.a);
                std::vector<Val> saved;
                Args args;
                Parts p;
                Val object;
                Val* first = sp - argc;
                const bool inPlace = !n.runtimeName && !n.runtimeNs;
                if (inPlace) {
                    // The arguments read where they are, the object under them.
                    p.n = &n;
                    p.key = n.key;
                    object = first[-1];
                    args = Args(first, argc);
                } else {
                    // The name's parts are under the arguments.
                    saved.assign(first, sp);
                    for (Val* at = first; at < sp; ++at) *at = Val();
                    sp = first;
                    p = parts(in.a);
                    object = pop();
                    args = Args(saved.data(), saved.size());
                }
                Val result;
                switch (in.op) {
                    case OP_constructprop: {
                        Val cls = getProperty(object, *p.n, p.key, p.hasRuntime ? &p.runtime : nullptr);
                        result = vm.construct(cls, args);
                        break;
                    }
                    case OP_callsuper:
                    case OP_callsupervoid: {
                        const as3::Trait* trait = superTrait(frame, p.key);
                        Val function = trait->kind == as3::TraitKind::Method ? trait->method : vm.readTrait(object, *trait, false);
                        result = vm.callFunction(function, object, args);
                        break;
                    }
                    default:
                        result = callProperty(object, *p.n, p.key, p.hasRuntime ? &p.runtime : nullptr, args,
                                              in.op == OP_callproplex);
                        break;
                }
                if (inPlace) {
                    for (Val* at = first - 1; at < sp; ++at) *at = Val();
                    sp = first - 1;
                }
                if (in.op != OP_callpropvoid && in.op != OP_callsupervoid) push(std::move(result));
                break;
            }
            case OP_callstatic: {
                const uint32_t argc = in.b;
                std::vector<Val> saved(sp - argc, sp);
                for (Val* p = sp - argc; p < sp; ++p) *p = Val();
                sp -= argc;
                Val receiver = pop();
                MethodRef ref;
                ref.file = &file;
                ref.method = in.a;
                ref.scope = frame.method->scope;
                ref.owner = frame.method->owner;
                ref.name = file.methodNames[in.a];
                push(invoke(ref, receiver, Args(saved.data(), saved.size())));
                break;
            }
            case OP_callmethod:
                vm.throwError("VerifyError", "Error #1051: callmethod is not supported.", 1051);
            case OP_constructsuper: {
                const uint32_t argc = in.a;
                std::vector<Val> saved(sp - argc, sp);
                for (Val* p = sp - argc; p < sp; ++p) *p = Val();
                sp -= argc;
                Val object = pop();
                const ClassRecord* owner = frame.method->owner;
                as3::ClassInfo* base = owner && owner->info ? owner->info->base.get() : nullptr;
                if (base) {
                    Val replaced;
                    vm.runNativeConstructor(base, object, Args(saved.data(), saved.size()), replaced);
                }
                break;
            }
            case OP_returnvoid:
                return Val();
            case OP_returnvalue:
                return pop();

            // Making things
            case OP_newobject: {
                const uint32_t argc = in.a;
                Val object = vm.newObject();
                Val* at = sp - 2 * argc;
                for (uint32_t i = 0; i < argc; ++i) {
                    const Val& key = at[2 * i];
                    vm.setProperty(object, key.isString() ? key.str() : vm.toString(key), at[2 * i + 1]);
                }
                for (Val* p = at; p < sp; ++p) *p = Val();
                sp = at;
                push(std::move(object));
                break;
            }
            case OP_newarray: {
                const uint32_t argc = in.a;
                std::vector<Val> elements(sp - argc, sp);
                for (Val* p = sp - argc; p < sp; ++p) *p = Val();
                sp -= argc;
                push(vm.newArray(std::move(elements)));
                break;
            }
            case OP_newactivation: {
                Code& code = *frame.code;
                if (!code.activation) {
                    const MethodBody& body = file.abc.bodies[file.bodyOf[frame.method->method]];
                    code.activation = slotClass(file, body.traits, "activation");
                }
                push(vm.allocate(code.activation));
                break;
            }
            case OP_newcatch: {
                if (in.a >= frame.code->handlers.size()) {
                    vm.throwError("VerifyError", "Error #1107: Exception index out of range.", 1107);
                }
                Handler& handler = frame.code->handlers[in.a];
                if (!handler.scopeClass) {
                    Trait trait;
                    trait.name = handler.var;
                    trait.kind = TRAIT_Slot;
                    trait.slotId = 1;
                    handler.scopeClass = slotClass(file, {trait}, "catch");
                }
                push(vm.allocate(handler.scopeClass));
                break;
            }
            case OP_newfunction: {
                auto scope = std::make_shared<ScopeChain>();
                if (frame.method->scope) *scope = *frame.method->scope;
                for (uint32_t i = 0; i < frame.scopeDepth; ++i) {
                    scope->push_back({frame.scopes[i], ((frame.withMask >> std::min<uint32_t>(i, 63)) & 1) != 0});
                }
                const std::string& methodName = in.a < file.methodNames.size() ? file.methodNames[in.a] : std::string();
                push(makeMethod(file, in.a, scope, frame.method->owner, methodName));
                break;
            }
            case OP_newclass: {
                Val base = pop();
                push(newClass(frame, in.a, base));
                break;
            }
            case OP_applytype: {
                const uint32_t argc = in.a;
                std::vector<Val> types(sp - argc, sp);
                for (Val* p = sp - argc; p < sp; ++p) *p = Val();
                sp -= argc;
                Val base = pop();
                push(vm.applyType(base, types));
                break;
            }

            // for-in
            case OP_hasnext2: {
                Val& object = regs[in.a];
                Val& at = regs[in.b];
                const double position = num(vm, at);
                if (object.isNullish() || !object.isObject()) {
                    push(Val::boolean(false));
                    break;
                }
                Iteration& iterating = iteration(frame, object, position == 0);
                const size_t keys = iterating.keys->elements.size();
                if (position < 0 || position >= static_cast<double>(keys)) {
                    at = Val::number(0);
                    object = Val::null();
                    push(Val::boolean(false));
                } else {
                    at = Val::number(position + 1);
                    push(Val::boolean(true));
                }
                break;
            }
            case OP_hasnext: {
                Val at = pop();
                Val object = pop();
                const double position = num(vm, at);
                if (!object.isObject()) {
                    push(Val::number(0));
                    break;
                }
                Iteration& iterating = iteration(frame, object, position == 0);
                const size_t keys = iterating.keys->elements.size();
                push(Val::number(position >= 0 && position < static_cast<double>(keys) ? position + 1 : 0));
                break;
            }
            case OP_nextname:
            case OP_nextvalue: {
                Val at = pop();
                Val object = pop();
                const double position = num(vm, at);
                if (!object.isObject()) {
                    push(Val());
                    break;
                }
                Iteration& iterating = iteration(frame, object, false);
                const std::vector<Val>& keys = iterating.keys->elements;
                const size_t i = position >= 1 ? static_cast<size_t>(position) - 1 : keys.size();
                if (i >= keys.size()) {
                    push(Val());
                    break;
                }
                Val key = keys[i];
                if (in.op == OP_nextname) {
                    push(key.isNumber() ? Val::string(key.toString()) : key);
                } else {
                    push(vm.getIndex(object, key));
                }
                break;
            }

            default:
                vm.throwError("VerifyError",
                              "Error #1011: Method " + (frame.method ? frame.method->name : std::string()) +
                                  " uses an instruction that is not supported: " +
                                  (opInfo(in.op).name ? opInfo(in.op).name : std::to_string(in.op)) + ".",
                              1011);
        }
        frame.pc = next;
    }
}

// -- Domain --------------------------------------------------------------------

Domain::Domain(VM& vm) : _state(std::make_shared<State>(vm)) {
    auto object = std::make_shared<Value>();
    object->type = ValueType::Object;
    object->cls = vm.builtins().object;
    _state->domainObject = Val(object);
    std::weak_ptr<State> weak = _state;
    vm.addDefinitionProvider([weak](const std::string& name, Val& value) {
        std::shared_ptr<State> state = weak.lock();
        return state && state->provide(name, value);
    });
}

Domain::~Domain() {
    // What the domain made may outlive it (in the VM); it no longer runs.
    _state->dead = true;
    for (auto& file : _state->files) {
        for (auto& record : file->classes) {
            if (record) record->object = Val();
        }
        for (auto& script : file->scripts) script->global = Val();
    }
    _state->index.clear();
    _state->domainObject = Val();
}

bool Domain::load(const std::vector<uint8_t>& bytes, std::string& error) {
    AbcFile file;
    if (!read(bytes, file, error)) return false;
    return load(std::move(file), error);
}

bool Domain::load(AbcFile file, std::string& error) {
    AS3_ZONE("ABC load");
    return _state->load(std::move(file), error);
}

void Domain::runEntry() {
    AS3_ZONE("ABC run entry");
    if (_state->files.empty()) return;
    File& file = *_state->files.back();
    if (file.scripts.empty()) return;
    _state->errorStack.clear();
    _state->start(*file.scripts.back());
}

void Domain::runAll() {
    AS3_ZONE("ABC run all");
    _state->errorStack.clear();
    for (auto& file : _state->files) {
        for (auto& script : file->scripts) _state->start(*script);
    }
}

Val Domain::find(const std::string& qualifiedName) {
    Val value;
    if (_state->provide(qualifiedName, value)) return value;
    return Val();
}

std::vector<std::string> Domain::definitionNames() const {
    return _state->order;
}

std::string Domain::takeErrorStack() {
    std::string stack;
    stack.swap(_state->errorStack);
    _state->errorDepth = 0;
    return stack;
}

uint64_t Domain::instructionCount() const {
    return _state->instructions;
}

} // namespace abc
} // namespace as3
