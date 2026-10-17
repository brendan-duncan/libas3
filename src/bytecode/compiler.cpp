#include "compiler.h"
#include "../runtime/profile.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <sstream>

namespace as3 {

// What the compiler knows of a variable of the function being compiled.
struct Compiler::VarInfo {
    bool isCell = false;
    uint16_t index = 0;
    Coerce coerce = Coerce::None;
    bool isConst = false;
};

// The class whose members are being compiled: what its names are.
struct Compiler::ClassContext {
    std::string name;
    std::string qualifiedName;
    std::map<std::string, Coerce> instanceMembers;
    std::map<std::string, Coerce> staticMembers;
    // Its private members, which its code names by "pkg.Class::name".
    std::set<std::string> privateMembers;
};

// A statement break, continue or return can leave: a loop, a switch, a
// labeled statement, or a try whose finally block must run on the way.
struct Compiler::ControlEntry {
    enum class Kind { Loop, Switch, Label, Try };
    Kind kind = Kind::Loop;
    std::vector<std::string> labels;
    std::vector<size_t> breakJumps;
    std::vector<size_t> continueJumps;
    // A try's finally block, and the code its handler covers so far.
    Statement* finallyBody = nullptr;
    bool regionOpen = false;
    bool paused = false;
    uint32_t regionStart = 0;
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
};

struct Compiler::FunctionState {
    FunctionState* parent = nullptr;
    std::shared_ptr<BytecodeFunction> proto;
    std::map<std::string, VarInfo> vars;
    std::map<std::string, uint16_t> upvalueIndex;
    ClassContext* cls = nullptr;
    bool isStatic = false;
    bool isScript = false;
    std::shared_ptr<CompileUnit> unit;
    // A script's typed top-level vars.
    std::map<std::string, Coerce> scriptVars;
    std::vector<ControlEntry> controls;
    int depth = 0;
    int maxDepth = 0;
    int tempCount = 0;
    // The locals holding the objects of the with blocks around, outermost
    // first.
    std::vector<uint16_t> withLocals;
};

namespace {

// What a function declares and what names it and the functions in it use,
// to find which of its variables a closure captures.
struct Scan {
    std::vector<std::pair<std::string, std::string>> vars;
    std::vector<FunctionDefinition*> functions;
    std::set<std::string> refs;
    std::set<std::string> innerFree;
    bool usesArguments = false;
    bool callsSuper = false;
};

void scanStatement(Statement* stmt, Scan& scan);
void scanExpression(Expression* expr, Scan& scan);
std::set<std::string> freeNames(const std::vector<std::unique_ptr<Parameter>>& params, Statement* body,
                                const std::string& selfName);

void scanExpression(Expression* expr, Scan& scan) {
    if (!expr) {
        return;
    }
    switch (expr->kind) {
        case Expression::ExpressionKind::Identifier: {
            auto* id = static_cast<Identifier*>(expr);
            scan.refs.insert(id->name);
            if (id->name == "arguments") {
                scan.usesArguments = true;
            }
            break;
        }
        case Expression::ExpressionKind::Binary: {
            // Down the left of a chain without recursing.
            Expression* at = expr;
            while (at && at->kind == Expression::ExpressionKind::Binary) {
                auto* binary = static_cast<BinaryExpression*>(at);
                scanExpression(binary->right.get(), scan);
                at = binary->left.get();
            }
            scanExpression(at, scan);
            break;
        }
        case Expression::ExpressionKind::Unary:
            scanExpression(static_cast<UnaryExpression*>(expr)->operand.get(), scan);
            break;
        case Expression::ExpressionKind::Conditional: {
            auto* conditional = static_cast<ConditionalExpression*>(expr);
            scanExpression(conditional->test.get(), scan);
            scanExpression(conditional->consequent.get(), scan);
            scanExpression(conditional->alternate.get(), scan);
            break;
        }
        case Expression::ExpressionKind::Assignment: {
            auto* assignment = static_cast<AssignmentExpression*>(expr);
            scanExpression(assignment->left.get(), scan);
            scanExpression(assignment->right.get(), scan);
            break;
        }
        case Expression::ExpressionKind::Call: {
            auto* call = static_cast<CallExpression*>(expr);
            if (call->callee && call->callee->kind == Expression::ExpressionKind::Super) {
                scan.callsSuper = true;
            }
            scanExpression(call->callee.get(), scan);
            for (auto& arg : call->arguments) {
                scanExpression(arg.get(), scan);
            }
            break;
        }
        case Expression::ExpressionKind::New: {
            auto* made = static_cast<NewExpression*>(expr);
            scanExpression(made->callee.get(), scan);
            for (auto& arg : made->arguments) {
                scanExpression(arg.get(), scan);
            }
            break;
        }
        case Expression::ExpressionKind::Member: {
            auto* member = static_cast<MemberExpression*>(expr);
            scanExpression(member->object.get(), scan);
            if (member->computed) {
                scanExpression(member->computedProperty.get(), scan);
            }
            break;
        }
        case Expression::ExpressionKind::Function: {
            auto* function = static_cast<FunctionExpression*>(expr);
            const auto free = freeNames(function->parameters, function->body.get(),
                                        function->name ? function->name->name : std::string());
            scan.innerFree.insert(free.begin(), free.end());
            break;
        }
        case Expression::ExpressionKind::Array:
            for (auto& element : static_cast<ArrayExpression*>(expr)->elements) {
                scanExpression(element.get(), scan);
            }
            break;
        case Expression::ExpressionKind::Object:
            for (auto& property : static_cast<ObjectExpression*>(expr)->properties) {
                if (property.first && property.first->kind != Expression::ExpressionKind::Identifier) {
                    scanExpression(property.first.get(), scan);
                }
                scanExpression(property.second.get(), scan);
            }
            break;
        case Expression::ExpressionKind::Sequence:
            for (auto& part : static_cast<SequenceExpression*>(expr)->expressions) {
                scanExpression(part.get(), scan);
            }
            break;
        case Expression::ExpressionKind::TypeApplication:
            scanExpression(static_cast<TypeApplication*>(expr)->base.get(), scan);
            break;
        default:
            break;
    }
}

void scanStatement(Statement* stmt, Scan& scan) {
    if (!stmt) {
        return;
    }
    if (auto* block = dynamic_cast<BlockStatement*>(stmt)) {
        for (auto& s : block->statements) {
            scanStatement(s.get(), scan);
        }
    } else if (auto* expression = dynamic_cast<ExpressionStatement*>(stmt)) {
        scanExpression(expression->expression.get(), scan);
    } else if (auto* variables = dynamic_cast<VariableDefinition*>(stmt)) {
        for (auto& decl : variables->declarations) {
            scan.vars.emplace_back(decl->id->name, decl->type ? decl->type->name : std::string());
            scanExpression(decl->init.get(), scan);
        }
    } else if (auto* function = dynamic_cast<FunctionDefinition*>(stmt)) {
        scan.functions.push_back(function);
        scan.vars.emplace_back(function->name->name, std::string());
        const auto free = freeNames(function->parameters, function->body.get(), std::string());
        scan.innerFree.insert(free.begin(), free.end());
    } else if (auto* branch = dynamic_cast<IfStatement*>(stmt)) {
        scanExpression(branch->test.get(), scan);
        scanStatement(branch->consequent.get(), scan);
        scanStatement(branch->alternate.get(), scan);
    } else if (auto* loop = dynamic_cast<WhileStatement*>(stmt)) {
        scanExpression(loop->test.get(), scan);
        scanStatement(loop->body.get(), scan);
    } else if (auto* doLoop = dynamic_cast<DoWhileStatement*>(stmt)) {
        scanStatement(doLoop->body.get(), scan);
        scanExpression(doLoop->test.get(), scan);
    } else if (auto* forLoop = dynamic_cast<ForStatement*>(stmt)) {
        scanStatement(forLoop->init.get(), scan);
        scanExpression(forLoop->test.get(), scan);
        scanExpression(forLoop->update.get(), scan);
        scanStatement(forLoop->body.get(), scan);
    } else if (auto* forIn = dynamic_cast<ForInStatement*>(stmt)) {
        scanStatement(forIn->left.get(), scan);
        scanExpression(forIn->right.get(), scan);
        scanStatement(forIn->body.get(), scan);
    } else if (auto* choice = dynamic_cast<SwitchStatement*>(stmt)) {
        scanExpression(choice->discriminant.get(), scan);
        for (auto& clause : choice->cases) {
            scanExpression(clause->test.get(), scan);
            for (auto& s : clause->consequent) {
                scanStatement(s.get(), scan);
            }
        }
    } else if (auto* labeled = dynamic_cast<LabeledStatement*>(stmt)) {
        scanStatement(labeled->body.get(), scan);
    } else if (auto* attempt = dynamic_cast<TryStatement*>(stmt)) {
        scanStatement(attempt->block.get(), scan);
        for (auto& clause : attempt->catches) {
            scan.vars.emplace_back(clause.variable->name, std::string());
            scanStatement(clause.body.get(), scan);
        }
        scanStatement(attempt->finallyBody.get(), scan);
    } else if (auto* result = dynamic_cast<ReturnStatement*>(stmt)) {
        scanExpression(result->argument.get(), scan);
    } else if (auto* thrown = dynamic_cast<ThrowStatement*>(stmt)) {
        scanExpression(thrown->argument.get(), scan);
    } else if (auto* with = dynamic_cast<WithStatement*>(stmt)) {
        scanExpression(with->object.get(), scan);
        scanStatement(with->body.get(), scan);
    }
}

std::set<std::string> freeNames(const std::vector<std::unique_ptr<Parameter>>& params, Statement* body,
                                const std::string& selfName) {
    Scan scan;
    scanStatement(body, scan);
    std::set<std::string> declared;
    for (const auto& param : params) {
        declared.insert(param->name->name);
    }
    for (const auto& var : scan.vars) {
        declared.insert(var.first);
    }
    declared.insert("arguments");
    if (!selfName.empty()) {
        declared.insert(selfName);
    }
    std::set<std::string> free;
    for (const auto& name : scan.refs) {
        if (!declared.count(name)) free.insert(name);
    }
    for (const auto& name : scan.innerFree) {
        if (!declared.count(name)) free.insert(name);
    }
    return free;
}

// A literal's value, for a field's default or a constant.
bool literalValue(Expression* expr, Val& out) {
    if (!expr || expr->kind != Expression::ExpressionKind::Literal) {
        return false;
    }
    auto* literal = static_cast<Literal*>(expr);
    switch (literal->kind) {
        case Literal::LiteralKind::Numeric:
            out = Val::number(literal->numberValue);
            return true;
        case Literal::LiteralKind::String:
            out = Val::string(literal->stringValue);
            return true;
        case Literal::LiteralKind::Boolean:
            out = Val::boolean(literal->boolValue);
            return true;
        case Literal::LiteralKind::Null:
            out = Val::null();
            return true;
        default:
            return false;
    }
}

// The roots of package names that a.b.C may start with when nothing else
// has that name.
bool isPackageRoot(const std::string& name, const CompileUnit* unit) {
    static const char* known[] = {"flash", "fl", "mx", "com", "org", "net", "air", "adobe", "spark", "de", "uk"};
    for (const char* root : known) {
        if (name == root) return true;
    }
    if (!unit) {
        return false;
    }
    const auto rootOf = [](const std::string& path) { return path.substr(0, path.find('.')); };
    if (!unit->packageName.empty() && rootOf(unit->packageName) == name) {
        return true;
    }
    for (const auto& import : unit->imports) {
        if (rootOf(import.second) == name) return true;
    }
    for (const auto& wildcard : unit->wildcards) {
        if (rootOf(wildcard) == name) return true;
    }
    return false;
}

} // namespace

Compiler::Compiler() {
}

Compiler::~Compiler() {
}

// Emission

size_t Compiler::here() const {
    return fs->proto->chunk.getCurrentOffset();
}

void Compiler::adjust(int delta) {
    fs->depth += delta;
    if (fs->depth > fs->maxDepth) {
        fs->maxDepth = fs->depth;
    }
    if (fs->depth < 0) {
        fs->depth = 0;
    }
}

void Compiler::emitOp(OpCode op) {
    fs->proto->chunk.writeOpCode(op, currentLine);
    switch (op) {
        case OpCode::OP_CONSTANT: case OpCode::OP_UNDEFINED: case OpCode::OP_NULL: case OpCode::OP_TRUE:
        case OpCode::OP_FALSE: case OpCode::OP_ZERO: case OpCode::OP_ONE: case OpCode::OP_DUP:
        case OpCode::OP_DUP_X1: case OpCode::OP_DUP_X2: case OpCode::OP_GET_LOCAL: case OpCode::OP_GET_CELL:
        case OpCode::OP_GET_UPVALUE: case OpCode::OP_GET_NAME: case OpCode::OP_TYPEOF_NAME:
        case OpCode::OP_GET_QNAME: case OpCode::OP_GET_SUPER: case OpCode::OP_THIS: case OpCode::OP_SCOPE_THIS:
        case OpCode::OP_OWNER_CLASS: case OpCode::OP_CLOSURE: case OpCode::OP_NEW_REGEXP:
        case OpCode::OP_DEFINE_CLASS: case OpCode::OP_ITER_NEXT: case OpCode::OP_GET_LOCAL_PROP:
        case OpCode::OP_GET_THIS_PROP:
            adjust(1);
            break;
        case OpCode::OP_DUP2:
            adjust(2);
            break;
        case OpCode::OP_POP: case OpCode::OP_PUT_LOCAL: case OpCode::OP_PUT_CELL: case OpCode::OP_PUT_UPVALUE:
        case OpCode::OP_DEFINE_VAR: case OpCode::OP_DEFINE_FUNCTION: case OpCode::OP_SET_PROP:
        case OpCode::OP_GET_INDEX: case OpCode::OP_DELETE_INDEX: case OpCode::OP_ADD: case OpCode::OP_SUBTRACT:
        case OpCode::OP_MULTIPLY: case OpCode::OP_DIVIDE: case OpCode::OP_MODULO: case OpCode::OP_EQUAL:
        case OpCode::OP_NOT_EQUAL: case OpCode::OP_STRICT_EQUAL: case OpCode::OP_STRICT_NOT_EQUAL:
        case OpCode::OP_GREATER: case OpCode::OP_GREATER_EQUAL: case OpCode::OP_LESS: case OpCode::OP_LESS_EQUAL:
        case OpCode::OP_BIT_AND: case OpCode::OP_BIT_OR: case OpCode::OP_BIT_XOR: case OpCode::OP_LEFT_SHIFT:
        case OpCode::OP_RIGHT_SHIFT: case OpCode::OP_UNSIGNED_RIGHT_SHIFT: case OpCode::OP_INSTANCEOF:
        case OpCode::OP_IN: case OpCode::OP_IS: case OpCode::OP_AS: case OpCode::OP_JUMP_IF_FALSE:
        case OpCode::OP_JUMP_IF_TRUE: case OpCode::OP_JUMP_IF_FALSE_KEEP: case OpCode::OP_JUMP_IF_TRUE_KEEP:
        case OpCode::OP_JUMP_IF_NOT_NULLISH_KEEP: case OpCode::OP_RETURN: case OpCode::OP_THROW:
            adjust(-1);
            break;
        case OpCode::OP_SET_INDEX:
            adjust(-2);
            break;
        default:
            break;
    }
}

void Compiler::emitU8(uint8_t value) {
    fs->proto->chunk.writeByte(value, currentLine);
}

void Compiler::emitU16(uint16_t value) {
    fs->proto->chunk.writeU16(value);
}

void Compiler::emitS32(int32_t value) {
    fs->proto->chunk.writeS32(value);
}

void Compiler::emitConstant(const Val& value) {
    const uint16_t index = fs->proto->chunk.addConstant(value);
    emitOp(OpCode::OP_CONSTANT);
    emitU16(index);
}

void Compiler::emitNumber(double value) {
    if (value == 0 && !std::signbit(value)) {
        emitOp(OpCode::OP_ZERO);
    } else if (value == 1) {
        emitOp(OpCode::OP_ONE);
    } else {
        emitConstant(Val::number(value));
    }
}

void Compiler::emitString(const std::string& value) {
    emitConstant(Val::string(value));
}

uint16_t Compiler::nameIndex(const std::string& name) {
    return fs->proto->chunk.addName(name);
}

uint16_t Compiler::newCache() {
    fs->proto->caches.emplace_back();
    return static_cast<uint16_t>(fs->proto->caches.size() - 1);
}

size_t Compiler::emitJump(OpCode op) {
    emitOp(op);
    const size_t at = here();
    emitS32(0);
    return at;
}

void Compiler::patchJump(size_t at) {
    patchJumpTo(at, here());
}

void Compiler::patchJumpTo(size_t at, size_t target) {
    // Relative to the end of the jump's operand.
    fs->proto->chunk.patchS32(at, static_cast<int32_t>(static_cast<int64_t>(target) - static_cast<int64_t>(at + 4)));
}

void Compiler::emitJumpTo(OpCode op, size_t target) {
    const size_t at = emitJump(op);
    patchJumpTo(at, target);
}

uint16_t Compiler::allocTemp() {
    const uint16_t slot = fs->proto->numLocals++;
    fs->proto->localInit.push_back(Coerce::None);
    return slot;
}

std::string Compiler::propertyKey(const std::string& name) const {
    if (fs && fs->cls && fs->cls->privateMembers.count(name)) {
        return fs->cls->qualifiedName + "::" + name;
    }
    return name;
}

void Compiler::emitGetProp(const std::string& name) {
    emitOp(OpCode::OP_GET_PROP);
    emitU16(nameIndex(propertyKey(name)));
    emitU16(newCache());
}

void Compiler::emitSetProp(const std::string& name) {
    emitOp(OpCode::OP_SET_PROP);
    emitU16(nameIndex(propertyKey(name)));
    emitU16(newCache());
}

void Compiler::emitCallProp(const std::string& name, int argc) {
    emitOp(OpCode::OP_CALL_PROP);
    emitU16(nameIndex(propertyKey(name)));
    emitU8(static_cast<uint8_t>(argc));
    emitU16(newCache());
    adjust(-argc);
}

void Compiler::emitThisProp(OpCode op, const std::string& name) {
    emitOp(op);
    emitU16(nameIndex(propertyKey(name)));
    emitU16(newCache());
}

void Compiler::emitLocalProp(OpCode op, uint16_t slot, const std::string& name) {
    emitOp(op);
    emitU16(slot);
    emitU16(nameIndex(propertyKey(name)));
    emitU16(newCache());
}

void Compiler::emitFusedCall(OpCode op, uint16_t slot, const std::string& name, int argc) {
    emitOp(op);
    if (op == OpCode::OP_CALL_LOCAL_PROP) {
        emitU16(slot);
    }
    emitU16(nameIndex(propertyKey(name)));
    emitU8(static_cast<uint8_t>(argc));
    emitU16(newCache());
    adjust(-argc);
}

bool Compiler::localObject(Expression* object, uint16_t& slot) {
    if (!object || object->kind != Expression::ExpressionKind::Identifier) {
        return false;
    }
    const Resolved where = resolve(static_cast<Identifier*>(object)->name);
    if (where.kind != NameKind::Local) {
        return false;
    }
    slot = where.index;
    return true;
}

bool Compiler::thisObject(Expression* object) {
    // this in a method is the instance its names find.
    return object && object->kind == Expression::ExpressionKind::This && fs->proto->isMethod;
}

size_t Compiler::emitWith(OpCode op, const std::string& name) {
    emitOp(op);
    emitU16(nameIndex(name));
    emitU8(static_cast<uint8_t>(fs->withLocals.size()));
    for (auto it = fs->withLocals.rbegin(); it != fs->withLocals.rend(); ++it) {
        emitU16(*it);
    }
    const size_t at = here();
    emitS32(0);
    return at;
}

void Compiler::emitCoerce(Coerce coerce) {
    if (coerce == Coerce::None) {
        return;
    }
    emitOp(OpCode::OP_COERCE);
    emitU8(static_cast<uint8_t>(coerce));
}

void Compiler::error(const std::string& message) {
    std::ostringstream text;
    if (!fileName.empty()) {
        text << fileName << ":";
    }
    text << currentLine << ": " << message;
    errors.push_back(text.str());
}

void Compiler::warning(const std::string& message) {
    std::ostringstream text;
    if (!fileName.empty()) {
        text << fileName << ":";
    }
    text << currentLine << ": " << message;
    warnings.push_back(text.str());
}

// Programs

std::shared_ptr<BytecodeFunction> Compiler::compile(Program* program) {
    AS3_ZONE("AS3 compile");
    errors.clear();
    fileName = program ? program->fileName : std::string();
    auto script = std::make_shared<BytecodeFunction>("<script>", 0);
    script->isScript = true;
    FunctionState state;
    state.proto = script;
    state.isScript = true;
    FunctionState* saved = fs;
    fs = &state;

    // The file's imports, outside any package, and what its packages
    // import too.
    auto topUnit = std::make_shared<CompileUnit>();
    topUnit->fileName = fileName;
    const auto addImport = [](CompileUnit& unit, ImportDirective* import) {
        if (import->isPackage) {
            unit.wildcards.push_back(import->path);
        } else {
            unit.imports[import->name->name] =
                import->path.empty() ? import->name->name : import->path + "." + import->name->name;
        }
    };
    if (program) {
        for (auto& import : program->imports) {
            addImport(*topUnit, import.get());
        }
        for (auto& node : program->statements) {
            if (auto* import = dynamic_cast<ImportDirective*>(node.get())) {
                addImport(*topUnit, import);
            }
        }
    }
    script->unit = topUnit;
    state.unit = topUnit;

    if (program) {
        // Packages first, each with its imports, then what is outside
        // them, in order.
        std::vector<AstNode*> outside;
        for (auto& node : program->statements) {
            if (auto* package = dynamic_cast<PackageDefinition*>(node.get())) {
                auto unit = std::make_shared<CompileUnit>(*topUnit);
                unit->packageName = package->name ? package->name->name : std::string();
                for (auto& import : package->imports) {
                    addImport(*unit, import.get());
                }
                std::vector<AstNode*> members;
                for (auto& member : package->members) {
                    if (auto* import = dynamic_cast<ImportDirective*>(member.get())) {
                        addImport(*unit, import);
                    } else {
                        members.push_back(member.get());
                    }
                }
                compileUnitMembers(members, unit);
            } else if (!dynamic_cast<ImportDirective*>(node.get())) {
                outside.push_back(node.get());
            }
        }
        compileUnitMembers(outside, topUnit);
    }

    emitOp(OpCode::OP_RETURN_UNDEFINED);
    script->maxStack = static_cast<uint16_t>(std::min(state.maxDepth + 8, 65535));
    fs = saved;

    if (!errors.empty()) {
        std::string all;
        for (const auto& message : errors) {
            all += message + "\n";
        }
        throw CompileError("Compilation failed: " + all, fileName, 0);
    }
    script->chunk.fuse();
    return script;
}

void Compiler::compileUnitMembers(std::vector<AstNode*>& members, const std::shared_ptr<CompileUnit>& unit) {
    std::shared_ptr<CompileUnit> savedUnit = fs->unit;
    fs->unit = unit;
    const std::string prefix = unit->packageName.empty() ? std::string() : unit->packageName + ".";

    // Classes first, a base before the classes that extend it, then
    // functions, then the rest in order.
    std::vector<ClassDefinition*> classes;
    for (AstNode* node : members) {
        if (auto* definition = dynamic_cast<ClassDefinition*>(node)) {
            classes.push_back(definition);
        }
    }
    std::vector<ClassDefinition*> ordered;
    std::set<ClassDefinition*> placed;
    std::function<void(ClassDefinition*, int)> place = [&](ClassDefinition* definition, int depth) {
        if (placed.count(definition) || depth > 64) {
            return;
        }
        std::vector<std::string> bases;
        if (definition->superClass) bases.push_back(definition->superClass->name);
        for (auto& face : definition->interfaces) bases.push_back(face->name);
        for (const auto& base : bases) {
            const std::string shortName = base.substr(base.find_last_of('.') == std::string::npos ? 0 : base.find_last_of('.') + 1);
            for (ClassDefinition* other : classes) {
                if (other != definition && other->name->name == shortName) {
                    place(other, depth + 1);
                }
            }
        }
        placed.insert(definition);
        ordered.push_back(definition);
    };
    for (ClassDefinition* definition : classes) {
        place(definition, 0);
    }
    for (ClassDefinition* definition : ordered) {
        currentLine = definition->line;
        compileClass(definition, unit);
    }

    for (AstNode* node : members) {
        if (auto* function = dynamic_cast<FunctionDefinition*>(node)) {
            currentLine = function->line;
            auto code = compileFunction(function->name->name, function->parameters, function->returnType.get(),
                                        function->body.get(), nullptr, false, false, false, function->line);
            emitClosure(code);
            emitOp(OpCode::OP_DEFINE_FUNCTION);
            emitU16(nameIndex(prefix + function->name->name));
        }
    }

    for (AstNode* node : members) {
        if (dynamic_cast<ClassDefinition*>(node) || dynamic_cast<FunctionDefinition*>(node)) {
            continue;
        }
        if (auto* variables = dynamic_cast<VariableDefinition*>(node); variables && !prefix.empty()) {
            // A package-level variable: by its qualified name.
            currentLine = variables->line;
            for (auto& decl : variables->declarations) {
                const Coerce coerce = coerceForType(decl->type ? decl->type->name : std::string());
                if (decl->init) {
                    compileExpression(decl->init.get());
                    emitCoerce(coerce);
                } else {
                    emitConstant(defaultForCoerce(coerce));
                }
                emitOp(OpCode::OP_DEFINE_VAR);
                emitU16(nameIndex(prefix + decl->id->name));
            }
            continue;
        }
        if (auto* stmt = dynamic_cast<Statement*>(node)) {
            compileStatement(stmt);
        }
    }
    fs->unit = savedUnit;
}

// Functions

std::shared_ptr<BytecodeFunction> Compiler::compileFunction(
    const std::string& name, const std::vector<std::unique_ptr<Parameter>>& params, Identifier* returnType,
    Statement* body, ClassContext* cls, bool isStatic, bool isMethod, bool isConstructor, int line,
    const std::vector<std::pair<std::string, Expression*>>* fieldInits, const std::string& selfName) {
    int arity = 0;
    for (const auto& param : params) {
        if (!param->rest) ++arity;
    }
    auto proto = std::make_shared<BytecodeFunction>(name, arity);
    proto->isMethod = isMethod;
    proto->isStatic = isStatic;
    proto->isConstructor = isConstructor;
    proto->line = line;
    proto->returnCoerce = returnType ? coerceForType(returnType->name) : Coerce::None;
    if (proto->returnCoerce == Coerce::Nullable) {
        proto->returnCoerce = Coerce::None;
    }

    FunctionState state;
    state.parent = fs;
    state.proto = proto;
    state.cls = cls ? cls : (fs ? fs->cls : nullptr);
    state.isStatic = cls ? isStatic : (fs ? fs->isStatic : false);
    state.unit = fs ? fs->unit : nullptr;
    proto->unit = state.unit;
    FunctionState* saved = fs;
    const int savedLine = currentLine;
    fs = &state;
    if (line > 0) {
        currentLine = line;
    }

    // What it declares, and which of those a closure in it captures.
    Scan scan;
    scanStatement(body, scan);
    if (fieldInits) {
        for (const auto& init : *fieldInits) {
            scanExpression(init.second, scan);
        }
    }
    for (const auto& param : params) {
        scanExpression(param->defaultValue.get(), scan);
    }
    const std::set<std::string>& captured = scan.innerFree;

    const auto declare = [&](const std::string& varName, Coerce coerce, bool forceLocal) -> VarInfo& {
        auto found = state.vars.find(varName);
        if (found != state.vars.end()) {
            return found->second;
        }
        VarInfo info;
        info.coerce = coerce;
        if (!forceLocal && captured.count(varName)) {
            info.isCell = true;
            info.index = proto->numCells++;
            proto->cellInit.push_back(static_cast<int32_t>(coerce));
        } else {
            info.index = proto->numLocals++;
            proto->localInit.push_back(coerce);
        }
        return state.vars.emplace(varName, info).first->second;
    };

    // Parameters take the first locals, captured or not (a captured one is
    // copied to its cell on entry).
    for (const auto& param : params) {
        if (param->rest) continue;
        const Coerce coerce = param->type ? coerceForType(param->type->name) : Coerce::None;
        VarInfo info;
        info.coerce = coerce;
        info.index = proto->numLocals++;
        proto->localInit.push_back(Coerce::None);
        proto->paramCoerce.push_back(coerce);
        if (captured.count(param->name->name)) {
            VarInfo cell;
            cell.isCell = true;
            cell.coerce = coerce;
            cell.index = proto->numCells++;
            proto->cellInit.push_back(-1);
            proto->paramCells.push_back(cell.index);
            state.vars[param->name->name] = cell;
        } else {
            proto->paramCells.push_back(-1);
            state.vars[param->name->name] = info;
        }
    }
    for (const auto& param : params) {
        if (!param->rest) continue;
        VarInfo& rest = declare(param->name->name, Coerce::None, false);
        if (rest.isCell) {
            proto->restCell = rest.index;
        } else {
            proto->restLocal = rest.index;
        }
    }
    if (scan.usesArguments && !state.vars.count("arguments")) {
        VarInfo& arguments = declare("arguments", Coerce::None, false);
        if (arguments.isCell) {
            proto->argumentsCell = arguments.index;
        } else {
            proto->argumentsLocal = arguments.index;
        }
    }
    for (const auto& var : scan.vars) {
        declare(var.first, coerceForType(var.second), false);
    }
    // A function expression's own name, inside it: the function.
    if (!selfName.empty() && !state.vars.count(selfName)) {
        VarInfo& self = declare(selfName, Coerce::None, false);
        if (self.isCell) {
            proto->selfCell = self.index;
        } else {
            proto->selfLocal = self.index;
        }
    }

    // Default values for arguments not given.
    int paramIndex = 0;
    for (const auto& param : params) {
        if (param->rest) continue;
        if (param->defaultValue) {
            emitOp(OpCode::OP_DEFAULT_ARG);
            emitU16(static_cast<uint16_t>(paramIndex));
            const size_t skip = here();
            emitS32(0);
            compileExpression(param->defaultValue.get());
            const VarInfo& info = state.vars[param->name->name];
            emitCoerce(info.coerce);
            emitOp(info.isCell ? OpCode::OP_PUT_CELL : OpCode::OP_PUT_LOCAL);
            emitU16(info.index);
            patchJump(skip);
        }
        ++paramIndex;
    }

    // Nested function declarations are there from the start.
    for (FunctionDefinition* nested : scan.functions) {
        auto code = compileFunction(nested->name->name, nested->parameters, nested->returnType.get(),
                                    nested->body.get(), nullptr, false, false, false, nested->line);
        emitClosure(code);
        const VarInfo& info = state.vars[nested->name->name];
        emitOp(info.isCell ? OpCode::OP_PUT_CELL : OpCode::OP_PUT_LOCAL);
        emitU16(info.index);
    }

    if (isConstructor) {
        // Fields first, then the superclass's constructor unless the body
        // calls it itself.
        if (fieldInits) {
            for (const auto& init : *fieldInits) {
                emitOp(OpCode::OP_SCOPE_THIS);
                compileExpression(init.second);
                emitSetProp(init.first);
                emitOp(OpCode::OP_POP);
            }
        }
        if (!scan.callsSuper) {
            emitOp(OpCode::OP_SUPER_CTOR);
            emitU8(0);
            adjust(1);
            emitOp(OpCode::OP_POP);
        }
    }

    if (auto* block = dynamic_cast<BlockStatement*>(body)) {
        for (auto& stmt : block->statements) {
            compileStatement(stmt.get());
        }
    } else if (body) {
        compileStatement(body);
    }
    emitOp(OpCode::OP_RETURN_UNDEFINED);

    proto->maxStack = static_cast<uint16_t>(std::min(state.maxDepth + 8, 65535));
    fs = saved;
    currentLine = savedLine;
    proto->chunk.fuse();
    return proto;
}

void Compiler::emitClosure(const std::shared_ptr<BytecodeFunction>& function) {
    fs->proto->functions.push_back(function);
    emitOp(OpCode::OP_CLOSURE);
    emitU16(static_cast<uint16_t>(fs->proto->functions.size() - 1));
}

void Compiler::compileClass(ClassDefinition* definition, const std::shared_ptr<CompileUnit>& unit) {
    auto klass = std::make_shared<ClassTemplate>();
    klass->name = definition->name->name;
    klass->packageName = unit->packageName;
    klass->qualifiedName = unit->packageName.empty() ? klass->name : unit->packageName + "." + klass->name;
    klass->superName = definition->superClass ? definition->superClass->name : std::string();
    for (auto& face : definition->interfaces) {
        klass->interfaceNames.push_back(face->name);
    }
    klass->isInterface = definition->isInterface;
    klass->isDynamic = hasAttribute(definition->attributes, AttributeType::Dynamic);
    klass->isFinal = hasAttribute(definition->attributes, AttributeType::Final);
    klass->metadata = definition->metadata;
    klass->unit = unit;
    klass->line = definition->line;

    ClassContext context;
    context.name = klass->name;
    context.qualifiedName = klass->qualifiedName;
    FunctionDefinition* constructorDefinition = nullptr;
    for (auto& member : definition->members) {
        if (auto* function = dynamic_cast<FunctionDefinition*>(member.get())) {
            const bool isStatic = hasAttribute(function->attributes, AttributeType::Static);
            if (!isStatic && !function->isGetter && !function->isSetter && function->name->name == klass->name) {
                constructorDefinition = function;
                continue;
            }
            Coerce coerce = Coerce::None;
            if (function->isGetter && function->returnType) {
                coerce = coerceForType(function->returnType->name);
            }
            (isStatic ? context.staticMembers : context.instanceMembers)[function->name->name] = coerce;
            if (hasAttribute(function->attributes, AttributeType::Private)) {
                context.privateMembers.insert(function->name->name);
            }
        } else if (auto* variables = dynamic_cast<VariableDefinition*>(member.get())) {
            const bool isStatic = hasAttribute(variables->attributes, AttributeType::Static);
            for (auto& decl : variables->declarations) {
                (isStatic ? context.staticMembers : context.instanceMembers)[decl->id->name] =
                    coerceForType(decl->type ? decl->type->name : std::string());
                if (hasAttribute(variables->attributes, AttributeType::Private)) {
                    context.privateMembers.insert(decl->id->name);
                }
            }
        }
    }

    std::vector<std::pair<std::string, Expression*>> instanceInits;
    std::vector<StatementPtr> staticBody;
    for (auto& member : definition->members) {
        if (auto* function = dynamic_cast<FunctionDefinition*>(member.get())) {
            if (function == constructorDefinition) {
                continue;
            }
            MemberTemplate made;
            made.name = function->name->name;
            made.isStatic = hasAttribute(function->attributes, AttributeType::Static);
            made.isOverride = hasAttribute(function->attributes, AttributeType::Override);
            made.isFinal = hasAttribute(function->attributes, AttributeType::Final);
            made.isPrivate = hasAttribute(function->attributes, AttributeType::Private);
            made.isPublic = hasAttribute(function->attributes, AttributeType::Public);
            made.kind = function->isGetter ? MemberTemplate::Kind::Getter
                        : function->isSetter ? MemberTemplate::Kind::Setter
                                             : MemberTemplate::Kind::Method;
            made.typeName = function->returnType ? function->returnType->name : std::string();
            if (function->isSetter && !function->parameters.empty() && function->parameters[0]->type) {
                made.typeName = function->parameters[0]->type->name;
            }
            made.metadata = function->metadata;
            if (function->body) {
                made.function = compileFunction(made.name, function->parameters, function->returnType.get(),
                                                 function->body.get(), &context, made.isStatic, !made.isStatic,
                                                 false, function->line);
                made.function->qualifiedName = klass->qualifiedName + "/" + made.name;
            }
            klass->members.push_back(std::move(made));
        } else if (auto* variables = dynamic_cast<VariableDefinition*>(member.get())) {
            const bool isStatic = hasAttribute(variables->attributes, AttributeType::Static);
            for (auto& decl : variables->declarations) {
                MemberTemplate made;
                made.name = decl->id->name;
                made.kind = variables->isConst ? MemberTemplate::Kind::Const : MemberTemplate::Kind::Var;
                made.isStatic = isStatic;
                made.isPrivate = hasAttribute(variables->attributes, AttributeType::Private);
                made.isPublic = hasAttribute(variables->attributes, AttributeType::Public);
                made.typeName = decl->type ? decl->type->name : std::string();
                made.metadata = variables->metadata;
                made.hasLiteral = literalValue(decl->init.get(), made.literal);
                if (made.hasLiteral) {
                    made.literal = coerceValue(made.literal, coerceForType(made.typeName));
                }
                if (decl->init && !(made.hasLiteral && !isStatic)) {
                    if (isStatic) {
                        // ClassName.field = init, when the class is defined.
                        auto assign = std::make_unique<AssignmentExpression>(
                            OperatorType::Assign, std::make_unique<Identifier>(made.name), std::move(decl->init));
                        auto stmt = std::make_unique<ExpressionStatement>(std::move(assign));
                        stmt->line = variables->line;
                        staticBody.push_back(std::move(stmt));
                    } else {
                        instanceInits.emplace_back(made.name, decl->init.get());
                    }
                }
                klass->members.push_back(std::move(made));
            }
        } else if (auto* stmt = dynamic_cast<Statement*>(member.get())) {
            if (!dynamic_cast<NamespaceDefinition*>(stmt) && !dynamic_cast<EmptyStatement*>(stmt)) {
                staticBody.push_back(StatementPtr(static_cast<Statement*>(member.release())));
            }
        }
    }

    if (!klass->isInterface) {
        static const std::vector<std::unique_ptr<Parameter>> noParams;
        klass->constructor = compileFunction(
            klass->name, constructorDefinition ? constructorDefinition->parameters : noParams, nullptr,
            constructorDefinition ? constructorDefinition->body.get() : nullptr, &context, false, true, true,
            constructorDefinition ? constructorDefinition->line : definition->line, &instanceInits);
        klass->constructor->qualifiedName = klass->qualifiedName;
        if (!staticBody.empty()) {
            BlockStatement block(std::move(staticBody));
            klass->staticInit = compileFunction("$cinit", noParams, nullptr, &block, &context, true, false, false,
                                                definition->line);
            klass->staticInit->qualifiedName = klass->qualifiedName + "$cinit";
        }
    }

    fs->proto->classes.push_back(klass);
    emitOp(OpCode::OP_DEFINE_CLASS);
    emitU16(static_cast<uint16_t>(fs->proto->classes.size() - 1));
    emitOp(OpCode::OP_POP);
}

// Names

int Compiler::resolveUpvalue(FunctionState* state, const std::string& name) {
    auto known = state->upvalueIndex.find(name);
    if (known != state->upvalueIndex.end()) {
        return known->second;
    }
    FunctionState* parent = state->parent;
    if (!parent) {
        return -1;
    }
    UpvalueSource source{};
    auto var = parent->vars.find(name);
    if (var != parent->vars.end()) {
        if (!var->second.isCell) {
            return -1;
        }
        source.fromCell = true;
        source.index = var->second.index;
    } else {
        const int outer = resolveUpvalue(parent, name);
        if (outer < 0) {
            return -1;
        }
        source.fromCell = false;
        source.index = static_cast<uint16_t>(outer);
    }
    state->proto->upvalues.push_back(source);
    const uint16_t index = static_cast<uint16_t>(state->proto->upvalues.size() - 1);
    state->upvalueIndex[name] = index;
    return index;
}

Compiler::Resolved Compiler::resolve(const std::string& name) {
    Resolved where;
    auto var = fs->vars.find(name);
    if (var != fs->vars.end()) {
        where.kind = var->second.isCell ? NameKind::Cell : NameKind::Local;
        where.index = var->second.index;
        where.coerce = var->second.coerce;
        where.isConst = var->second.isConst;
        return where;
    }
    const int upvalue = resolveUpvalue(fs, name);
    if (upvalue >= 0) {
        where.kind = NameKind::Upvalue;
        where.index = static_cast<uint16_t>(upvalue);
        // Its type, from where it is declared.
        for (FunctionState* at = fs->parent; at; at = at->parent) {
            auto found = at->vars.find(name);
            if (found != at->vars.end()) {
                where.coerce = found->second.coerce;
                break;
            }
        }
        return where;
    }
    if (fs->cls) {
        if (!fs->isStatic) {
            auto member = fs->cls->instanceMembers.find(name);
            if (member != fs->cls->instanceMembers.end()) {
                where.kind = NameKind::Member;
                where.coerce = member->second;
                return where;
            }
        }
        auto member = fs->cls->staticMembers.find(name);
        if (member != fs->cls->staticMembers.end()) {
            where.kind = NameKind::Static;
            where.coerce = member->second;
            return where;
        }
        if (name == fs->cls->name) {
            where.kind = NameKind::OwnClass;
            return where;
        }
    }
    where.kind = NameKind::Global;
    for (FunctionState* at = fs; at; at = at->parent) {
        if (at->isScript) {
            auto typed = at->scriptVars.find(name);
            if (typed != at->scriptVars.end()) {
                where.coerce = typed->second;
            }
            break;
        }
    }
    return where;
}

void Compiler::emitLoad(const Resolved& where, const std::string& name) {
    switch (where.kind) {
        case NameKind::Local:
            emitOp(OpCode::OP_GET_LOCAL);
            emitU16(where.index);
            break;
        case NameKind::Cell:
            emitOp(OpCode::OP_GET_CELL);
            emitU16(where.index);
            break;
        case NameKind::Upvalue:
            emitOp(OpCode::OP_GET_UPVALUE);
            emitU16(where.index);
            break;
        case NameKind::Member:
            emitThisProp(OpCode::OP_GET_THIS_PROP, name);
            break;
        case NameKind::Static:
            emitOp(OpCode::OP_OWNER_CLASS);
            emitGetProp(name);
            break;
        case NameKind::OwnClass:
            emitOp(OpCode::OP_OWNER_CLASS);
            break;
        case NameKind::Global:
            emitOp(OpCode::OP_GET_NAME);
            emitU16(nameIndex(name));
            emitU16(newCache());
            break;
    }
}

void Compiler::emitStore(const Resolved& where, const std::string& name, bool keep) {
    if (where.isConst) {
        error("Cannot assign to the constant '" + name + "'");
    }
    switch (where.kind) {
        case NameKind::Local:
            emitCoerce(where.coerce);
            emitOp(keep ? OpCode::OP_SET_LOCAL : OpCode::OP_PUT_LOCAL);
            emitU16(where.index);
            break;
        case NameKind::Cell:
            emitCoerce(where.coerce);
            emitOp(keep ? OpCode::OP_SET_CELL : OpCode::OP_PUT_CELL);
            emitU16(where.index);
            break;
        case NameKind::Upvalue:
            emitCoerce(where.coerce);
            emitOp(keep ? OpCode::OP_SET_UPVALUE : OpCode::OP_PUT_UPVALUE);
            emitU16(where.index);
            break;
        case NameKind::Member:
            emitThisProp(OpCode::OP_SET_THIS_PROP, name);
            if (!keep) emitOp(OpCode::OP_POP);
            break;
        case NameKind::Static:
            emitOp(OpCode::OP_OWNER_CLASS);
            emitOp(OpCode::OP_SWAP);
            emitSetProp(name);
            if (!keep) emitOp(OpCode::OP_POP);
            break;
        case NameKind::OwnClass:
            error("Cannot assign to the class '" + name + "'");
            if (!keep) emitOp(OpCode::OP_POP);
            break;
        case NameKind::Global:
            emitCoerce(where.coerce);
            emitOp(OpCode::OP_SET_NAME);
            emitU16(nameIndex(name));
            emitU16(newCache());
            if (!keep) emitOp(OpCode::OP_POP);
            break;
    }
}

bool Compiler::qualifiedName(Expression* expr, std::string& name, std::vector<std::string>& rest) {
    std::vector<std::string> parts;
    Expression* at = expr;
    while (at && at->kind == Expression::ExpressionKind::Member) {
        auto* member = static_cast<MemberExpression*>(at);
        if (member->computed || !member->property) {
            return false;
        }
        parts.push_back(member->property->name);
        at = member->object.get();
    }
    if (!at || at->kind != Expression::ExpressionKind::Identifier || parts.empty()) {
        return false;
    }
    const std::string& root = static_cast<Identifier*>(at)->name;
    if (!isPackageRoot(root, fs->unit.get())) {
        return false;
    }
    const Resolved where = resolve(root);
    if (where.kind != NameKind::Global) {
        return false;
    }
    parts.push_back(root);
    std::reverse(parts.begin(), parts.end());
    // The package, then the class (its first capital): what follows is
    // its members.
    size_t classAt = parts.size() - 1;
    for (size_t i = 1; i < parts.size(); ++i) {
        if (!parts[i].empty() && std::isupper(static_cast<unsigned char>(parts[i][0]))) {
            classAt = i;
            break;
        }
    }
    name = parts[0];
    for (size_t i = 1; i <= classAt; ++i) {
        name += "." + parts[i];
    }
    rest.assign(parts.begin() + static_cast<long>(classAt) + 1, parts.end());
    return true;
}

// Statements

void Compiler::compileStatement(Statement* stmt) {
    if (!stmt) return;
    // What it compiles to is on its line.
    if (stmt->line > 0) {
        currentLine = stmt->line;
    }

    if (auto* block = dynamic_cast<BlockStatement*>(stmt)) {
        compileBlockStatement(block);
    } else if (auto* exprStmt = dynamic_cast<ExpressionStatement*>(stmt)) {
        compileDiscarded(exprStmt->expression.get());
    } else if (auto* ifStmt = dynamic_cast<IfStatement*>(stmt)) {
        compileIfStatement(ifStmt);
    } else if (auto* whileStmt = dynamic_cast<WhileStatement*>(stmt)) {
        compileWhileStatement(whileStmt, {});
    } else if (auto* doWhileStmt = dynamic_cast<DoWhileStatement*>(stmt)) {
        compileDoWhileStatement(doWhileStmt, {});
    } else if (auto* forStmt = dynamic_cast<ForStatement*>(stmt)) {
        compileForStatement(forStmt, {});
    } else if (auto* forInStmt = dynamic_cast<ForInStatement*>(stmt)) {
        compileForInStatement(forInStmt, {});
    } else if (auto* switchStmt = dynamic_cast<SwitchStatement*>(stmt)) {
        compileSwitchStatement(switchStmt, {});
    } else if (auto* labeled = dynamic_cast<LabeledStatement*>(stmt)) {
        compileLabeledStatement(labeled);
    } else if (auto* returnStmt = dynamic_cast<ReturnStatement*>(stmt)) {
        compileReturnStatement(returnStmt);
    } else if (auto* varDef = dynamic_cast<VariableDefinition*>(stmt)) {
        compileVariableDefinition(varDef);
    } else if (auto* funcDef = dynamic_cast<FunctionDefinition*>(stmt)) {
        if (fs->isScript) {
            // A function at the top level of a script: the timeline's, or
            // a global.
            auto code = compileFunction(funcDef->name->name, funcDef->parameters, funcDef->returnType.get(),
                                        funcDef->body.get(), nullptr, false, false, false, funcDef->line);
            emitClosure(code);
            emitOp(OpCode::OP_DEFINE_FUNCTION);
            emitU16(nameIndex(funcDef->name->name));
        }
        // In a function it was made at the start.
    } else if (auto* classDef = dynamic_cast<ClassDefinition*>(stmt)) {
        compileClass(classDef, fs->unit ? fs->unit : std::make_shared<CompileUnit>());
    } else if (auto* breakStmt = dynamic_cast<BreakStatement*>(stmt)) {
        compileBreakContinue(breakStmt->label ? breakStmt->label->name : std::string(), false);
    } else if (auto* continueStmt = dynamic_cast<ContinueStatement*>(stmt)) {
        compileBreakContinue(continueStmt->label ? continueStmt->label->name : std::string(), true);
    } else if (auto* throwStmt = dynamic_cast<ThrowStatement*>(stmt)) {
        compileExpression(throwStmt->argument.get());
        emitOp(OpCode::OP_THROW);
    } else if (auto* tryStmt = dynamic_cast<TryStatement*>(stmt)) {
        compileTryStatement(tryStmt);
    } else if (auto* with = dynamic_cast<WithStatement*>(stmt)) {
        // Names in the body are looked for on the object first.
        compileExpression(with->object.get());
        const uint16_t object = allocTemp();
        emitOp(OpCode::OP_PUT_LOCAL);
        emitU16(object);
        fs->withLocals.push_back(object);
        compileStatement(with->body.get());
        fs->withLocals.pop_back();
        emitOp(OpCode::OP_UNDEFINED);
        emitOp(OpCode::OP_PUT_LOCAL);
        emitU16(object);
    }
    // EmptyStatement, NamespaceDefinition do nothing
}

void Compiler::compileBlockStatement(BlockStatement* stmt) {
    for (auto& s : stmt->statements) {
        compileStatement(s.get());
    }
}

void Compiler::compileIfStatement(IfStatement* stmt) {
    compileExpression(stmt->test.get());
    const size_t elseJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    compileStatement(stmt->consequent.get());
    if (stmt->alternate) {
        const size_t endJump = emitJump(OpCode::OP_JUMP);
        patchJump(elseJump);
        compileStatement(stmt->alternate.get());
        patchJump(endJump);
    } else {
        patchJump(elseJump);
    }
}

void Compiler::compileLoopBody(Statement* body, const std::vector<std::string>& labels, bool isLoop) {
    ControlEntry entry;
    entry.kind = isLoop ? ControlEntry::Kind::Loop : ControlEntry::Kind::Switch;
    entry.labels = labels;
    fs->controls.push_back(std::move(entry));
    compileStatement(body);
}

void Compiler::compileWhileStatement(WhileStatement* stmt, const std::vector<std::string>& labels) {
    // The test at the bottom: one jump a time round.
    const size_t toTest = emitJump(OpCode::OP_JUMP);
    const size_t top = here();
    compileLoopBody(stmt->body.get(), labels, true);
    ControlEntry entry = std::move(fs->controls.back());
    fs->controls.pop_back();
    patchJump(toTest);
    for (size_t jump : entry.continueJumps) patchJump(jump);
    compileExpression(stmt->test.get());
    emitJumpTo(OpCode::OP_JUMP_IF_TRUE, top);
    for (size_t jump : entry.breakJumps) patchJump(jump);
}

void Compiler::compileDoWhileStatement(DoWhileStatement* stmt, const std::vector<std::string>& labels) {
    const size_t start = here();
    compileLoopBody(stmt->body.get(), labels, true);
    ControlEntry entry = std::move(fs->controls.back());
    fs->controls.pop_back();
    for (size_t jump : entry.continueJumps) patchJump(jump);
    compileExpression(stmt->test.get());
    emitJumpTo(OpCode::OP_JUMP_IF_TRUE, start);
    for (size_t jump : entry.breakJumps) patchJump(jump);
}

void Compiler::compileForStatement(ForStatement* stmt, const std::vector<std::string>& labels) {
    if (stmt->init) {
        compileStatement(stmt->init.get());
    }
    // The test at the bottom: one jump a time round.
    const size_t toTest = emitJump(OpCode::OP_JUMP);
    const size_t top = here();
    compileLoopBody(stmt->body.get(), labels, true);
    ControlEntry entry = std::move(fs->controls.back());
    fs->controls.pop_back();
    // continue goes on to the update, not past it.
    for (size_t jump : entry.continueJumps) patchJump(jump);
    if (stmt->update) {
        compileDiscarded(stmt->update.get());
    }
    patchJump(toTest);
    if (stmt->test) {
        compileExpression(stmt->test.get());
        emitJumpTo(OpCode::OP_JUMP_IF_TRUE, top);
    } else {
        emitJumpTo(OpCode::OP_JUMP, top);
    }
    for (size_t jump : entry.breakJumps) patchJump(jump);
}

void Compiler::compileForInStatement(ForInStatement* stmt, const std::vector<std::string>& labels) {
    compileExpression(stmt->right.get());
    emitOp(OpCode::OP_ITER_INIT);
    emitU8(stmt->each ? 1 : 0);
    const uint16_t iterator = allocTemp();
    emitOp(OpCode::OP_PUT_LOCAL);
    emitU16(iterator);

    const size_t start = here();
    emitOp(OpCode::OP_ITER_NEXT);
    emitU16(iterator);
    const size_t exitJump = here();
    emitS32(0);

    // The key (or value) into the variable.
    if (auto* variables = dynamic_cast<VariableDefinition*>(stmt->left.get())) {
        const auto& decl = variables->declarations.front();
        const std::string& name = decl->id->name;
        if (fs->isScript && !fs->vars.count(name)) {
            // A script's var: its type known from here on.
            fs->scriptVars[name] = coerceForType(decl->type ? decl->type->name : std::string());
        }
        emitStore(resolve(name), name, false);
    } else if (auto* target = dynamic_cast<ExpressionStatement*>(stmt->left.get())) {
        Expression* expr = target->expression.get();
        if (expr->kind == Expression::ExpressionKind::Identifier) {
            const std::string& name = static_cast<Identifier*>(expr)->name;
            emitStore(resolve(name), name, false);
        } else if (expr->kind == Expression::ExpressionKind::Member) {
            auto* member = static_cast<MemberExpression*>(expr);
            const uint16_t value = allocTemp();
            emitOp(OpCode::OP_PUT_LOCAL);
            emitU16(value);
            compileExpression(member->object.get());
            if (member->computed) {
                compileExpression(member->computedProperty.get());
                emitOp(OpCode::OP_GET_LOCAL);
                emitU16(value);
                emitOp(OpCode::OP_SET_INDEX);
            } else {
                emitOp(OpCode::OP_GET_LOCAL);
                emitU16(value);
                emitSetProp(member->property->name);
            }
            emitOp(OpCode::OP_POP);
        } else {
            error("Invalid target in for-in");
            emitOp(OpCode::OP_POP);
        }
    }

    compileLoopBody(stmt->body.get(), labels, true);
    ControlEntry entry = std::move(fs->controls.back());
    fs->controls.pop_back();
    for (size_t jump : entry.continueJumps) patchJumpTo(jump, start);
    emitJumpTo(OpCode::OP_JUMP, start);
    patchJump(exitJump);
    for (size_t jump : entry.breakJumps) patchJump(jump);
    // Let go of the iterator.
    emitOp(OpCode::OP_UNDEFINED);
    emitOp(OpCode::OP_PUT_LOCAL);
    emitU16(iterator);
}

void Compiler::compileSwitchStatement(SwitchStatement* stmt, const std::vector<std::string>& labels) {
    compileExpression(stmt->discriminant.get());
    const uint16_t value = allocTemp();
    emitOp(OpCode::OP_PUT_LOCAL);
    emitU16(value);

    // The tests, in order, each jumping to its body; then default.
    std::vector<size_t> caseJumps(stmt->cases.size(), 0);
    for (size_t i = 0; i < stmt->cases.size(); ++i) {
        CaseClause* clause = stmt->cases[i].get();
        if (!clause->test) continue;
        if (clause->line > 0) currentLine = clause->line;
        emitOp(OpCode::OP_GET_LOCAL);
        emitU16(value);
        compileExpression(clause->test.get());
        emitOp(OpCode::OP_STRICT_EQUAL);
        caseJumps[i] = emitJump(OpCode::OP_JUMP_IF_TRUE);
    }
    const size_t defaultJump = emitJump(OpCode::OP_JUMP);
    bool hasDefault = false;

    ControlEntry entry;
    entry.kind = ControlEntry::Kind::Switch;
    entry.labels = labels;
    fs->controls.push_back(std::move(entry));
    for (size_t i = 0; i < stmt->cases.size(); ++i) {
        CaseClause* clause = stmt->cases[i].get();
        if (clause->test) {
            patchJump(caseJumps[i]);
        } else {
            patchJump(defaultJump);
            hasDefault = true;
        }
        for (auto& s : clause->consequent) {
            compileStatement(s.get());
        }
    }
    ControlEntry done = std::move(fs->controls.back());
    fs->controls.pop_back();
    if (!hasDefault) patchJump(defaultJump);
    for (size_t jump : done.breakJumps) patchJump(jump);
}

void Compiler::compileLabeledStatement(LabeledStatement* stmt) {
    std::vector<std::string> labels;
    Statement* body = stmt;
    while (auto* labeled = dynamic_cast<LabeledStatement*>(body)) {
        labels.push_back(labeled->label->name);
        body = labeled->body.get();
    }
    if (body && body->line > 0) currentLine = body->line;
    if (auto* loop = dynamic_cast<WhileStatement*>(body)) {
        compileWhileStatement(loop, labels);
    } else if (auto* doLoop = dynamic_cast<DoWhileStatement*>(body)) {
        compileDoWhileStatement(doLoop, labels);
    } else if (auto* forLoop = dynamic_cast<ForStatement*>(body)) {
        compileForStatement(forLoop, labels);
    } else if (auto* forIn = dynamic_cast<ForInStatement*>(body)) {
        compileForInStatement(forIn, labels);
    } else if (auto* choice = dynamic_cast<SwitchStatement*>(body)) {
        compileSwitchStatement(choice, labels);
    } else {
        ControlEntry entry;
        entry.kind = ControlEntry::Kind::Label;
        entry.labels = labels;
        fs->controls.push_back(std::move(entry));
        compileStatement(body);
        ControlEntry done = std::move(fs->controls.back());
        fs->controls.pop_back();
        for (size_t jump : done.breakJumps) patchJump(jump);
    }
}

void Compiler::pauseRegions(size_t from) {
    for (size_t i = from; i < fs->controls.size(); ++i) {
        ControlEntry& entry = fs->controls[i];
        if (entry.kind == ControlEntry::Kind::Try && entry.regionOpen) {
            if (here() > entry.regionStart) {
                entry.ranges.emplace_back(entry.regionStart, static_cast<uint32_t>(here()));
            }
            entry.regionOpen = false;
            entry.paused = true;
        }
    }
}

void Compiler::resumeRegions(size_t from) {
    for (size_t i = from; i < fs->controls.size(); ++i) {
        ControlEntry& entry = fs->controls[i];
        if (entry.kind == ControlEntry::Kind::Try && entry.paused) {
            entry.regionOpen = true;
            entry.paused = false;
            entry.regionStart = static_cast<uint32_t>(here());
        }
    }
}

// A try's finally block, run on the way out of it by a break, continue or
// return: outside the handlers of the tries it leaves, and with the
// statements it may break to being only those outside the try.
void Compiler::emitFinallyFor(size_t entryIndex) {
    Statement* body = fs->controls[entryIndex].finallyBody;
    if (!body) {
        return;
    }
    pauseRegions(entryIndex);
    std::vector<ControlEntry> inner(std::make_move_iterator(fs->controls.begin() + static_cast<long>(entryIndex)),
                                    std::make_move_iterator(fs->controls.end()));
    fs->controls.erase(fs->controls.begin() + static_cast<long>(entryIndex), fs->controls.end());
    const int savedLine = currentLine;
    compileStatement(body);
    currentLine = savedLine;
    for (auto& entry : inner) {
        fs->controls.push_back(std::move(entry));
    }
    resumeRegions(entryIndex);
}

void Compiler::compileBreakContinue(const std::string& label, bool isContinue) {
    int target = -1;
    for (int i = static_cast<int>(fs->controls.size()) - 1; i >= 0; --i) {
        const ControlEntry& entry = fs->controls[static_cast<size_t>(i)];
        if (entry.kind == ControlEntry::Kind::Try) continue;
        if (label.empty()) {
            if (entry.kind == ControlEntry::Kind::Loop ||
                (!isContinue && entry.kind == ControlEntry::Kind::Switch)) {
                target = i;
                break;
            }
        } else if (std::find(entry.labels.begin(), entry.labels.end(), label) != entry.labels.end()) {
            if (isContinue && entry.kind != ControlEntry::Kind::Loop) {
                error("continue " + label + ": the label is not a loop's");
                return;
            }
            target = i;
            break;
        }
    }
    if (target < 0) {
        error(std::string(isContinue ? "continue" : "break") +
              (label.empty() ? " outside a loop" : " to an unknown label '" + label + "'"));
        return;
    }
    for (int i = static_cast<int>(fs->controls.size()) - 1; i > target; --i) {
        if (fs->controls[static_cast<size_t>(i)].kind == ControlEntry::Kind::Try) {
            emitFinallyFor(static_cast<size_t>(i));
        }
    }
    const size_t jump = emitJump(OpCode::OP_JUMP);
    if (isContinue) {
        fs->controls[static_cast<size_t>(target)].continueJumps.push_back(jump);
    } else {
        fs->controls[static_cast<size_t>(target)].breakJumps.push_back(jump);
    }
}

void Compiler::compileReturnStatement(ReturnStatement* stmt) {
    if (fs->isScript && !fs->parent) {
        // A return in a frame script ends it.
    }
    bool finallyPending = false;
    for (const auto& entry : fs->controls) {
        if (entry.kind == ControlEntry::Kind::Try && entry.finallyBody) finallyPending = true;
    }
    if (stmt->argument) {
        compileExpression(stmt->argument.get());
        emitCoerce(fs->proto->returnCoerce);
    } else {
        emitOp(OpCode::OP_UNDEFINED);
    }
    if (finallyPending) {
        const uint16_t value = allocTemp();
        emitOp(OpCode::OP_PUT_LOCAL);
        emitU16(value);
        for (int i = static_cast<int>(fs->controls.size()) - 1; i >= 0; --i) {
            if (fs->controls[static_cast<size_t>(i)].kind == ControlEntry::Kind::Try) {
                emitFinallyFor(static_cast<size_t>(i));
            }
        }
        emitOp(OpCode::OP_GET_LOCAL);
        emitU16(value);
    }
    emitOp(OpCode::OP_RETURN);
}

void Compiler::compileTryStatement(TryStatement* stmt) {
    const bool hasCatch = !stmt->catches.empty();
    Statement* finallyBody = stmt->finallyBody.get();
    const int line = currentLine;

    const auto close = [this](ControlEntry& entry) {
        if (entry.regionOpen && here() > entry.regionStart) {
            entry.ranges.emplace_back(entry.regionStart, static_cast<uint32_t>(here()));
        }
        entry.regionOpen = false;
    };

    ControlEntry tryEntry;
    tryEntry.kind = ControlEntry::Kind::Try;
    tryEntry.finallyBody = finallyBody;
    tryEntry.regionOpen = true;
    tryEntry.regionStart = static_cast<uint32_t>(here());
    fs->controls.push_back(std::move(tryEntry));
    const size_t entryIndex = fs->controls.size() - 1;

    compileStatement(stmt->block.get());
    close(fs->controls[entryIndex]);
    std::vector<std::pair<uint32_t, uint32_t>> tryRanges = std::move(fs->controls[entryIndex].ranges);
    fs->controls[entryIndex].ranges.clear();

    std::vector<size_t> endJumps;
    // Done without an exception: the finally block, then on.
    if (finallyBody) {
        emitFinallyFor(entryIndex);
    }
    endJumps.push_back(emitJump(OpCode::OP_JUMP));

    if (hasCatch) {
        const size_t catchStart = here();
        for (const auto& range : tryRanges) {
            fs->proto->handlers.push_back(Handler{range.first, range.second, static_cast<uint32_t>(catchStart)});
        }
        tryRanges.clear();
        // The catch blocks are covered by the finally handler.
        ControlEntry& entry = fs->controls[entryIndex];
        entry.regionOpen = finallyBody != nullptr;
        entry.regionStart = static_cast<uint32_t>(here());
        fs->depth = 0;
        adjust(1);
        const uint16_t exception = allocTemp();
        emitOp(OpCode::OP_PUT_LOCAL);
        emitU16(exception);
        for (auto& clause : stmt->catches) {
            currentLine = clause.body && clause.body->line > 0 ? clause.body->line : line;
            size_t nextJump = 0;
            const bool typed = !clause.type.empty() && clause.type != "*" && clause.type != "Object";
            if (typed) {
                emitOp(OpCode::OP_GET_LOCAL);
                emitU16(exception);
                compileTypeExpression(clause.type);
                emitOp(OpCode::OP_IS);
                nextJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
            }
            emitOp(OpCode::OP_GET_LOCAL);
            emitU16(exception);
            const std::string& name = clause.variable->name;
            if (fs->isScript && !fs->vars.count(name)) {
                // In a script, the exception is kept in a local of its own.
                VarInfo local;
                local.index = allocTemp();
                fs->vars[name] = local;
            }
            Resolved where = resolve(name);
            where.coerce = Coerce::None;
            emitStore(where, name, false);
            compileStatement(clause.body.get());
            if (finallyBody) {
                emitFinallyFor(entryIndex);
            }
            endJumps.push_back(emitJump(OpCode::OP_JUMP));
            if (typed) {
                patchJump(nextJump);
            }
        }
        // No clause took it: on to the next handler (through finally).
        emitOp(OpCode::OP_GET_LOCAL);
        emitU16(exception);
        emitOp(OpCode::OP_THROW);
        close(fs->controls[entryIndex]);
        tryRanges = std::move(fs->controls[entryIndex].ranges);
    }

    fs->controls.pop_back();

    if (finallyBody) {
        const size_t finallyStart = here();
        for (const auto& range : tryRanges) {
            fs->proto->handlers.push_back(Handler{range.first, range.second, static_cast<uint32_t>(finallyStart)});
        }
        fs->depth = 0;
        adjust(1);
        const uint16_t exception = allocTemp();
        emitOp(OpCode::OP_PUT_LOCAL);
        emitU16(exception);
        compileStatement(finallyBody);
        emitOp(OpCode::OP_GET_LOCAL);
        emitU16(exception);
        emitOp(OpCode::OP_THROW);
    }

    for (size_t jump : endJumps) {
        patchJump(jump);
    }
    fs->depth = 0;
    currentLine = line;
}

void Compiler::compileVariableDefinition(VariableDefinition* stmt) {
    for (auto& decl : stmt->declarations) {
        const std::string& name = decl->id->name;
        const Coerce coerce = coerceForType(decl->type ? decl->type->name : std::string());
        if (fs->isScript && !fs->vars.count(name)) {
            // A var at the top level of a script: the timeline's, or a
            // global; one without a value is only made if not there.
            fs->scriptVars[name] = coerce;
            if (decl->init) {
                compileExpression(decl->init.get());
                emitCoerce(coerce);
                emitOp(OpCode::OP_SET_NAME);
                emitU16(nameIndex(name));
                emitU16(newCache());
                emitOp(OpCode::OP_POP);
            } else {
                emitConstant(defaultForCoerce(coerce));
                emitOp(OpCode::OP_DEFINE_VAR);
                emitU16(nameIndex(name));
            }
            continue;
        }
        if (!decl->init) {
            // It was given its type's starting value on entry.
            continue;
        }
        Resolved where = resolve(name);
        if (stmt->isConst) {
            where.isConst = false;
        }
        compileExpression(decl->init.get());
        emitStore(where, name, false);
        if (stmt->isConst) {
            auto found = fs->vars.find(name);
            if (found != fs->vars.end()) found->second.isConst = true;
        }
    }
}

// Expressions

void Compiler::compileDiscarded(Expression* expr) {
    if (!expr) return;
    if (expr->line > 0) currentLine = expr->line;
    if (expr->kind == Expression::ExpressionKind::Assignment) {
        compileAssignment(static_cast<AssignmentExpression*>(expr), true);
        return;
    }
    if (expr->kind == Expression::ExpressionKind::Unary) {
        auto* unary = static_cast<UnaryExpression*>(expr);
        if (unary->op == OperatorType::PreIncrement || unary->op == OperatorType::PostIncrement ||
            unary->op == OperatorType::PreDecrement || unary->op == OperatorType::PostDecrement) {
            compileUnaryExpression(unary, true);
            return;
        }
    }
    if (expr->kind == Expression::ExpressionKind::Sequence) {
        for (auto& part : static_cast<SequenceExpression*>(expr)->expressions) {
            compileDiscarded(part.get());
        }
        return;
    }
    compileExpression(expr);
    emitOp(OpCode::OP_POP);
}

void Compiler::compileExpression(Expression* expr) {
    if (!expr) {
        emitOp(OpCode::OP_UNDEFINED);
        return;
    }
    if (expr->line > 0) currentLine = expr->line;

    switch (expr->kind) {
        case Expression::ExpressionKind::Identifier:
            compileIdentifier(static_cast<Identifier*>(expr));
            break;
        case Expression::ExpressionKind::Literal:
            compileLiteral(static_cast<Literal*>(expr));
            break;
        case Expression::ExpressionKind::Binary:
            compileBinaryExpression(static_cast<BinaryExpression*>(expr));
            break;
        case Expression::ExpressionKind::Unary:
            compileUnaryExpression(static_cast<UnaryExpression*>(expr), false);
            break;
        case Expression::ExpressionKind::Conditional: {
            auto* conditional = static_cast<ConditionalExpression*>(expr);
            compileExpression(conditional->test.get());
            const size_t elseJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
            compileExpression(conditional->consequent.get());
            const size_t endJump = emitJump(OpCode::OP_JUMP);
            patchJump(elseJump);
            adjust(-1);
            compileExpression(conditional->alternate.get());
            patchJump(endJump);
            break;
        }
        case Expression::ExpressionKind::Assignment:
            compileAssignment(static_cast<AssignmentExpression*>(expr), false);
            break;
        case Expression::ExpressionKind::Call:
            compileCallExpression(static_cast<CallExpression*>(expr));
            break;
        case Expression::ExpressionKind::New:
            compileNewExpression(static_cast<NewExpression*>(expr));
            break;
        case Expression::ExpressionKind::Member:
            compileMemberExpression(static_cast<MemberExpression*>(expr));
            break;
        case Expression::ExpressionKind::Function:
            compileFunctionExpression(static_cast<FunctionExpression*>(expr));
            break;
        case Expression::ExpressionKind::Array:
            compileArrayExpression(static_cast<ArrayExpression*>(expr));
            break;
        case Expression::ExpressionKind::Object:
            compileObjectExpression(static_cast<ObjectExpression*>(expr));
            break;
        case Expression::ExpressionKind::This:
            emitOp(OpCode::OP_THIS);
            break;
        case Expression::ExpressionKind::Super:
            // super alone: this, as its superclass sees it.
            emitOp(OpCode::OP_THIS);
            break;
        case Expression::ExpressionKind::Sequence: {
            auto& parts = static_cast<SequenceExpression*>(expr)->expressions;
            for (size_t i = 0; i < parts.size(); ++i) {
                if (i + 1 < parts.size()) {
                    compileDiscarded(parts[i].get());
                } else {
                    compileExpression(parts[i].get());
                }
            }
            break;
        }
        case Expression::ExpressionKind::TypeApplication: {
            auto* applied = static_cast<TypeApplication*>(expr);
            compileExpression(applied->base.get());
            for (const auto& type : applied->typeArguments) {
                compileTypeExpression(type);
            }
            emitOp(OpCode::OP_APPLY_TYPE);
            emitU8(static_cast<uint8_t>(applied->typeArguments.size()));
            adjust(-static_cast<int>(applied->typeArguments.size()));
            break;
        }
    }
}

void Compiler::compileTypeExpression(const std::string& typeName) {
    if (typeName == "*" || typeName.empty()) {
        emitOp(OpCode::OP_NULL);
        return;
    }
    const size_t open = typeName.find(".<");
    if (open != std::string::npos && typeName.back() == '>') {
        compileTypeExpression(typeName.substr(0, open));
        // The arguments, split where not nested.
        const std::string inner = typeName.substr(open + 2, typeName.size() - open - 3);
        std::vector<std::string> args;
        int depth = 0;
        std::string part;
        for (char c : inner) {
            if (c == '<') ++depth;
            if (c == '>') --depth;
            if (c == ',' && depth == 0) {
                args.push_back(part);
                part.clear();
            } else {
                part += c;
            }
        }
        args.push_back(part);
        for (const auto& arg : args) {
            compileTypeExpression(arg);
        }
        emitOp(OpCode::OP_APPLY_TYPE);
        emitU8(static_cast<uint8_t>(args.size()));
        adjust(-static_cast<int>(args.size()));
        return;
    }
    if (typeName.find('.') != std::string::npos) {
        emitOp(OpCode::OP_GET_QNAME);
        emitU16(nameIndex(typeName));
        return;
    }
    emitLoad(resolve(typeName), typeName);
}

void Compiler::compileIdentifier(Identifier* expr) {
    const std::string& name = expr->name;
    if (!fs->withLocals.empty()) {
        // On a with object if one has it, else as it would be.
        const size_t found = emitWith(OpCode::OP_WITH_GET, name);
        std::vector<uint16_t> saved;
        saved.swap(fs->withLocals);
        compileIdentifier(expr);
        fs->withLocals.swap(saved);
        patchJump(found);
        return;
    }
    const Resolved where = resolve(name);
    if (where.kind == NameKind::Global) {
        if (name == "undefined") {
            emitOp(OpCode::OP_UNDEFINED);
            return;
        }
        if (name == "NaN") {
            emitConstant(Val::number(std::numeric_limits<double>::quiet_NaN()));
            return;
        }
        if (name == "Infinity") {
            emitConstant(Val::number(std::numeric_limits<double>::infinity()));
            return;
        }
    }
    emitLoad(where, name);
}

void Compiler::compileLiteral(Literal* expr) {
    switch (expr->kind) {
        case Literal::LiteralKind::Null:
            emitOp(OpCode::OP_NULL);
            break;
        case Literal::LiteralKind::Undefined:
            emitOp(OpCode::OP_UNDEFINED);
            break;
        case Literal::LiteralKind::Boolean:
            emitOp(expr->boolValue ? OpCode::OP_TRUE : OpCode::OP_FALSE);
            break;
        case Literal::LiteralKind::Numeric:
            emitNumber(expr->numberValue);
            break;
        case Literal::LiteralKind::String:
            emitString(expr->stringValue);
            break;
        case Literal::LiteralKind::RegExp: {
            const uint16_t pattern = fs->proto->chunk.addConstant(Val::string(expr->stringValue));
            const uint16_t flags = fs->proto->chunk.addConstant(Val::string(expr->flags));
            emitOp(OpCode::OP_NEW_REGEXP);
            emitU16(pattern);
            emitU16(flags);
            break;
        }
        default:
            error("Unsupported literal");
            emitOp(OpCode::OP_UNDEFINED);
            break;
    }
}

bool Compiler::emitBinaryOperator(OperatorType op) {
    switch (op) {
        case OperatorType::Add: emitOp(OpCode::OP_ADD); return true;
        case OperatorType::Subtract: emitOp(OpCode::OP_SUBTRACT); return true;
        case OperatorType::Multiply: emitOp(OpCode::OP_MULTIPLY); return true;
        case OperatorType::Divide: emitOp(OpCode::OP_DIVIDE); return true;
        case OperatorType::Remainder: emitOp(OpCode::OP_MODULO); return true;
        case OperatorType::ShiftLeft: emitOp(OpCode::OP_LEFT_SHIFT); return true;
        case OperatorType::ShiftRight: emitOp(OpCode::OP_RIGHT_SHIFT); return true;
        case OperatorType::ShiftRightUnsigned: emitOp(OpCode::OP_UNSIGNED_RIGHT_SHIFT); return true;
        case OperatorType::Lt: emitOp(OpCode::OP_LESS); return true;
        case OperatorType::Gt: emitOp(OpCode::OP_GREATER); return true;
        case OperatorType::Le: emitOp(OpCode::OP_LESS_EQUAL); return true;
        case OperatorType::Ge: emitOp(OpCode::OP_GREATER_EQUAL); return true;
        case OperatorType::Equals: emitOp(OpCode::OP_EQUAL); return true;
        case OperatorType::NotEquals: emitOp(OpCode::OP_NOT_EQUAL); return true;
        case OperatorType::StrictEquals: emitOp(OpCode::OP_STRICT_EQUAL); return true;
        case OperatorType::StrictNotEquals: emitOp(OpCode::OP_STRICT_NOT_EQUAL); return true;
        case OperatorType::BitwiseAnd: emitOp(OpCode::OP_BIT_AND); return true;
        case OperatorType::BitwiseOr: emitOp(OpCode::OP_BIT_OR); return true;
        case OperatorType::BitwiseXor: emitOp(OpCode::OP_BIT_XOR); return true;
        case OperatorType::Instanceof: emitOp(OpCode::OP_INSTANCEOF); return true;
        case OperatorType::In: emitOp(OpCode::OP_IN); return true;
        case OperatorType::Is: emitOp(OpCode::OP_IS); return true;
        case OperatorType::As: emitOp(OpCode::OP_AS); return true;
        default:
            return false;
    }
}

void Compiler::compileBinaryExpression(BinaryExpression* expr) {
    switch (expr->op) {
        case OperatorType::LogicalAnd: {
            compileExpression(expr->left.get());
            const size_t end = emitJump(OpCode::OP_JUMP_IF_FALSE_KEEP);
            compileExpression(expr->right.get());
            patchJump(end);
            return;
        }
        case OperatorType::LogicalOr: {
            compileExpression(expr->left.get());
            const size_t end = emitJump(OpCode::OP_JUMP_IF_TRUE_KEEP);
            compileExpression(expr->right.get());
            patchJump(end);
            return;
        }
        case OperatorType::NullCoalescing: {
            compileExpression(expr->left.get());
            const size_t end = emitJump(OpCode::OP_JUMP_IF_NOT_NULLISH_KEEP);
            compileExpression(expr->right.get());
            patchJump(end);
            return;
        }
        case OperatorType::LogicalXor:
            compileExpression(expr->left.get());
            emitOp(OpCode::OP_NOT);
            compileExpression(expr->right.get());
            emitOp(OpCode::OP_NOT);
            emitOp(OpCode::OP_NOT_EQUAL);
            return;
        case OperatorType::Is:
        case OperatorType::As:
        case OperatorType::Instanceof: {
            compileExpression(expr->left.get());
            // The type: a name, a.b.C or Vector.<T>, as a type.
            std::string qualified;
            std::vector<std::string> rest;
            if (expr->right->kind == Expression::ExpressionKind::Member &&
                qualifiedName(expr->right.get(), qualified, rest) && rest.empty()) {
                emitOp(OpCode::OP_GET_QNAME);
                emitU16(nameIndex(qualified));
            } else {
                compileExpression(expr->right.get());
            }
            emitBinaryOperator(expr->op);
            return;
        }
        default:
            break;
    }
    // Folded when both sides are numbers.
    Val left;
    Val right;
    if (literalValue(expr->left.get(), left) && literalValue(expr->right.get(), right) && left.isNumber() &&
        right.isNumber()) {
        double value = 0;
        bool folded = true;
        switch (expr->op) {
            case OperatorType::Add: value = left.num + right.num; break;
            case OperatorType::Subtract: value = left.num - right.num; break;
            case OperatorType::Multiply: value = left.num * right.num; break;
            case OperatorType::Divide: value = left.num / right.num; break;
            case OperatorType::ShiftLeft: value = toInt32(left.num) << (toUint32(right.num) & 31); break;
            case OperatorType::BitwiseOr: value = toInt32(left.num) | toInt32(right.num); break;
            case OperatorType::BitwiseAnd: value = toInt32(left.num) & toInt32(right.num); break;
            default: folded = false; break;
        }
        if (folded) {
            emitNumber(value);
            return;
        }
    }
    // A chain of plain operators (a + b + c + ...): the left-most first,
    // then each right and its operator, without recursing down the chain.
    std::vector<BinaryExpression*> chain{expr};
    while (true) {
        Expression* left = chain.back()->left.get();
        if (!left || left->kind != Expression::ExpressionKind::Binary) break;
        auto* inner = static_cast<BinaryExpression*>(left);
        switch (inner->op) {
            case OperatorType::LogicalAnd: case OperatorType::LogicalOr: case OperatorType::NullCoalescing:
            case OperatorType::LogicalXor: case OperatorType::Is: case OperatorType::As:
            case OperatorType::Instanceof:
                break;
            default:
                chain.push_back(inner);
                continue;
        }
        break;
    }
    if (chain.size() > 1) {
        compileExpression(chain.back()->left.get());
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            compileExpression((*it)->right.get());
            if (!emitBinaryOperator((*it)->op)) {
                error("Unsupported binary operator");
            }
        }
        return;
    }
    compileExpression(expr->left.get());
    compileExpression(expr->right.get());
    if (!emitBinaryOperator(expr->op)) {
        error("Unsupported binary operator");
    }
}

