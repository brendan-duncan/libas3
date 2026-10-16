#pragma once

#include "opcode.h"
#include "../runtime/value.h"
#include "../ast/ast_nodes.h"
#include <vector>
#include <memory>
#include <map>

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
    void writeS32(int32_t value);

    // Constant pool management
    uint16_t addConstant(const Val& value);
    const Val& getConstant(size_t index) const { return constants[index]; }

    // Code access
    const std::vector<uint8_t>& getCode() const { return code; }
    uint8_t getByte(size_t offset) const;
    uint16_t getU16(size_t offset) const;
    uint32_t getU32(size_t offset) const;
    int32_t getS32(size_t offset) const;

    // Debug info
    int getLine(size_t offset) const;
    void setName(const std::string& n) { name = n; }
    const std::string& getName() const { return name; }

    // Patching (for jumps)
    void patchS32(size_t offset, int32_t value);
    size_t getCurrentOffset() const { return code.size(); }

    // Disassembly
    void disassemble(const std::string& name) const;
    size_t disassembleInstruction(size_t offset) const;

    // The constant pool, for the VM.
    std::vector<Val> constants;
    // Names that instructions use by index (properties, variables).
    std::vector<std::string> names;
    uint16_t addName(const std::string& name);

private:
    std::vector<uint8_t> code;            // Bytecode
    std::vector<int> lines;                // Line numbers for debugging
    std::string name;                      // Function/chunk name
    std::map<std::string, uint16_t> nameIndex;

    void addLine(int line);
};

// What the code of one file (or frame script) knows names by: its package
// and its imports.
struct CompileUnit {
    std::string fileName;
    std::string packageName;
    // import a.b.C: C to a.b.C.
    std::map<std::string, std::string> imports;
    // import a.b.*: a.b.
    std::vector<std::string> wildcards;
};

// An exception handler: an exception thrown in [start, end) continues at
// target, with the exception on the stack.
struct Handler {
    uint32_t start;
    uint32_t end;
    uint32_t target;
};

// Where a closure's captured variable comes from, when it is made: a cell
// of the function making it, or one that function captured itself.
struct UpvalueSource {
    bool fromCell;
    uint16_t index;
};

// An inline cache for a property instruction: the class last seen there
// and its trait.
struct PropertyCache {
    const ClassInfo* cls = nullptr;
    const Trait* trait = nullptr;
    // A static of the class (the object is the class itself).
    bool onClass = false;
    // For names: the global it found, valid while the VM's names are as
    // they were (version) and 'this' is of the class seen.
    uint32_t version = 0;
    Val value;
};

struct ClassTemplate;

// Function object for bytecode functions
class BytecodeFunction {
public:
    std::string name;
    int arity;                              // Number of parameters (not the rest one)
    Chunk chunk;

    // Locals (parameters first) and captured variables.
    uint16_t numLocals = 0;
    uint16_t numCells = 0;
    uint16_t maxStack = 0;
    // A parameter that is captured is copied to this cell at entry (-1: not).
    std::vector<int32_t> paramCells;
    std::vector<Coerce> paramCoerce;
    // What each local starts as, for a typed var: 0, NaN, false or null.
    std::vector<Coerce> localInit;
    std::vector<int32_t> cellInit;  // a cell's starting value, as a Coerce (-1: undefined)
    // The rest parameter's local (-1: none), and the local or cell
    // 'arguments' is kept in (-1: not used).
    int32_t restLocal = -1;
    int32_t restCell = -1;
    int32_t argumentsLocal = -1;
    int32_t argumentsCell = -1;
    // A named function expression's local or cell for itself (-1: none).
    int32_t selfLocal = -1;
    int32_t selfCell = -1;
    Coerce returnCoerce = Coerce::None;

    // A class member (its 'this' is the instance its names find).
    bool isMethod = false;
    bool isStatic = false;
    bool isConstructor = false;
    // The top-level script of a file or frame: its vars are the timeline's
    // or global.
    bool isScript = false;
    int line = 0;

    std::vector<Handler> handlers;
    std::vector<std::shared_ptr<BytecodeFunction>> functions;
    // What this function captures from the one it is made in.
    std::vector<UpvalueSource> upvalues;
    std::vector<std::shared_ptr<ClassTemplate>> classes;
    std::vector<PropertyCache> caches;
    std::shared_ptr<CompileUnit> unit;
    // Class.method, for stack traces.
    std::string qualifiedName;

    BytecodeFunction(const std::string& n, int a)
        : name(n)
        , arity(a) {
    }
};

// A class member as compiled.
struct MemberTemplate {
    enum class Kind { Var, Const, Method, Getter, Setter };
    Kind kind = Kind::Var;
    std::string name;
    bool isStatic = false;
    bool isOverride = false;
    bool isFinal = false;
    std::string typeName;
    std::shared_ptr<BytecodeFunction> function;
    // A literal initializer's value, for a var or const (the constructor
    // or the static initializer sets every field; this is what the class
    // shows before).
    bool hasLiteral = false;
    Val literal;
    std::vector<Metadata> metadata;
};

// A class as compiled, made into a class when its definition runs.
struct ClassTemplate {
    std::string name;
    std::string packageName;
    std::string qualifiedName;
    // As written; resolved when the class is defined.
    std::string superName;
    std::vector<std::string> interfaceNames;
    bool isInterface = false;
    bool isDynamic = false;
    bool isFinal = false;
    std::vector<MemberTemplate> members;
    // Field initializers, then the constructor's body.
    std::shared_ptr<BytecodeFunction> constructor;
    // Static field initializers and the statements in the class body.
    std::shared_ptr<BytecodeFunction> staticInit;
    std::vector<Metadata> metadata;
    std::shared_ptr<CompileUnit> unit;
    int line = 0;
};

} // namespace as3