void Compiler::compileUnaryExpression(UnaryExpression* expr, bool discard) {
    switch (expr->op) {
        case OperatorType::PreIncrement:
            compileIncrement(expr->operand.get(), true, false, discard);
            return;
        case OperatorType::PreDecrement:
            compileIncrement(expr->operand.get(), false, false, discard);
            return;
        case OperatorType::PostIncrement:
            compileIncrement(expr->operand.get(), true, true, discard);
            return;
        case OperatorType::PostDecrement:
            compileIncrement(expr->operand.get(), false, true, discard);
            return;
        case OperatorType::Typeof:
            if (expr->operand->kind == Expression::ExpressionKind::Identifier) {
                const std::string& name = static_cast<Identifier*>(expr->operand.get())->name;
                if (resolve(name).kind == NameKind::Global) {
                    emitOp(OpCode::OP_TYPEOF_NAME);
                    emitU16(nameIndex(name));
                    return;
                }
            }
            compileExpression(expr->operand.get());
            emitOp(OpCode::OP_TYPEOF);
            return;
        case OperatorType::Delete: {
            Expression* target = expr->operand.get();
            if (target->kind == Expression::ExpressionKind::Member) {
                auto* member = static_cast<MemberExpression*>(target);
                compileExpression(member->object.get());
                if (member->computed) {
                    compileExpression(member->computedProperty.get());
                    emitOp(OpCode::OP_DELETE_INDEX);
                } else {
                    emitOp(OpCode::OP_DELETE_PROP);
                    emitU16(nameIndex(member->property->name));
                }
                return;
            }
            compileDiscarded(target);
            emitOp(OpCode::OP_FALSE);
            return;
        }
        case OperatorType::Void:
            compileDiscarded(expr->operand.get());
            emitOp(OpCode::OP_UNDEFINED);
            return;
        default:
            break;
    }
    compileExpression(expr->operand.get());
    switch (expr->op) {
        case OperatorType::Negative: emitOp(OpCode::OP_NEGATE); break;
        case OperatorType::Positive: emitOp(OpCode::OP_TO_NUMBER); break;
        case OperatorType::LogicalNot: emitOp(OpCode::OP_NOT); break;
        case OperatorType::BitwiseNot: emitOp(OpCode::OP_BIT_NOT); break;
        default:
            error("Unsupported unary operator");
            break;
    }
}

void Compiler::compileIncrement(Expression* target, bool increment, bool postfix, bool discard) {
    const OpCode step = increment ? OpCode::OP_INCREMENT : OpCode::OP_DECREMENT;
    const bool keepOld = postfix && !discard;
    if (target->kind == Expression::ExpressionKind::Identifier && !fs->withLocals.empty()) {
        const std::string& name = static_cast<Identifier*>(target)->name;
        compileIdentifier(static_cast<Identifier*>(target));
        if (keepOld) {
            emitOp(OpCode::OP_TO_NUMBER);
            emitOp(OpCode::OP_DUP);
        }
        emitOp(step);
        const size_t found = emitWith(OpCode::OP_WITH_SET, name);
        std::vector<uint16_t> saved;
        saved.swap(fs->withLocals);
        emitStore(resolve(name), name, true);
        fs->withLocals.swap(saved);
        patchJump(found);
        if (keepOld || discard) emitOp(OpCode::OP_POP);
        return;
    }
    if (target->kind == Expression::ExpressionKind::Identifier) {
        const std::string& name = static_cast<Identifier*>(target)->name;
        const Resolved where = resolve(name);
        if (where.kind == NameKind::Local && discard) {
            emitOp(OpCode::OP_INC_LOCAL);
            emitU16(where.index);
            emitU8(static_cast<uint8_t>(increment ? 129 : 127));
            emitU8(static_cast<uint8_t>(where.coerce));
            return;
        }
        if (where.kind == NameKind::Member) {
            emitThisProp(OpCode::OP_GET_THIS_PROP, name);
            if (keepOld) {
                emitOp(OpCode::OP_TO_NUMBER);
                emitOp(OpCode::OP_DUP);
            }
            emitOp(step);
            emitThisProp(OpCode::OP_SET_THIS_PROP, name);
            if (keepOld || discard) emitOp(OpCode::OP_POP);
            return;
        }
        if (where.kind == NameKind::Static) {
            emitOp(OpCode::OP_OWNER_CLASS);
            emitOp(OpCode::OP_DUP);
            emitGetProp(name);
            if (keepOld) {
                emitOp(OpCode::OP_TO_NUMBER);
                emitOp(OpCode::OP_DUP_X1);
            }
            emitOp(step);
            emitSetProp(name);
            if (keepOld || discard) emitOp(OpCode::OP_POP);
            return;
        }
        emitLoad(where, name);
        if (keepOld) {
            emitOp(OpCode::OP_TO_NUMBER);
            emitOp(OpCode::OP_DUP);
            emitOp(step);
            emitStore(where, name, false);
        } else {
            emitOp(step);
            emitStore(where, name, !discard);
        }
        return;
    }
    if (target->kind == Expression::ExpressionKind::Member) {
        auto* member = static_cast<MemberExpression*>(target);
        if (member->object && member->object->kind == Expression::ExpressionKind::Super && !member->computed) {
            emitOp(OpCode::OP_GET_SUPER);
            emitU16(nameIndex(member->property->name));
            if (keepOld) {
                emitOp(OpCode::OP_TO_NUMBER);
                emitOp(OpCode::OP_DUP);
            }
            emitOp(step);
            emitOp(OpCode::OP_SET_SUPER);
            emitU16(nameIndex(member->property->name));
            if (keepOld || discard) emitOp(OpCode::OP_POP);
            return;
        }
        uint16_t slot = 0;
        const bool onLocal = !member->computed && localObject(member->object.get(), slot);
        if (onLocal || (!member->computed && thisObject(member->object.get()))) {
            const std::string& name = member->property->name;
            if (onLocal) {
                emitLocalProp(OpCode::OP_GET_LOCAL_PROP, slot, name);
            } else {
                emitThisProp(OpCode::OP_GET_THIS_PROP, name);
            }
            if (keepOld) {
                emitOp(OpCode::OP_TO_NUMBER);
                emitOp(OpCode::OP_DUP);
            }
            emitOp(step);
            if (onLocal) {
                emitLocalProp(OpCode::OP_SET_LOCAL_PROP, slot, name);
            } else {
                emitThisProp(OpCode::OP_SET_THIS_PROP, name);
            }
            if (keepOld || discard) emitOp(OpCode::OP_POP);
            return;
        }
        compileExpression(member->object.get());
        if (member->computed) {
            compileExpression(member->computedProperty.get());
            emitOp(OpCode::OP_DUP2);
            emitOp(OpCode::OP_GET_INDEX);
            if (keepOld) {
                emitOp(OpCode::OP_TO_NUMBER);
                emitOp(OpCode::OP_DUP_X2);
            }
            emitOp(step);
            emitOp(OpCode::OP_SET_INDEX);
        } else {
            emitOp(OpCode::OP_DUP);
            emitGetProp(member->property->name);
            if (keepOld) {
                emitOp(OpCode::OP_TO_NUMBER);
                emitOp(OpCode::OP_DUP_X1);
            }
            emitOp(step);
            emitSetProp(member->property->name);
        }
        if (keepOld || discard) emitOp(OpCode::OP_POP);
        return;
    }
    error("Invalid target for ++ or --");
    if (!discard) emitOp(OpCode::OP_UNDEFINED);
}

void Compiler::compileAssignment(AssignmentExpression* expr, bool discard) {
    const bool compound = expr->op != OperatorType::Assign;
    Expression* target = expr->left.get();
    const auto applyOperator = [&]() {
        if (expr->op == OperatorType::LogicalAnd || expr->op == OperatorType::LogicalOr ||
            expr->op == OperatorType::NullCoalescing) {
            error("Unsupported compound assignment");
        }
        if (!emitBinaryOperator(expr->op)) {
            error("Unsupported compound assignment");
        }
    };

    if (target->kind == Expression::ExpressionKind::Identifier && !fs->withLocals.empty()) {
        const std::string& name = static_cast<Identifier*>(target)->name;
        if (compound) {
            compileIdentifier(static_cast<Identifier*>(target));
            compileExpression(expr->right.get());
            applyOperator();
        } else {
            compileExpression(expr->right.get());
        }
        const size_t found = emitWith(OpCode::OP_WITH_SET, name);
        std::vector<uint16_t> saved;
        saved.swap(fs->withLocals);
        emitStore(resolve(name), name, true);
        fs->withLocals.swap(saved);
        patchJump(found);
        if (discard) emitOp(OpCode::OP_POP);
        return;
    }
    if (target->kind == Expression::ExpressionKind::Identifier) {
        const std::string& name = static_cast<Identifier*>(target)->name;
        const Resolved where = resolve(name);
        if (where.kind == NameKind::Member) {
            if (compound) {
                emitThisProp(OpCode::OP_GET_THIS_PROP, name);
                compileExpression(expr->right.get());
                applyOperator();
            } else {
                compileExpression(expr->right.get());
            }
            emitThisProp(OpCode::OP_SET_THIS_PROP, name);
            if (discard) emitOp(OpCode::OP_POP);
            return;
        }
        if (where.kind == NameKind::Static) {
            emitOp(OpCode::OP_OWNER_CLASS);
            if (compound) {
                emitOp(OpCode::OP_DUP);
                emitGetProp(name);
                compileExpression(expr->right.get());
                applyOperator();
            } else {
                compileExpression(expr->right.get());
            }
            emitSetProp(name);
            if (discard) emitOp(OpCode::OP_POP);
            return;
        }
        if (compound) {
            emitLoad(where, name);
            compileExpression(expr->right.get());
            applyOperator();
        } else {
            compileExpression(expr->right.get());
        }
        emitStore(where, name, !discard);
        return;
    }
    if (target->kind == Expression::ExpressionKind::Member) {
        auto* member = static_cast<MemberExpression*>(target);
        if (member->object && member->object->kind == Expression::ExpressionKind::Super && !member->computed) {
            if (compound) {
                emitOp(OpCode::OP_GET_SUPER);
                emitU16(nameIndex(member->property->name));
                compileExpression(expr->right.get());
                applyOperator();
            } else {
                compileExpression(expr->right.get());
            }
            emitOp(OpCode::OP_SET_SUPER);
            emitU16(nameIndex(member->property->name));
            if (discard) emitOp(OpCode::OP_POP);
            return;
        }
        uint16_t slot = 0;
        const bool onLocal = !member->computed && localObject(member->object.get(), slot);
        if (onLocal || (!member->computed && thisObject(member->object.get()))) {
            const std::string& name = member->property->name;
            if (compound) {
                if (onLocal) {
                    emitLocalProp(OpCode::OP_GET_LOCAL_PROP, slot, name);
                } else {
                    emitThisProp(OpCode::OP_GET_THIS_PROP, name);
                }
                compileExpression(expr->right.get());
                applyOperator();
            } else {
                compileExpression(expr->right.get());
            }
            if (onLocal) {
                emitLocalProp(OpCode::OP_SET_LOCAL_PROP, slot, name);
            } else {
                emitThisProp(OpCode::OP_SET_THIS_PROP, name);
            }
            if (discard) emitOp(OpCode::OP_POP);
            return;
        }
        std::string qualified;
        std::vector<std::string> rest;
        if (!member->computed && qualifiedName(member, qualified, rest) && !rest.empty()) {
            // com.x.Y.field = value
            emitOp(OpCode::OP_GET_QNAME);
            emitU16(nameIndex(qualified));
            for (size_t i = 0; i + 1 < rest.size(); ++i) {
                emitGetProp(rest[i]);
            }
        } else {
            compileExpression(member->object.get());
        }
        if (member->computed) {
            compileExpression(member->computedProperty.get());
            if (compound) {
                emitOp(OpCode::OP_DUP2);
                emitOp(OpCode::OP_GET_INDEX);
                compileExpression(expr->right.get());
                applyOperator();
            } else {
                compileExpression(expr->right.get());
            }
            emitOp(OpCode::OP_SET_INDEX);
        } else {
            const std::string& name = member->property->name;
            if (compound) {
                emitOp(OpCode::OP_DUP);
                emitGetProp(name);
                compileExpression(expr->right.get());
                applyOperator();
            } else {
                compileExpression(expr->right.get());
            }
            emitSetProp(name);
        }
        if (discard) emitOp(OpCode::OP_POP);
        return;
    }
    error("Invalid assignment target");
    compileExpression(expr->right.get());
    if (discard) emitOp(OpCode::OP_POP);
}

int Compiler::compileArguments(const ExpressionList& args) {
    for (auto& arg : args) {
        compileExpression(arg.get());
    }
    if (args.size() > 255) {
        error("Too many arguments");
    }
    return static_cast<int>(args.size());
}

void Compiler::compileCallExpression(CallExpression* expr) {
    Expression* callee = expr->callee.get();
    if (callee->kind == Expression::ExpressionKind::Super) {
        const int argc = compileArguments(expr->arguments);
        emitOp(OpCode::OP_SUPER_CTOR);
        emitU8(static_cast<uint8_t>(argc));
        adjust(1 - argc);
        return;
    }
    if (callee->kind == Expression::ExpressionKind::Member) {
        auto* member = static_cast<MemberExpression*>(callee);
        if (member->object && member->object->kind == Expression::ExpressionKind::Super && !member->computed) {
            const int argc = compileArguments(expr->arguments);
            emitOp(OpCode::OP_CALL_SUPER);
            emitU16(nameIndex(member->property->name));
            emitU8(static_cast<uint8_t>(argc));
            adjust(1 - argc);
            return;
        }
        std::string qualified;
        std::vector<std::string> rest;
        if (!member->computed && qualifiedName(member, qualified, rest)) {
            emitOp(OpCode::OP_GET_QNAME);
            emitU16(nameIndex(qualified));
            if (rest.empty()) {
                const int argc = compileArguments(expr->arguments);
                emitOp(OpCode::OP_CALL);
                emitU8(static_cast<uint8_t>(argc));
                adjust(-argc);
                return;
            }
            for (size_t i = 0; i + 1 < rest.size(); ++i) {
                emitGetProp(rest[i]);
            }
            const int argc = compileArguments(expr->arguments);
            emitCallProp(rest.back(), argc);
            return;
        }
        uint16_t slot = 0;
        const bool onLocal = !member->computed && localObject(member->object.get(), slot);
        if (onLocal || (!member->computed && thisObject(member->object.get()))) {
            emitOp(OpCode::OP_UNDEFINED);
            const int argc = compileArguments(expr->arguments);
            emitFusedCall(onLocal ? OpCode::OP_CALL_LOCAL_PROP : OpCode::OP_CALL_THIS_PROP, slot,
                          member->property->name, argc);
            return;
        }
        compileExpression(member->object.get());
        if (member->computed) {
            compileExpression(member->computedProperty.get());
            const int argc = compileArguments(expr->arguments);
            emitOp(OpCode::OP_CALL_INDEX);
            emitU8(static_cast<uint8_t>(argc));
            adjust(-argc - 1);
            return;
        }
        const int argc = compileArguments(expr->arguments);
        emitCallProp(member->property->name, argc);
        return;
    }
    if (callee->kind == Expression::ExpressionKind::Identifier && !fs->withLocals.empty() && !calleeOutsideWith) {
        // A with object's method, called on it; else the call as it would be.
        const std::string& name = static_cast<Identifier*>(callee)->name;
        const size_t notFound = emitWith(OpCode::OP_WITH_FIND, name);
        adjust(1);
        const int argc = compileArguments(expr->arguments);
        emitCallProp(name, argc);
        const size_t end = emitJump(OpCode::OP_JUMP);
        patchJump(notFound);
        adjust(-1);
        // The callee as it would be; its arguments still in the with.
        calleeOutsideWith = true;
        compileCallExpression(expr);
        adjust(-1);
        patchJump(end);
        adjust(1);
        return;
    }
    if (callee->kind == Expression::ExpressionKind::Identifier) {
        calleeOutsideWith = false;
        const std::string& name = static_cast<Identifier*>(callee)->name;
        const Resolved where = resolve(name);
        switch (where.kind) {
            case NameKind::Member: {
                emitOp(OpCode::OP_UNDEFINED);
                const int argc = compileArguments(expr->arguments);
                emitFusedCall(OpCode::OP_CALL_THIS_PROP, 0, name, argc);
                return;
            }
            case NameKind::Static: {
                emitOp(OpCode::OP_OWNER_CLASS);
                const int argc = compileArguments(expr->arguments);
                emitCallProp(name, argc);
                return;
            }
            case NameKind::Global: {
                // int(x), uint(x) and Number(x) as the conversions they are.
                if (expr->arguments.size() == 1 && (name == "int" || name == "uint" || name == "Number")) {
                    compileExpression(expr->arguments[0].get());
                    emitCoerce(name == "int" ? Coerce::Int : name == "uint" ? Coerce::Uint : Coerce::Number);
                    return;
                }
                if (expr->arguments.size() == 1 && name == "Boolean") {
                    compileExpression(expr->arguments[0].get());
                    emitOp(OpCode::OP_NOT);
                    emitOp(OpCode::OP_NOT);
                    return;
                }
                // ActionScript 1's getProperty(target, _x) and
                // setProperty(target, _x, value) name the property: a bare
                // name there is its name.
                const bool namesProperty = name == "getProperty" || name == "setProperty";
                int argc = 0;
                for (size_t i = 0; i < expr->arguments.size(); ++i) {
                    Expression* arg = expr->arguments[i].get();
                    if (namesProperty && i == 1 && arg->kind == Expression::ExpressionKind::Identifier) {
                        emitString(static_cast<Identifier*>(arg)->name);
                    } else {
                        compileExpression(arg);
                    }
                    ++argc;
                }
                emitOp(OpCode::OP_CALL_NAME);
                emitU16(nameIndex(name));
                emitU8(static_cast<uint8_t>(argc));
                emitU16(newCache());
                adjust(1 - argc);
                return;
            }
            default:
                break;
        }
        emitLoad(where, name);
        const int argc = compileArguments(expr->arguments);
        emitOp(OpCode::OP_CALL);
        emitU8(static_cast<uint8_t>(argc));
        adjust(-argc);
        return;
    }
    compileExpression(callee);
    const int argc = compileArguments(expr->arguments);
    emitOp(OpCode::OP_CALL);
    emitU8(static_cast<uint8_t>(argc));
    adjust(-argc);
}

void Compiler::compileNewExpression(NewExpression* expr) {
    std::string qualified;
    std::vector<std::string> rest;
    Expression* callee = expr->callee.get();
    if (callee->kind == Expression::ExpressionKind::Member && qualifiedName(callee, qualified, rest)) {
        emitOp(OpCode::OP_GET_QNAME);
        emitU16(nameIndex(qualified));
        for (const auto& part : rest) {
            emitGetProp(part);
        }
    } else {
        compileExpression(callee);
    }
    const int argc = compileArguments(expr->arguments);
    emitOp(OpCode::OP_NEW);
    emitU8(static_cast<uint8_t>(argc));
    adjust(-argc);
}

void Compiler::compileMemberExpression(MemberExpression* expr) {
    if (expr->object && expr->object->kind == Expression::ExpressionKind::Super && !expr->computed) {
        emitOp(OpCode::OP_GET_SUPER);
        emitU16(nameIndex(expr->property->name));
        return;
    }
    std::string qualified;
    std::vector<std::string> rest;
    if (!expr->computed && qualifiedName(expr, qualified, rest)) {
        emitOp(OpCode::OP_GET_QNAME);
        emitU16(nameIndex(qualified));
        for (const auto& part : rest) {
            emitGetProp(part);
        }
        return;
    }
    uint16_t slot = 0;
    if (!expr->computed && localObject(expr->object.get(), slot)) {
        emitLocalProp(OpCode::OP_GET_LOCAL_PROP, slot, expr->property->name);
        return;
    }
    if (!expr->computed && thisObject(expr->object.get())) {
        emitThisProp(OpCode::OP_GET_THIS_PROP, expr->property->name);
        return;
    }
    compileExpression(expr->object.get());
    if (expr->computed) {
        compileExpression(expr->computedProperty.get());
        emitOp(OpCode::OP_GET_INDEX);
    } else {
        emitGetProp(expr->property->name);
    }
}

void Compiler::compileArrayExpression(ArrayExpression* expr) {
    if (!expr->vectorType.empty()) {
        compileTypeExpression(expr->vectorType);
    }
    for (auto& element : expr->elements) {
        if (element) {
            compileExpression(element.get());
        } else {
            emitOp(OpCode::OP_UNDEFINED);
        }
    }
    const int count = static_cast<int>(expr->elements.size());
    if (count > 65535) {
        error("Too many elements in an array literal");
    }
    if (!expr->vectorType.empty()) {
        emitOp(OpCode::OP_NEW_VECTOR);
        emitU16(static_cast<uint16_t>(count));
        adjust(-count);
    } else {
        emitOp(OpCode::OP_NEW_ARRAY);
        emitU16(static_cast<uint16_t>(count));
        adjust(1 - count);
    }
}

void Compiler::compileObjectExpression(ObjectExpression* expr) {
    for (auto& property : expr->properties) {
        Expression* key = property.first.get();
        if (key->kind == Expression::ExpressionKind::Identifier) {
            emitString(static_cast<Identifier*>(key)->name);
        } else {
            Val literal;
            if (literalValue(key, literal)) {
                emitString(literal.toString());
            } else {
                compileExpression(key);
            }
        }
        compileExpression(property.second.get());
    }
    const int pairs = static_cast<int>(expr->properties.size());
    emitOp(OpCode::OP_NEW_OBJECT);
    emitU16(static_cast<uint16_t>(pairs));
    adjust(1 - 2 * pairs);
}

void Compiler::compileFunctionExpression(FunctionExpression* expr) {
    const std::string name = expr->name ? expr->name->name : std::string();
    auto code = compileFunction(name, expr->parameters, expr->returnType.get(), expr->body.get(), nullptr, false,
                                false, false, expr->line, nullptr, name);
    emitClosure(code);
}

} // namespace as3
