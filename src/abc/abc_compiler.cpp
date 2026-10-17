#include "abc.h"
#include "abc_builder.h"
#include "abc_player.h"
#include "abc_verifier.h"
#include "../parser/parser.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <set>
#include <sstream>

namespace as3 {
namespace abc {

std::string Diagnostic::text() const {
    std::string out = fileName.empty() ? std::string("<source>") : fileName;
    if (line > 0) out += ":" + std::to_string(line);
    return out + (isError ? ": error: " : ": warning: ") + message;
}

std::string Output::errorText() const {
    std::string all;
    for (const Diagnostic& diagnostic : diagnostics) all += diagnostic.text() + "\n";
    return all;
}

std::string timelineClassName(const std::string& documentName, const std::string& symbolName, int index) {
    // A document's other characters become _, a symbol's are left out.
    const auto clean = [](const std::string& text, bool keep) {
        std::string out;
        for (char c : text) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$') out += c;
            else if (keep) out += '_';
        }
        if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0]))) out = "_" + out;
        return out;
    };
    const std::string package = clean(documentName, true) + "_fla";
    if (symbolName.empty()) return package + ".MainTimeline";
    return package + "." + clean(symbolName, false) + "_" + std::to_string(index);
}

namespace {

using EK = Expression::ExpressionKind;

// Types

struct TypeRef {
    enum Kind { Any, Void, Int, Uint, Number, Boolean, String, Object, Other };
    Kind kind = Any;
    uint32_t mn = 0;   // 0 for *
};

enum class Access { Public, Private, Protected, Internal };

struct Member {
    std::string name;
    enum Kind { Var, Const, Method, Accessor };
    Kind kind = Var;
    bool isStatic = false;
    Access access = Access::Internal;
    uint32_t ns = 0;
    std::string type;
    Expression* init = nullptr;
};

struct Unit;

struct ClassModel {
    std::string package;
    std::string name;
    bool isInterface = false;
    bool isFinal = false;
    bool isDynamic = false;
    bool isPublic = true;
    bool outside = false;
    ClassDefinition* ast = nullptr;
    Unit* unit = nullptr;
    const Timeline* timeline = nullptr;
    int line = 0;
    // Qualified with a dot; empty for none (an interface).
    std::string superName;
    uint32_t superMn = 0;
    std::vector<uint32_t> interfaceMns;
    std::map<std::string, Member> instance;
    std::map<std::string, Member> statics;
    uint32_t nameMn = 0;
    uint32_t privateNs = 0;
    uint32_t protectedNs = 0;
    uint32_t staticProtectedNs = 0;
    uint32_t interfaceNs = 0;
    int classIndex = -1;

    std::string qualified() const { return package.empty() ? name : package + "." + name; }
};

// A definition at a package's or file's top: a class, function or
// variable, found by name.
struct Definition {
    enum Kind { Class, Function, Variable };
    Kind kind = Class;
    ClassModel* cls = nullptr;
    uint32_t mn = 0;
    std::string type;
    bool isConst = false;
    Expression* init = nullptr;
};

struct SourceFile;

// What code is compiled in: a package of a file (or the file outside its
// packages, or a timeline), with what it imports.
struct Unit {
    SourceFile* file = nullptr;
    std::string fileName;
    std::string package;
    bool outside = false;
    std::map<std::string, std::string> imports;
    std::vector<std::string> wildcards;
    std::vector<AstNode*> members;
    std::map<const ClassModel*, uint32_t> openSets;
};

struct SourceFile {
    std::string fileName;
    std::unique_ptr<Program> program;
    std::vector<std::unique_ptr<Unit>> units;
    // Definitions outside the packages, which only the file sees.
    std::map<std::string, Definition> privateDefinitions;
    uint32_t privateNs = 0;
    std::vector<ClassModel*> classes;
    std::vector<FunctionDefinition*> functions;   // a package's, or outside one
    bool timeline = false;
    // A timeline's frames, one for each of its program's blocks.
    std::vector<int> frames;
};

// Code being compiled

struct Var {
    bool slot = false;
    uint32_t index = 0;   // the register, or the activation's slot
    TypeRef type;
};

struct Control {
    enum Kind { Loop, Switch, Block, Try, With };
    Kind kind = Block;
    std::vector<std::string> labels;
    CodeBuilder::Label breakLabel;
    CodeBuilder::Label continueLabel;
    // With: the register holding the object, to push again in handlers.
    uint32_t withRegister = 0;
    // Try: the code its catches cover and the code its finally covers, as
    // ranges, and whether each is open (or paused, to open again).
    Statement* finallyBody = nullptr;
    bool catchesOpen = false;
    bool finallyOpen = false;
    size_t catchStart = 0;
    size_t finallyStart = 0;
    bool pausedCatches = false;
    bool pausedFinally = false;
    std::vector<std::pair<size_t, size_t>> catchRanges;
    std::vector<std::pair<size_t, size_t>> finallyRanges;
};

struct Function {
    explicit Function(AbcFile& file) : code(file) {}

    Function* parent = nullptr;
    CodeBuilder code;
    Unit* unit = nullptr;
    ClassModel* cls = nullptr;
    bool isStatic = false;
    bool isMethod = false;
    bool isConstructor = false;
    bool isScript = false;
    bool pushesThis = false;
    TypeRef returnType;
    bool returnsValue = false;
    std::map<std::string, Var> vars;
    bool hasActivation = false;
    uint32_t activation = 0;
    uint32_t nextSlot = 1;
    uint32_t initScope = 0;
    uint32_t method = 0;
    std::vector<Control> controls;
    int lastLine = -1;
};

// What a name in code is.
struct NameRef {
    enum Kind { Register, Slot, ThisMember, Lexical, Global };
    Kind kind = Global;
    uint32_t index = 0;   // register or slot
    uint32_t mn = 0;      // the name
    TypeRef type;
    ClassModel* cls = nullptr;   // a class named, for its statics
};

// An assignment's target.
struct Target {
    enum Kind { Register, Slot, Property, Super };
    Kind kind = Register;
    uint32_t index = 0;
    uint32_t mn = 0;
    TypeRef type;
    // Property: how the object (and a runtime name) are pushed.
    std::function<void()> pushBase;
    int baseCount = 0;
};

// A compile-time constant: a parameter's default or a slot's value.
struct Constant {
    enum Kind { Undefined, Null, Boolean, Number, String };
    Kind kind = Undefined;
    bool boolean = false;
    double number = 0;
    std::string text;
};

// Scanning functions for what they declare and what their nested
// functions use, to find the variables closures capture.

struct Scan {
    std::vector<std::pair<std::string, std::string>> vars;
    std::vector<FunctionDefinition*> functions;
    std::set<std::string> refs;
    std::set<std::string> innerFree;
    // What var declares (not catch or function).
    std::set<std::string> declared;
    bool usesArguments = false;
    bool callsSuper = false;
};

void scanStatement(Statement* stmt, Scan& scan);
void scanExpression(Expression* expr, Scan& scan);
std::set<std::string> freeNames(const std::vector<std::unique_ptr<Parameter>>& params, Statement* body,
                                const std::string& selfName);

// The predicate of an E4X filter, as the parser makes it:
// $xmlFilter(list, function($item) { with ($item) return predicate; }).
Expression* filterPredicate(CallExpression* call) {
    if (!call->callee || call->callee->kind != EK::Identifier ||
        static_cast<Identifier*>(call->callee.get())->name != "$xmlFilter" || call->arguments.size() != 2 ||
        call->arguments[1]->kind != EK::Function) {
        return nullptr;
    }
    auto* function = static_cast<FunctionExpression*>(call->arguments[1].get());
    auto* block = dynamic_cast<BlockStatement*>(function->body.get());
    if (!block || block->statements.size() != 1) return nullptr;
    auto* with = dynamic_cast<WithStatement*>(block->statements[0].get());
    if (!with) return nullptr;
    auto* result = dynamic_cast<ReturnStatement*>(with->body.get());
    return result ? result->argument.get() : nullptr;
}

void scanExpression(Expression* expr, Scan& scan) {
    if (!expr) return;
    switch (expr->kind) {
        case EK::Identifier: {
            auto* id = static_cast<Identifier*>(expr);
            scan.refs.insert(id->name);
            if (id->name == "arguments") scan.usesArguments = true;
            break;
        }
        case EK::Binary: {
            Expression* at = expr;
            while (at && at->kind == EK::Binary) {
                auto* binary = static_cast<BinaryExpression*>(at);
                scanExpression(binary->right.get(), scan);
                at = binary->left.get();
            }
            scanExpression(at, scan);
            break;
        }
        case EK::Unary:
            scanExpression(static_cast<UnaryExpression*>(expr)->operand.get(), scan);
            break;
        case EK::Conditional: {
            auto* conditional = static_cast<ConditionalExpression*>(expr);
            scanExpression(conditional->test.get(), scan);
            scanExpression(conditional->consequent.get(), scan);
            scanExpression(conditional->alternate.get(), scan);
            break;
        }
        case EK::Assignment: {
            auto* assignment = static_cast<AssignmentExpression*>(expr);
            scanExpression(assignment->left.get(), scan);
            scanExpression(assignment->right.get(), scan);
            break;
        }
        case EK::Call: {
            auto* call = static_cast<CallExpression*>(expr);
            if (Expression* predicate = filterPredicate(call)) {
                // Compiled in place, not as a closure.
                scanExpression(call->arguments[0].get(), scan);
                scanExpression(predicate, scan);
                break;
            }
            if (call->callee && call->callee->kind == EK::Super) scan.callsSuper = true;
            scanExpression(call->callee.get(), scan);
            for (auto& arg : call->arguments) scanExpression(arg.get(), scan);
            break;
        }
        case EK::New: {
            auto* made = static_cast<NewExpression*>(expr);
            scanExpression(made->callee.get(), scan);
            for (auto& arg : made->arguments) scanExpression(arg.get(), scan);
            break;
        }
        case EK::Member: {
            auto* member = static_cast<MemberExpression*>(expr);
            scanExpression(member->object.get(), scan);
            if (member->computed) scanExpression(member->computedProperty.get(), scan);
            break;
        }
        case EK::Function: {
            auto* function = static_cast<FunctionExpression*>(expr);
            const auto free = freeNames(function->parameters, function->body.get(),
                                        function->name ? function->name->name : std::string());
            scan.innerFree.insert(free.begin(), free.end());
            break;
        }
        case EK::Array:
            for (auto& element : static_cast<ArrayExpression*>(expr)->elements) scanExpression(element.get(), scan);
            break;
        case EK::Object:
            for (auto& property : static_cast<ObjectExpression*>(expr)->properties) {
                if (property.first && property.first->kind != EK::Identifier) scanExpression(property.first.get(), scan);
                scanExpression(property.second.get(), scan);
            }
            break;
        case EK::Sequence:
            for (auto& part : static_cast<SequenceExpression*>(expr)->expressions) scanExpression(part.get(), scan);
            break;
        case EK::TypeApplication:
            scanExpression(static_cast<TypeApplication*>(expr)->base.get(), scan);
            break;
        default:
            break;
    }
}

void scanStatement(Statement* stmt, Scan& scan) {
    if (!stmt) return;
    if (auto* block = dynamic_cast<BlockStatement*>(stmt)) {
        for (auto& s : block->statements) scanStatement(s.get(), scan);
    } else if (auto* expression = dynamic_cast<ExpressionStatement*>(stmt)) {
        scanExpression(expression->expression.get(), scan);
    } else if (auto* variables = dynamic_cast<VariableDefinition*>(stmt)) {
        for (auto& decl : variables->declarations) {
            scan.vars.emplace_back(decl->id->name, decl->type ? decl->type->name : std::string());
            scan.declared.insert(decl->id->name);
            scanExpression(decl->init.get(), scan);
        }
    } else if (auto* function = dynamic_cast<FunctionDefinition*>(stmt)) {
        scan.functions.push_back(function);
        scan.vars.emplace_back(function->name->name, "Function");
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
            for (auto& s : clause->consequent) scanStatement(s.get(), scan);
        }
    } else if (auto* labeled = dynamic_cast<LabeledStatement*>(stmt)) {
        scanStatement(labeled->body.get(), scan);
    } else if (auto* attempt = dynamic_cast<TryStatement*>(stmt)) {
        scanStatement(attempt->block.get(), scan);
        for (auto& clause : attempt->catches) {
            // Untyped: two catches may give one name different types.
            scan.vars.emplace_back(clause.variable->name, "*");
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
    for (const auto& param : params) declared.insert(param->name->name);
    for (const auto& var : scan.vars) declared.insert(var.first);
    declared.insert("arguments");
    if (!selfName.empty()) declared.insert(selfName);
    std::set<std::string> free;
    for (const auto& name : scan.refs) {
        if (!declared.count(name)) free.insert(name);
    }
    for (const auto& name : scan.innerFree) {
        if (!declared.count(name)) free.insert(name);
    }
    return free;
}

// Whether a constructor's body calls super(), outside nested functions.
bool callsSuper(Statement* body) {
    Scan scan;
    scanStatement(body, scan);
    return scan.callsSuper;
}

Access accessOf(const std::vector<AttributePtr>& attributes, Access otherwise) {
    if (hasAttribute(attributes, AttributeType::Public)) return Access::Public;
    if (hasAttribute(attributes, AttributeType::Private)) return Access::Private;
    if (hasAttribute(attributes, AttributeType::Protected)) return Access::Protected;
    if (hasAttribute(attributes, AttributeType::Internal)) return Access::Internal;
    return otherwise;
}

std::string packageOf(const std::string& qualified) {
    const size_t dot = qualified.find_last_of('.');
    return dot == std::string::npos ? std::string() : qualified.substr(0, dot);
}

std::string shortOf(const std::string& qualified) {
    const size_t dot = qualified.find_last_of('.');
    return dot == std::string::npos ? qualified : qualified.substr(dot + 1);
}

// A number as ActionScript writes it, for an object literal's numeric key.
std::string numberText(double value) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value < 0 ? "-Infinity" : "Infinity";
    if (value == std::floor(value) && std::fabs(value) < 1e21) {
        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "%.0f", value);
        return buffer;
    }
    for (int precision = 1; precision <= 17; ++precision) {
        char buffer[40];
        std::snprintf(buffer, sizeof buffer, "%.*g", precision, value);
        if (std::strtod(buffer, nullptr) == value) return buffer;
    }
    return std::to_string(value);
}

class AbcCompiler {
public:
    explicit AbcCompiler(const Options& options) : options(options), pool(file) {}

    Output run(const std::vector<Source>& sources, const std::vector<Timeline>& timelines);

private:
    const Options& options;
    AbcFile file;
    PoolBuilder pool;
    Output output;
    std::vector<std::unique_ptr<SourceFile>> files;
    std::vector<std::unique_ptr<ClassModel>> classes;
    std::map<std::string, Definition> globals;
    Function* cur = nullptr;
    // The class whose constants are found by name where no code is being
    // compiled (a field's value, a parameter's default).
    ClassModel* constantClass = nullptr;
    std::string currentFile;
    int currentLine = 0;

    // Diagnostics

    void error(const std::string& message, int line = -1) {
        output.diagnostics.push_back({true, currentFile, line < 0 ? currentLine : line, message});
    }
    void warning(const std::string& message, int line = -1) {
        output.diagnostics.push_back({false, currentFile, line < 0 ? currentLine : line, message});
    }
    bool hasErrors() const {
        for (const Diagnostic& diagnostic : output.diagnostics) {
            if (diagnostic.isError) return true;
        }
        return false;
    }

    // Namespaces and names

    uint32_t publicNs() { return pool.ns(NS_Package, ""); }
    uint32_t packageNs(const std::string& package) { return pool.ns(NS_Package, package); }
    uint32_t internalNs(const std::string& package) { return pool.ns(NS_PackageInternal, package); }
    uint32_t publicName(const std::string& name) { return pool.qname(publicNs(), name); }

    ClassModel* sourceClass(const std::string& qualified) {
        auto found = globals.find(qualified);
        return found != globals.end() && found->second.kind == Definition::Class ? found->second.cls : nullptr;
    }

    ClassModel* superOf(const ClassModel* cls) {
        return cls && !cls->superName.empty() ? sourceClass(cls->superName) : nullptr;
    }

    uint32_t openSet(Unit* unit, const ClassModel* cls);
    const Definition* findDefinition(Unit* unit, const std::string& name, std::string* package = nullptr);
    bool isPackage(Unit* unit, const std::string& name);
    TypeRef resolveType(const std::string& written, Unit* unit, bool quiet = false);
    std::vector<std::string> superChain(const ClassModel* cls);

    // Models
    void parseSources(const std::vector<Source>& sources);
    void collectDefinitions(SourceFile& source);
    void addClass(ClassDefinition* definition, Unit* unit, bool outside);
    void buildTimeline(const Timeline& timeline);
    void resolveClasses();
    void addMember(ClassModel& cls, const std::string& name, Member member);

    // Output
    void compileFile(SourceFile& source);
    void compileClass(ClassModel& cls, Function& script);
    void compileTimeline(ClassModel& cls);
    uint32_t methodInfo(const std::vector<std::unique_ptr<Parameter>>& params, Identifier* returnType,
                        const std::string& name, Unit* unit, bool isSetter);
    uint32_t metadataOf(const std::vector<Metadata>& tags, std::vector<uint32_t>& out);
    void beginFunction(Function& f, const std::vector<std::unique_ptr<Parameter>>* params, Statement* body,
                       const std::string& selfName = std::string(), const std::set<std::string>* members = nullptr);
    void endFunction(Function& f);
    uint32_t compileMethod(FunctionDefinition* definition, ClassModel* cls, bool isStatic, const std::string& name,
                           uint32_t initScope);
    uint32_t compileClosure(const std::vector<std::unique_ptr<Parameter>>& params, Identifier* returnType,
                            Statement* body, const std::string& name, const std::string& selfName);
    void emitClassDefinition(ClassModel& cls, Function& script);
    bool constantValue(Expression* expr, Constant& out);
    void constantDetail(const Constant& value, const TypeRef& type, uint8_t& kind, uint32_t& index);

    // Statements
    void statement(Statement* stmt);
    void statements(const std::vector<StatementPtr>& list) {
        for (const auto& s : list) statement(s.get());
    }
    void variableDefinition(VariableDefinition* stmt);
    void ifStatement(IfStatement* stmt);
    void whileStatement(WhileStatement* stmt, const std::vector<std::string>& labels);
    void doWhileStatement(DoWhileStatement* stmt, const std::vector<std::string>& labels);
    void forStatement(ForStatement* stmt, const std::vector<std::string>& labels);
    void forInStatement(ForInStatement* stmt, const std::vector<std::string>& labels);
    void switchStatement(SwitchStatement* stmt, const std::vector<std::string>& labels);
    void labeledStatement(LabeledStatement* stmt);
    void breakContinue(const std::string& label, bool isContinue);
    void returnStatement(ReturnStatement* stmt);
    void tryStatement(TryStatement* stmt);
    void withStatement(WithStatement* stmt);
    void exitTo(size_t depth);
    void inlineFinally(size_t index);
    void pauseFrom(size_t index);
    void resumeAll();
    void restoreScopes(size_t controlDepth);

    // Expressions
    void expr(Expression* e);
    void discard(Expression* e);
    void condJump(Expression* e, bool jumpIf, CodeBuilder::Label target);
    void identifier(Identifier* id);
    void literal(Literal* lit);
    void binary(BinaryExpression* e);
    void unary(UnaryExpression* e, bool keep);
    void incDec(Expression* target, bool increment, bool postfix, bool keep);
    void assignment(AssignmentExpression* e, bool keep);
    void call(CallExpression* e, bool keep);
    void construct(NewExpression* e);
    void member(MemberExpression* e);
    void arrayLiteral(ArrayExpression* e);
    void objectLiteral(ObjectExpression* e);
    void xmlFilter(CallExpression* e, Expression* predicate);
    int arguments(const ExpressionList& args);
    bool binaryOp(OperatorType op);
    void typeValue(const std::string& written);

    NameRef resolve(const std::string& name);
    void load(const NameRef& ref);
    uint32_t propertyName(MemberExpression* e, bool& runtime);
    bool qualifiedPath(Expression* e, std::string& package, std::string& name);
    ClassModel* classNamed(Expression* e);
    bool targetOf(Expression* e, Target& out, bool strict);
    void loadTarget(const Target& target);
    void storeTarget(const Target& target);
    void storeFromStack(Expression* left);
    void coerce(const TypeRef& type);
    void pushDefault(const TypeRef& type);
    Function& root() {
        Function* f = cur;
        while (f->parent) f = f->parent;
        return *f;
    }
    bool inWith() const {
        for (Function* f = cur; f; f = f->parent) {
            for (const Control& control : f->controls) {
                if (control.kind == Control::With) return true;
            }
        }
        return false;
    }
    CodeBuilder& code() { return cur->code; }
    void op(uint8_t o) { cur->code.op(o); }
    void op(uint8_t o, uint32_t a) { cur->code.op(o, a); }
    void op(uint8_t o, uint32_t a, uint32_t b) { cur->code.op(o, a, b); }
    void pushString(const std::string& text) { op(OP_pushstring, pool.string(text)); }
    uint32_t activationName(const std::string& name) {
        return pool.qname(internalNs(cur->unit ? cur->unit->package : std::string()), name);
    }
};

// Names

uint32_t AbcCompiler::openSet(Unit* unit, const ClassModel* cls) {
    auto found = unit->openSets.find(cls);
    if (found != unit->openSets.end()) return found->second;
    std::vector<uint32_t> list = {publicNs(), packageNs(unit->package), internalNs(unit->package)};
    if (unit->file && unit->file->privateNs) list.push_back(unit->file->privateNs);
    for (const ClassModel* c = cls; c; c = superOf(c)) {
        if (c == cls && c->privateNs) list.push_back(c->privateNs);
        if (c->protectedNs) list.push_back(c->protectedNs);
        if (c->staticProtectedNs) list.push_back(c->staticProtectedNs);
        if (c != cls && c->package == unit->package) list.push_back(internalNs(c->package));
    }
    for (const std::string& wildcard : unit->wildcards) list.push_back(packageNs(wildcard));
    for (const auto& import : unit->imports) list.push_back(packageNs(packageOf(import.second)));
    const uint32_t set = pool.nsSet(list);
    unit->openSets[cls] = set;
    return set;
}

const Definition* AbcCompiler::findDefinition(Unit* unit, const std::string& name, std::string* package) {
    if (unit->file) {
        auto found = unit->file->privateDefinitions.find(name);
        if (found != unit->file->privateDefinitions.end()) return &found->second;
    }
    const auto global = [this](const std::string& key) -> const Definition* {
        auto found = globals.find(key);
        return found == globals.end() ? nullptr : &found->second;
    };
    auto imported = unit->imports.find(name);
    if (imported != unit->imports.end()) {
        if (package) *package = packageOf(imported->second);
        return global(imported->second);
    }
    if (const Definition* own = global(unit->package.empty() ? name : unit->package + "." + name)) return own;
    if (const Definition* top = global(name)) return top;
    for (const std::string& wildcard : unit->wildcards) {
        if (const Definition* found = global(wildcard + "." + name)) return found;
    }
    return nullptr;
}

bool AbcCompiler::isPackage(Unit* unit, const std::string& name) {
    if (name.empty()) return false;
    const auto under = [&name](const std::string& package) {
        return package == name || package.compare(0, name.size() + 1, name + ".") == 0;
    };
    if (under(unit->package)) return true;
    for (const auto& import : unit->imports) {
        if (under(packageOf(import.second))) return true;
    }
    for (const auto& wildcard : unit->wildcards) {
        if (under(wildcard)) return true;
    }
    for (const auto& definition : globals) {
        if (under(packageOf(definition.first))) return true;
    }
    return name == "flash" || name == "fl" || isPlayerPackage(name) ||
           name.compare(0, 6, "flash.") == 0 || name.compare(0, 3, "fl.") == 0;
}

TypeRef AbcCompiler::resolveType(const std::string& written, Unit* unit, bool quiet) {
    TypeRef type;
    if (written.empty() || written == "*") return type;
    if (written == "void") {
        type.kind = TypeRef::Void;
        type.mn = publicName("void");
        return type;
    }
    static const std::map<std::string, TypeRef::Kind> builtins = {
        {"int", TypeRef::Int}, {"uint", TypeRef::Uint}, {"Number", TypeRef::Number},
        {"Boolean", TypeRef::Boolean}, {"String", TypeRef::String}, {"Object", TypeRef::Object},
    };
    // Vector.<T>
    const size_t open = written.find(".<");
    if (open != std::string::npos && written.back() == '>') {
        const std::string base = written.substr(0, open);
        const std::string parameter = written.substr(open + 2, written.size() - open - 3);
        if (base == "Vector" || base == "__AS3__.vec.Vector") {
            const TypeRef element = resolveType(parameter, unit, quiet);
            type.kind = TypeRef::Other;
            type.mn = pool.typeName(pool.qname(packageNs("__AS3__.vec"), "Vector"), {element.mn});
            return type;
        }
        if (!quiet) warning("unknown generic type " + written + "; taken as *");
        return type;
    }
    if (written.find('.') != std::string::npos) {
        type.kind = TypeRef::Other;
        const Definition* definition = nullptr;
        auto found = globals.find(written);
        if (found != globals.end()) definition = &found->second;
        type.mn = definition ? definition->mn : pool.qname(packageNs(packageOf(written)), shortOf(written));
        return type;
    }
    std::string package;
    if (const Definition* definition = findDefinition(unit, written, &package)) {
        if (definition->kind == Definition::Class) {
            type.kind = TypeRef::Other;
            type.mn = definition->mn;
            return type;
        }
    }
    if (!package.empty() || unit->imports.count(written)) {
        // Imported by name: its package is known even without its
        // definition.
        type.kind = TypeRef::Other;
        type.mn = pool.qname(packageNs(packageOf(unit->imports[written])), written);
        return type;
    }
    auto builtin = builtins.find(written);
    if (builtin != builtins.end()) {
        type.kind = builtin->second;
        type.mn = publicName(written);
        return type;
    }
    if (findPlayerClass("", written)) {
        type.kind = TypeRef::Other;
        type.mn = publicName(written);
        return type;
    }
    for (const std::string& wildcard : unit->wildcards) {
        if (findPlayerClass(wildcard, written)) {
            type.kind = TypeRef::Other;
            type.mn = pool.qname(packageNs(wildcard), written);
            return type;
        }
    }
    if (!quiet) warning("type " + written + " is not known (import it by name); taken as *");
    return type;
}

std::vector<std::string> AbcCompiler::superChain(const ClassModel* cls) {
    // From Object down to the direct superclass, as far as known.
    std::vector<std::string> chain;
    std::string name = cls->superName;
    for (int guard = 0; !name.empty() && guard < 64; ++guard) {
        chain.push_back(name);
        if (ClassModel* model = sourceClass(name)) {
            name = model->superName;
        } else if (const PlayerClass* player = findPlayerClass(packageOf(name), shortOf(name))) {
            name = player->superclass ? player->superclass : "";
        } else {
            if (name != "Object") chain.push_back("Object");
            break;
        }
    }
    std::reverse(chain.begin(), chain.end());
    return chain;
}

// Models

void AbcCompiler::parseSources(const std::vector<Source>& sources) {
    for (const Source& source : sources) {
        auto entry = std::make_unique<SourceFile>();
        entry->fileName = source.fileName;
        currentFile = source.fileName;
        Parser parser(source.text, source.fileName);
        for (const auto& constant : options.constants) parser.defineConstant(constant.first, constant.second);
        if (options.includeHandler) parser.setIncludeHandler(options.includeHandler);
        entry->program = parser.parse();
        const auto report = [this](const std::string& text, bool isError) {
            std::istringstream lines(text);
            std::string line;
            while (std::getline(lines, line)) {
                if (line.empty()) continue;
                int number = 0;
                const size_t at = line.find("line ");
                if (at != std::string::npos) number = std::atoi(line.c_str() + at + 5);
                if (isError) error(line, number);
                else warning(line, number);
            }
        };
        report(parser.getErrors(), true);
        report(parser.getWarnings(), false);
        if (!parser.hasErrors() && entry->program) {
            files.push_back(std::move(entry));
        }
    }
}

void AbcCompiler::addMember(ClassModel& cls, const std::string& name, Member member) {
    auto& table = member.isStatic ? cls.statics : cls.instance;
    auto found = table.find(name);
    if (found != table.end()) {
        // A getter and its setter are one member.
        if (found->second.kind == Member::Accessor && member.kind == Member::Accessor) return;
        error("'" + name + "' is defined twice in " + cls.qualified());
        return;
    }
    member.name = name;
    if (cls.isInterface) {
        member.ns = cls.interfaceNs;
    } else {
        switch (member.access) {
            case Access::Public: member.ns = publicNs(); break;
            case Access::Private: member.ns = cls.privateNs; break;
            case Access::Protected: member.ns = member.isStatic ? cls.staticProtectedNs : cls.protectedNs; break;
            case Access::Internal: member.ns = internalNs(cls.package); break;
        }
    }
    table[name] = member;
}

void AbcCompiler::addClass(ClassDefinition* definition, Unit* unit, bool outside) {
    auto model = std::make_unique<ClassModel>();
    ClassModel& cls = *model;
    cls.package = outside ? std::string() : unit->package;
    cls.name = definition->name->name;
    cls.ast = definition;
    cls.unit = unit;
    cls.line = definition->line;
    cls.isInterface = definition->isInterface;
    cls.isFinal = hasAttribute(definition->attributes, AttributeType::Final);
    cls.isDynamic = hasAttribute(definition->attributes, AttributeType::Dynamic);
    cls.isPublic = !outside && hasAttribute(definition->attributes, AttributeType::Public);
    cls.outside = outside;
    const std::string where = cls.package.empty() ? cls.name : cls.package + ":" + cls.name;
    if (outside) {
        cls.nameMn = pool.qname(unit->file->privateNs, cls.name);
    } else {
        cls.nameMn = pool.qname(cls.isPublic ? packageNs(cls.package) : internalNs(cls.package), cls.name);
    }
    if (cls.isInterface) {
        cls.interfaceNs = pool.ns(NS_Namespace, where);
    } else {
        cls.privateNs = pool.privateNs(where);
        cls.protectedNs = pool.ns(NS_Protected, where);
        cls.staticProtectedNs = pool.ns(NS_StaticProtected, where);
    }
    for (auto& node : definition->members) {
        if (auto* function = dynamic_cast<FunctionDefinition*>(node.get())) {
            const bool isStatic = hasAttribute(function->attributes, AttributeType::Static);
            if (!isStatic && function->name->name == cls.name && !function->isGetter && !function->isSetter) continue;
            Member member;
            member.isStatic = isStatic;
            member.access = cls.isInterface ? Access::Public : accessOf(function->attributes, Access::Internal);
            member.kind = function->isGetter || function->isSetter ? Member::Accessor : Member::Method;
            if (function->isGetter && function->returnType) member.type = function->returnType->name;
            addMember(cls, function->name->name, member);
        } else if (auto* variables = dynamic_cast<VariableDefinition*>(node.get())) {
            for (auto& decl : variables->declarations) {
                Member member;
                member.isStatic = hasAttribute(variables->attributes, AttributeType::Static);
                member.access = accessOf(variables->attributes, Access::Internal);
                member.kind = variables->isConst ? Member::Const : Member::Var;
                member.type = decl->type ? decl->type->name : std::string();
                member.init = decl->init.get();
                addMember(cls, decl->id->name, member);
            }
        }
    }
    Definition entry;
    entry.kind = Definition::Class;
    entry.cls = &cls;
    entry.mn = cls.nameMn;
    if (outside) {
        unit->file->privateDefinitions[cls.name] = entry;
    } else {
        if (globals.count(cls.qualified())) error(cls.qualified() + " is defined twice", cls.line);
        globals[cls.qualified()] = entry;
    }
    unit->file->classes.push_back(&cls);
    classes.push_back(std::move(model));
}

void AbcCompiler::collectDefinitions(SourceFile& source) {
    currentFile = source.fileName;
    source.privateNs = pool.privateNs(source.fileName + "$");
    const auto addImport = [](Unit& unit, ImportDirective* import) {
        if (import->isPackage) {
            unit.wildcards.push_back(import->path);
        } else {
            unit.imports[import->name->name] =
                import->path.empty() ? import->name->name : import->path + "." + import->name->name;
        }
    };
    auto outside = std::make_unique<Unit>();
    outside->file = &source;
    outside->fileName = source.fileName;
    outside->outside = true;
    for (auto& import : source.program->imports) addImport(*outside, import.get());
    for (auto& node : source.program->statements) {
        if (auto* import = dynamic_cast<ImportDirective*>(node.get())) addImport(*outside, import);
    }
    for (auto& node : source.program->statements) {
        if (auto* package = dynamic_cast<PackageDefinition*>(node.get())) {
            auto unit = std::make_unique<Unit>(*outside);
            unit->outside = false;
            unit->package = package->name ? package->name->name : std::string();
            for (auto& import : package->imports) addImport(*unit, import.get());
            for (auto& member : package->members) {
                if (auto* import = dynamic_cast<ImportDirective*>(member.get())) addImport(*unit, import);
                else unit->members.push_back(member.get());
            }
            source.units.push_back(std::move(unit));
        } else if (!dynamic_cast<ImportDirective*>(node.get())) {
            outside->members.push_back(node.get());
        }
    }
    source.units.push_back(std::move(outside));

    for (auto& unit : source.units) {
        for (AstNode* node : unit->members) {
            if (auto* definition = dynamic_cast<ClassDefinition*>(node)) {
                addClass(definition, unit.get(), unit->outside);
            } else if (auto* function = dynamic_cast<FunctionDefinition*>(node)) {
                Definition entry;
                entry.kind = Definition::Function;
                const bool isPublic = hasAttribute(function->attributes, AttributeType::Public);
                if (unit->outside) {
                    entry.mn = pool.qname(source.privateNs, function->name->name);
                    source.privateDefinitions[function->name->name] = entry;
                } else {
                    entry.mn = pool.qname(isPublic ? packageNs(unit->package) : internalNs(unit->package), function->name->name);
                    globals[unit->package.empty() ? function->name->name : unit->package + "." + function->name->name] = entry;
                }
                source.functions.push_back(function);
            } else if (auto* variables = dynamic_cast<VariableDefinition*>(node)) {
                const bool isPublic = hasAttribute(variables->attributes, AttributeType::Public);
                for (auto& decl : variables->declarations) {
                    Definition entry;
                    entry.kind = Definition::Variable;
                    entry.type = decl->type ? decl->type->name : std::string();
                    entry.isConst = variables->isConst;
                    entry.init = decl->init.get();
                    const std::string& name = decl->id->name;
                    if (unit->outside) {
                        entry.mn = pool.qname(source.privateNs, name);
                        source.privateDefinitions[name] = entry;
                    } else {
                        entry.mn = pool.qname(isPublic ? packageNs(unit->package) : internalNs(unit->package), name);
                        globals[unit->package.empty() ? name : unit->package + "." + name] = entry;
                    }
                }
            }
        }
    }
}

void AbcCompiler::buildTimeline(const Timeline& timeline) {
    auto source = std::make_unique<SourceFile>();
    source->fileName = timeline.className;
    source->timeline = true;
    source->program = std::make_unique<Program>();
    source->privateNs = pool.privateNs(timeline.className + "$");
    currentFile = timeline.className;
    auto unit = std::make_unique<Unit>();
    unit->file = source.get();
    unit->fileName = timeline.className;
    unit->package = packageOf(timeline.className);
    // Frame scripts see the player's packages without importing them.
    for (const char* package : {"flash.accessibility", "flash.display", "flash.errors", "flash.events", "flash.external",
                                "flash.filters", "flash.geom", "flash.media", "flash.net", "flash.printing",
                                "flash.system", "flash.text", "flash.ui", "flash.utils", "flash.xml"}) {
        unit->wildcards.push_back(package);
    }

    auto model = std::make_unique<ClassModel>();
    ClassModel& cls = *model;
    cls.package = unit->package;
    cls.name = shortOf(timeline.className);
    cls.timeline = &timeline;
    cls.isDynamic = true;
    cls.unit = unit.get();
    const std::string where = cls.package.empty() ? cls.name : cls.package + ":" + cls.name;
    cls.nameMn = pool.qname(packageNs(cls.package), cls.name);
    cls.privateNs = pool.privateNs(where);
    cls.protectedNs = pool.ns(NS_Protected, where);
    cls.staticProtectedNs = pool.ns(NS_StaticProtected, where);
    cls.superName = timeline.baseClass.empty() ? "flash.display.MovieClip" : timeline.baseClass;

    // Each frame's script, parsed; its imports are the class's.
    std::map<int, std::string> frames;
    for (const FrameScript& frame : timeline.frames) {
        if (frame.frame < 0) {
            error("frame " + std::to_string(frame.frame) + " is not a frame");
            continue;
        }
        frames[frame.frame] += frame.code + "\n";
    }
    for (const auto& frame : frames) {
        const std::string name = timeline.className + ", frame " + std::to_string(frame.first + 1);
        currentFile = name;
        Parser parser(frame.second, name);
        for (const auto& constant : options.constants) parser.defineConstant(constant.first, constant.second);
        if (options.includeHandler) parser.setIncludeHandler(options.includeHandler);
        auto program = parser.parse();
        if (parser.hasErrors() || !program) {
            std::istringstream lines(parser.getErrors());
            std::string line;
            while (std::getline(lines, line)) {
                const size_t at = line.find("line ");
                if (!line.empty()) error(line, at == std::string::npos ? 0 : std::atoi(line.c_str() + at + 5));
            }
            continue;
        }
        // The frame's code becomes a block; frame1's.
        auto block = std::make_unique<BlockStatement>(std::vector<StatementPtr>());
        for (auto& import : program->imports) {
            if (import->isPackage) unit->wildcards.push_back(import->path);
            else unit->imports[import->name->name] = import->path.empty() ? import->name->name : import->path + "." + import->name->name;
        }
        for (auto& node : program->statements) {
            if (auto* import = dynamic_cast<ImportDirective*>(node.get())) {
                if (import->isPackage) unit->wildcards.push_back(import->path);
                else unit->imports[import->name->name] = import->path.empty() ? import->name->name : import->path + "." + import->name->name;
                continue;
            }
            if (dynamic_cast<Statement*>(node.get())) {
                block->statements.push_back(StatementPtr(static_cast<Statement*>(node.release())));
            } else {
                error("this is not allowed in a frame script");
            }
        }
        // Kept on the program, so that its nodes live as long as it.
        source->program->statements.push_back(std::move(block));
        source->frames.push_back(frame.first);
        source->program->fileName = name;
    }
    unit->members.clear();
    for (auto& node : source->program->statements) unit->members.push_back(node.get());

    // Members: the stage's instances, then each frame's variables and
    // functions (public, as published movies have them).
    for (const StageInstance& instance : timeline.instances) {
        Member member;
        member.access = Access::Public;
        member.type = instance.type;
        if (!cls.instance.count(instance.name)) addMember(cls, instance.name, member);
    }
    for (AstNode* node : unit->members) {
        // Every var of a frame's code (outside its functions) and every
        // function it defines.
        Scan scan;
        scanStatement(static_cast<BlockStatement*>(node), scan);
        for (FunctionDefinition* function : scan.functions) {
            if (cls.instance.count(function->name->name)) {
                error("'" + function->name->name + "' is defined twice in the timeline", function->line);
                continue;
            }
            Member member;
            member.access = Access::Public;
            member.kind = Member::Method;
            addMember(cls, function->name->name, member);
        }
        for (const auto& var : scan.vars) {
            if (!scan.declared.count(var.first) || cls.instance.count(var.first)) continue;
            Member member;
            member.access = Access::Public;
            member.type = var.second;
            addMember(cls, var.first, member);
        }
    }
    for (const auto& frame : frames) {
        Member member;
        member.access = Access::Internal;
        member.kind = Member::Method;
        const std::string name = "frame" + std::to_string(frame.first + 1);
        if (!cls.instance.count(name)) addMember(cls, name, member);
    }

    Definition entry;
    entry.kind = Definition::Class;
    entry.cls = &cls;
    entry.mn = cls.nameMn;
    if (globals.count(timeline.className)) error(timeline.className + " is defined twice");
    globals[timeline.className] = entry;
    source->classes.push_back(&cls);
    source->units.push_back(std::move(unit));
    classes.push_back(std::move(model));
    files.push_back(std::move(source));
}

void AbcCompiler::resolveClasses() {
    for (auto& model : classes) {
        ClassModel& cls = *model;
        currentFile = cls.unit->fileName;
        currentLine = cls.line;
        if (cls.isInterface) {
            for (auto& face : cls.ast->interfaces) {
                TypeRef type = resolveType(face->name, cls.unit, true);
                if (type.mn) cls.interfaceMns.push_back(type.mn);
                else error("interface " + face->name + " is not known; import it by name");
            }
            continue;
        }
        if (cls.ast) {
            if (cls.ast->superClass && cls.ast->superClass->name != "Object") {
                const std::string& written = cls.ast->superClass->name;
                std::string qualified;
                std::string package;
                if (written.find('.') != std::string::npos) {
                    qualified = written;
                } else if (const Definition* definition = findDefinition(cls.unit, written, &package)) {
                    if (definition->kind == Definition::Class) qualified = definition->cls->qualified();
                } else if (cls.unit->imports.count(written)) {
                    qualified = cls.unit->imports[written];
                } else if (findPlayerClass("", written)) {
                    qualified = written;
                } else {
                    for (const std::string& wildcard : cls.unit->wildcards) {
                        if (findPlayerClass(wildcard, written)) qualified = wildcard + "." + written;
                    }
                }
                if (qualified.empty()) {
                    error("superclass " + written + " of " + cls.qualified() + " is not known; import it by name");
                    qualified = "Object";
                }
                cls.superName = qualified;
            } else {
                cls.superName = "Object";
            }
            for (auto& face : cls.ast->interfaces) {
                TypeRef type = resolveType(face->name, cls.unit, true);
                if (type.mn) cls.interfaceMns.push_back(type.mn);
                else error("interface " + face->name + " is not known; import it by name");
            }
        }
        // Outside-package classes keep their names in the file's
        // namespace; a definition found so gives its multiname.
        ClassModel* base = sourceClass(cls.superName);
        if (!base && cls.unit->file) {
            auto found = cls.unit->file->privateDefinitions.find(cls.superName);
            if (found != cls.unit->file->privateDefinitions.end() && found->second.kind == Definition::Class) {
                base = found->second.cls;
            }
        }
        cls.superMn = base ? base->nameMn
                           : pool.qname(packageNs(packageOf(cls.superName)), shortOf(cls.superName));
        if (base) cls.superName = base->qualified();
        if (base == &cls) {
            error(cls.qualified() + " extends itself");
            cls.superName = "Object";
            cls.superMn = publicName("Object");
        }
    }
}

// Constants

bool AbcCompiler::constantValue(Expression* e, Constant& out) {
    if (!e) return false;
    if (e->kind == EK::Literal) {
        auto* lit = static_cast<Literal*>(e);
        switch (lit->kind) {
            case Literal::LiteralKind::Numeric: out.kind = Constant::Number; out.number = lit->numberValue; return true;
            case Literal::LiteralKind::String: out.kind = Constant::String; out.text = lit->stringValue; return true;
            case Literal::LiteralKind::Boolean: out.kind = Constant::Boolean; out.boolean = lit->boolValue; return true;
            case Literal::LiteralKind::Null: out.kind = Constant::Null; return true;
            case Literal::LiteralKind::Undefined: out.kind = Constant::Undefined; return true;
            default: return false;
        }
    }
    if (e->kind == EK::Unary) {
        auto* u = static_cast<UnaryExpression*>(e);
        Constant inner;
        if ((u->op == OperatorType::Negative || u->op == OperatorType::Positive) && constantValue(u->operand.get(), inner) &&
            inner.kind == Constant::Number) {
            out = inner;
            if (u->op == OperatorType::Negative) out.number = -out.number;
            return true;
        }
        return false;
    }
    // A constant of a class, by name.
    ClassModel* owner = nullptr;
    std::string name;
    if (e->kind == EK::Identifier) {
        name = static_cast<Identifier*>(e)->name;
        if (name == "undefined") { out.kind = Constant::Undefined; return true; }
        if (name == "NaN") { out.kind = Constant::Number; out.number = std::numeric_limits<double>::quiet_NaN(); return true; }
        if (name == "Infinity") { out.kind = Constant::Number; out.number = std::numeric_limits<double>::infinity(); return true; }
        owner = cur && root().cls ? root().cls : constantClass;
    } else if (e->kind == EK::Member) {
        auto* m = static_cast<MemberExpression*>(e);
        if (m->computed || !m->property) return false;
        owner = classNamed(m->object.get());
        name = m->property->name;
    }
    for (ClassModel* c = owner; c && !name.empty(); c = superOf(c)) {
        auto found = c->statics.find(name);
        if (found != c->statics.end()) {
            if (found->second.kind != Member::Const || found->second.init == e) return false;
            Expression* init = found->second.init;
            found->second.init = nullptr;   // a guard against a constant naming itself
            const bool ok = constantValue(init, out);
            found->second.init = init;
            return ok;
        }
    }
    return false;
}

void AbcCompiler::constantDetail(const Constant& value, const TypeRef& type, uint8_t& kind, uint32_t& index) {
    switch (value.kind) {
        case Constant::Undefined: kind = CONSTANT_Undefined; index = 0; return;
        case Constant::Null: kind = CONSTANT_Null; index = CONSTANT_Null; return;
        case Constant::Boolean:
            kind = value.boolean ? CONSTANT_True : CONSTANT_False;
            index = kind;
            return;
        case Constant::String: kind = CONSTANT_Utf8; index = pool.string(value.text); return;
        case Constant::Number: {
            const double n = value.number;
            if (n == std::floor(n) && !(n == 0 && std::signbit(n))) {
                if (n >= -2147483648.0 && n <= 2147483647.0 && type.kind != TypeRef::Uint) {
                    kind = CONSTANT_Int;
                    index = pool.integer(static_cast<int32_t>(n));
                    return;
                }
                if (n >= 0 && n <= 4294967295.0) {
                    kind = CONSTANT_UInt;
                    index = pool.uinteger(static_cast<uint32_t>(n));
                    return;
                }
            }
            kind = CONSTANT_Double;
            index = pool.number(n);
            return;
        }
    }
}

// Methods

uint32_t AbcCompiler::methodInfo(const std::vector<std::unique_ptr<Parameter>>& params, Identifier* returnType,
                                 const std::string& name, Unit* unit, bool isSetter) {
    MethodInfo info;
    info.name = pool.string(name);
    bool defaults = false;
    for (const auto& param : params) {
        if (param->rest) {
            info.flags |= METHOD_NeedRest;
            continue;
        }
        const TypeRef type = resolveType(param->type ? param->type->name : std::string(), unit);
        info.paramTypes.push_back(type.mn);
        if (param->defaultValue) {
            defaults = true;
            Constant value;
            if (!constantValue(param->defaultValue.get(), value)) {
                error("the default of parameter " + param->name->name + " is not a constant");
            }
            OptionDetail option;
            constantDetail(value, type, option.kind, option.value);
            info.options.push_back(option);
        } else if (defaults) {
            error("parameter " + param->name->name + " needs a default, as those before it have");
        }
    }
    if (isSetter) {
        info.returnType = publicName("void");
    } else {
        info.returnType = returnType ? resolveType(returnType->name, unit).mn : 0;
    }
    file.methods.push_back(info);
    return static_cast<uint32_t>(file.methods.size() - 1);
}

uint32_t AbcCompiler::metadataOf(const std::vector<Metadata>& tags, std::vector<uint32_t>& out) {
    for (const Metadata& tag : tags) {
        MetadataInfo info;
        info.name = pool.string(tag.name);
        for (const auto& argument : tag.arguments) {
            info.items.emplace_back(argument.first.empty() ? 0 : pool.string(argument.first), pool.string(argument.second));
        }
        file.metadata.push_back(info);
        out.push_back(static_cast<uint32_t>(file.metadata.size() - 1));
    }
    return static_cast<uint32_t>(out.size());
}

void AbcCompiler::beginFunction(Function& f, const std::vector<std::unique_ptr<Parameter>>* params, Statement* body,
                                const std::string& selfName, const std::set<std::string>* members) {
    cur = &f;
    Scan scan;
    scanStatement(body, scan);
    MethodInfo& info = file.methods[f.method];
    CodeBuilder& c = f.code;

    // Registers: this, the parameters, the rest array or arguments.
    std::vector<std::pair<std::string, TypeRef>> paramList;
    uint32_t reg = 1;
    std::string restName;
    if (params) {
        for (const auto& param : *params) {
            if (param->rest) {
                restName = param->name->name;
                continue;
            }
            const TypeRef type = resolveType(param->type ? param->type->name : std::string(), f.unit, true);
            f.vars[param->name->name] = Var{false, reg++, type};
            paramList.emplace_back(param->name->name, type);
        }
    }
    bool declaresArguments = f.vars.count("arguments") > 0;
    for (const auto& var : scan.vars) {
        if (var.first == "arguments") declaresArguments = true;
    }
    if (!restName.empty()) {
        TypeRef array;
        array.kind = TypeRef::Other;
        array.mn = publicName("Array");
        f.vars[restName] = Var{false, reg++, array};
        if (scan.usesArguments && !declaresArguments) error("a function with ...rest cannot use arguments");
    } else if ((scan.usesArguments && !declaresArguments) || !selfName.empty()) {
        info.flags |= METHOD_NeedArguments;
        TypeRef object;
        object.kind = TypeRef::Object;
        object.mn = publicName("Object");
        f.vars["arguments"] = Var{false, reg++, object};
    }
    c.reserveRegisters(reg);

    // Locals: each name once; two declarations with different types are
    // untyped.
    std::map<std::string, std::string> locals;
    std::vector<std::string> order;
    for (const auto& var : scan.vars) {
        if (f.vars.count(var.first) || (members && members->count(var.first))) continue;
        auto found = locals.find(var.first);
        if (found == locals.end()) {
            locals[var.first] = var.second;
            order.push_back(var.first);
        } else if (found->second != var.second) {
            found->second = "*";
        }
    }
    if (!selfName.empty() && !f.vars.count(selfName) && !locals.count(selfName)) {
        locals[selfName] = "Function";
        order.push_back(selfName);
    }

    std::set<std::string> captured;
    for (const auto& name : scan.innerFree) {
        if (f.vars.count(name) || locals.count(name)) captured.insert(name);
    }

    if (f.pushesThis) {
        c.getLocal(0);
        c.op(OP_pushscope);
    }
    if (!captured.empty()) {
        f.hasActivation = true;
        info.flags |= METHOD_NeedActivation;
        f.activation = c.newRegister();
        c.op(OP_newactivation);
        c.op(OP_dup);
        c.setLocal(f.activation);
        c.op(OP_pushscope);
    }
    const auto slotFor = [&](const std::string& name, const TypeRef& type) {
        Trait trait;
        trait.name = activationName(name);
        trait.kind = TRAIT_Slot;
        trait.slotId = f.nextSlot++;
        trait.typeName = type.mn;
        c.activationTraits.push_back(trait);
        return trait.slotId;
    };
    // Captured parameters move into the activation.
    for (auto& param : f.vars) {
        if (!captured.count(param.first)) continue;
        const uint32_t from = param.second.index;
        const uint32_t slot = slotFor(param.first, param.second.type);
        c.getLocal(f.activation);
        c.getLocal(from);
        c.op(OP_setslot, slot);
        param.second.slot = true;
        param.second.index = slot;
    }
    for (const std::string& name : order) {
        const TypeRef type = resolveType(locals[name], f.unit);
        if (captured.count(name)) {
            f.vars[name] = Var{true, slotFor(name, type), type};
        } else {
            const uint32_t local = c.newRegister();
            f.vars[name] = Var{false, local, type};
            if (type.kind != TypeRef::Any) {
                pushDefault(type);
                c.setLocal(local);
            }
        }
    }
    if (!selfName.empty() && f.vars.count("arguments")) {
        // A named function expression finds itself by its name.
        const Var& self = f.vars[selfName];
        if (self.slot) c.getLocal(f.activation);
        c.getLocal(f.vars["arguments"].index);
        c.op(OP_getproperty, publicName("callee"));
        if (self.slot) c.op(OP_setslot, self.index);
        else c.setLocal(self.index);
    }
    // Nested functions, made before the code runs.
    for (FunctionDefinition* nested : scan.functions) {
        if (members && members->count(nested->name->name)) continue;
        const Var var = f.vars[nested->name->name];
        if (var.slot) c.getLocal(f.activation);
        const uint32_t m = compileClosure(nested->parameters, nested->returnType.get(), nested->body.get(),
                                          nested->name->name, std::string());
        c.op(OP_newfunction, m);
        if (var.slot) c.op(OP_setslot, var.index);
        else c.setLocal(var.index);
    }
}

void AbcCompiler::endFunction(Function& f) {
    CodeBuilder& c = f.code;
    if (c.reachable()) {
        if (f.returnsValue) {
            op(OP_pushundefined);
            op(OP_returnvalue);
        } else {
            op(OP_returnvoid);
        }
    }
    if (!c.error().empty()) {
        error("internal error in the code made for " + file.pool.strings[file.methods[f.method].name] + ": " + c.error());
    }
    file.bodies.push_back(c.finish(f.method, f.initScope));
}

uint32_t AbcCompiler::compileClosure(const std::vector<std::unique_ptr<Parameter>>& params, Identifier* returnType,
                                     Statement* body, const std::string& name, const std::string& selfName) {
    Function* outer = cur;
    const uint32_t m = methodInfo(params, returnType, name.empty() ? std::string() : name, outer->unit, false);
    Function f(file);
    f.parent = outer;
    f.unit = outer->unit;
    f.cls = outer->cls;
    f.isStatic = outer->isStatic;
    f.method = m;
    f.initScope = outer->initScope + static_cast<uint32_t>(std::max(0, outer->code.scopeDepth()));
    f.returnType = resolveType(returnType ? returnType->name : std::string(), f.unit, true);
    f.returnsValue = returnType && f.returnType.kind != TypeRef::Void;
    const int savedLine = currentLine;
    beginFunction(f, &params, body, selfName);
    statement(body);
    endFunction(f);
    cur = outer;
    currentLine = savedLine;
    return m;
}

uint32_t AbcCompiler::compileMethod(FunctionDefinition* definition, ClassModel* cls, bool isStatic,
                                    const std::string& name, uint32_t initScope) {
    const uint32_t m = methodInfo(definition->parameters, definition->returnType.get(), name, cls->unit,
                                  definition->isSetter);
    if (hasAttribute(definition->attributes, AttributeType::Native)) {
        file.methods[m].flags |= METHOD_Native;
        return m;
    }
    if (cls->isInterface || !definition->body) {
        if (!cls->isInterface) error("method " + definition->name->name + " has no body", definition->line);
        return m;
    }
    Function f(file);
    f.unit = cls->unit;
    f.cls = cls;
    f.isStatic = isStatic;
    f.isMethod = true;
    f.pushesThis = true;
    f.method = m;
    f.initScope = initScope;
    f.returnType = resolveType(definition->returnType ? definition->returnType->name : std::string(), f.unit, true);
    f.returnsValue = definition->returnType && f.returnType.kind != TypeRef::Void && !definition->isSetter;
    currentLine = definition->line;
    beginFunction(f, &definition->parameters, definition->body.get());
    statement(definition->body.get());
    endFunction(f);
    cur = nullptr;
    return m;
}

// Classes and scripts

void AbcCompiler::emitClassDefinition(ClassModel& cls, Function& script) {
    // getscopeobject 0, the superclasses as scopes, newclass, into the
    // script's slot.
    CodeBuilder& c = script.code;
    c.op(OP_getscopeobject, 0);
    if (cls.isInterface) {
        c.op(OP_pushnull);
        c.op(OP_newclass, static_cast<uint32_t>(cls.classIndex));
    } else {
        const std::vector<std::string> chain = superChain(&cls);
        for (const std::string& name : chain) {
            ClassModel* model = sourceClass(name);
            c.op(OP_getlex, model ? model->nameMn : (name == "Object" ? publicName("Object") : pool.qname(packageNs(packageOf(name)), shortOf(name))));
            c.op(OP_pushscope);
        }
        c.op(OP_getlex, cls.superMn);
        c.op(OP_newclass, static_cast<uint32_t>(cls.classIndex));
        for (size_t i = 0; i < chain.size(); ++i) c.op(OP_popscope);
    }
    c.op(OP_initproperty, cls.nameMn);
}

void AbcCompiler::compileClass(ClassModel& cls, Function& script) {
    currentFile = cls.unit->fileName;
    currentLine = cls.line;
    constantClass = &cls;
    const uint32_t chainScopes = cls.isInterface ? 0 : static_cast<uint32_t>(superChain(&cls).size());
    // As compilers count them: the script's own, then its scope stack at
    // newclass, then the class; methods one more.
    const uint32_t classScope = 1 + 1 + chainScopes;
    const uint32_t instanceScope = classScope + 1;

    InstanceInfo instance;
    ClassInfo info;
    instance.name = cls.nameMn;
    instance.superName = cls.isInterface ? 0 : cls.superMn;
    instance.interfaces = cls.interfaceMns;
    if (cls.isInterface) {
        instance.flags = INSTANCE_Interface | INSTANCE_Sealed;
    } else {
        instance.flags = INSTANCE_ProtectedNs;
        if (!cls.isDynamic) instance.flags |= INSTANCE_Sealed;
        if (cls.isFinal) instance.flags |= INSTANCE_Final;
        instance.protectedNs = cls.protectedNs;
    }

    std::vector<std::pair<uint32_t, Expression*>> instanceInits;
    std::vector<std::pair<uint32_t, Expression*>> staticInits;
    std::vector<Statement*> staticCode;
    FunctionDefinition* constructor = nullptr;
    const std::string where = cls.package.empty() ? cls.name : cls.package + ":" + cls.name;

    for (auto& node : cls.ast->members) {
        if (auto* function = dynamic_cast<FunctionDefinition*>(node.get())) {
            const bool isStatic = hasAttribute(function->attributes, AttributeType::Static);
            if (!isStatic && function->name->name == cls.name && !function->isGetter && !function->isSetter) {
                constructor = function;
                continue;
            }
            const Member& member = (isStatic ? cls.statics : cls.instance)[function->name->name];
            const std::string prefix = function->isGetter ? "get " : function->isSetter ? "set " : "";
            Trait trait;
            trait.name = pool.qname(member.ns, function->name->name);
            trait.kind = function->isGetter ? TRAIT_Getter : function->isSetter ? TRAIT_Setter : TRAIT_Method;
            if (hasAttribute(function->attributes, AttributeType::Override)) trait.attributes |= ATTR_Override;
            if (hasAttribute(function->attributes, AttributeType::Final)) trait.attributes |= ATTR_Final;
            trait.index = compileMethod(function, &cls, isStatic, where + "/" + prefix + function->name->name,
                                        isStatic ? classScope : instanceScope);
            metadataOf(function->metadata, trait.metadata);
            (isStatic ? info.traits : instance.traits).push_back(trait);
        } else if (auto* variables = dynamic_cast<VariableDefinition*>(node.get())) {
            const bool isStatic = hasAttribute(variables->attributes, AttributeType::Static);
            for (auto& decl : variables->declarations) {
                const Member& member = (isStatic ? cls.statics : cls.instance)[decl->id->name];
                Trait trait;
                trait.name = pool.qname(member.ns, decl->id->name);
                trait.kind = variables->isConst ? TRAIT_Const : TRAIT_Slot;
                const TypeRef type = resolveType(member.type, cls.unit);
                trait.typeName = type.mn;
                Constant value;
                cur = nullptr;
                if (decl->init && constantValue(decl->init.get(), value)) {
                    constantDetail(value, type, trait.valueKind, trait.valueIndex);
                } else if (decl->init) {
                    (isStatic ? staticInits : instanceInits).emplace_back(trait.name, decl->init.get());
                }
                metadataOf(variables->metadata, trait.metadata);
                (isStatic ? info.traits : instance.traits).push_back(trait);
            }
        } else if (dynamic_cast<NamespaceDefinition*>(node.get())) {
            // Namespaces are accepted, their members are internal.
        } else if (auto* stmt = dynamic_cast<Statement*>(node.get())) {
            if (!dynamic_cast<EmptyStatement*>(stmt)) staticCode.push_back(stmt);
        }
    }

    // The constructor: field initializers, super() when the body does not
    // call it, the body.
    if (cls.isInterface) {
        MethodInfo empty;
        empty.name = pool.string(where);
        file.methods.push_back(empty);
        instance.iinit = static_cast<uint32_t>(file.methods.size() - 1);
    } else {
        static const std::vector<std::unique_ptr<Parameter>> noParams;
        const auto& params = constructor ? constructor->parameters : noParams;
        instance.iinit = methodInfo(params, nullptr, where, cls.unit, false);
        Function f(file);
        f.unit = cls.unit;
        f.cls = &cls;
        f.isMethod = true;
        f.isConstructor = true;
        f.pushesThis = true;
        f.method = instance.iinit;
        f.initScope = instanceScope;
        currentLine = constructor ? constructor->line : cls.line;
        Statement* body = constructor ? constructor->body.get() : nullptr;
        beginFunction(f, &params, body);
        for (const auto& init : instanceInits) {
            f.code.getLocal(0);
            expr(init.second);
            op(OP_initproperty, init.first);
        }
        if (!body || !callsSuper(body)) {
            f.code.getLocal(0);
            op(OP_constructsuper, 0);
        }
        if (body) statement(body);
        endFunction(f);
        cur = nullptr;
    }

    // The static initializer.
    {
        MethodInfo cinit;
        cinit.name = pool.string(where + "$cinit");
        file.methods.push_back(cinit);
        info.cinit = static_cast<uint32_t>(file.methods.size() - 1);
        Function f(file);
        f.unit = cls.unit;
        f.cls = &cls;
        f.isStatic = true;
        f.isMethod = true;
        f.pushesThis = true;
        f.method = info.cinit;
        f.initScope = classScope;
        BlockStatement block{std::vector<StatementPtr>()};
        beginFunction(f, nullptr, &block);
        for (const auto& init : staticInits) {
            f.code.getLocal(0);
            expr(init.second);
            op(OP_initproperty, init.first);
        }
        for (Statement* stmt : staticCode) statement(stmt);
        endFunction(f);
        cur = nullptr;
    }

    file.instances[cls.classIndex] = std::move(instance);
    file.classes[cls.classIndex] = std::move(info);

    // The class in its script.
    Trait trait;
    trait.name = cls.nameMn;
    trait.kind = TRAIT_Class;
    trait.index = static_cast<uint32_t>(cls.classIndex);
    metadataOf(cls.ast->metadata, trait.metadata);
    file.scripts.back().traits.push_back(trait);
    constantClass = nullptr;
    cur = &script;
    emitClassDefinition(cls, script);
}

void AbcCompiler::compileTimeline(ClassModel& cls) {
    currentFile = cls.qualified();
    const uint32_t classScope = 1 + 1 + static_cast<uint32_t>(superChain(&cls).size());
    const uint32_t instanceScope = classScope + 1;
    const std::string where = cls.package.empty() ? cls.name : cls.package + ":" + cls.name;
    InstanceInfo instance;
    ClassInfo info;
    instance.name = cls.nameMn;
    instance.superName = cls.superMn;
    instance.flags = INSTANCE_ProtectedNs;
    instance.protectedNs = cls.protectedNs;

    // Stage instances and frame variables, then frame functions.
    std::set<std::string> members;
    for (const auto& entry : cls.instance) members.insert(entry.first);
    for (const auto& entry : cls.instance) {
        if (entry.second.kind != Member::Var) continue;
        Trait trait;
        trait.name = pool.qname(publicNs(), entry.first);
        trait.typeName = resolveType(entry.second.type, cls.unit).mn;
        instance.traits.push_back(trait);
    }
    std::vector<std::pair<int, uint32_t>> frameMethods;
    for (size_t i = 0; i < cls.unit->members.size(); ++i) {
        auto* block = static_cast<BlockStatement*>(cls.unit->members[i]);
        const int frame = cls.unit->file->frames[i];
        currentFile = cls.qualified() + ", frame " + std::to_string(frame + 1);
        Scan scan;
        scanStatement(block, scan);
        for (FunctionDefinition* function : scan.functions) {
            Trait trait;
            trait.name = pool.qname(publicNs(), function->name->name);
            trait.kind = TRAIT_Method;
            trait.index = compileMethod(function, &cls, false, where + "/" + function->name->name, instanceScope);
            instance.traits.push_back(trait);
        }
        // The frame's code: frameN, whose variables are the class's.
        const std::string name = "frame" + std::to_string(frame + 1);
        MethodInfo frameInfo;
        frameInfo.name = pool.string(where + "/" + name);
        file.methods.push_back(frameInfo);
        Function f(file);
        f.unit = cls.unit;
        f.cls = &cls;
        f.isMethod = true;
        f.pushesThis = true;
        f.method = static_cast<uint32_t>(file.methods.size() - 1);
        f.initScope = instanceScope;
        beginFunction(f, nullptr, block, std::string(), &members);
        statement(block);
        endFunction(f);
        cur = nullptr;
        Trait trait;
        trait.name = pool.qname(internalNs(cls.package), name);
        trait.kind = TRAIT_Method;
        trait.index = f.method;
        instance.traits.push_back(trait);
        frameMethods.emplace_back(frame, trait.name);
    }

    // The constructor: super(), then addFrameScript(0, frame1, ...).
    {
        MethodInfo ctor;
        ctor.name = pool.string(where);
        file.methods.push_back(ctor);
        instance.iinit = static_cast<uint32_t>(file.methods.size() - 1);
        Function f(file);
        f.unit = cls.unit;
        f.cls = &cls;
        f.isMethod = true;
        f.pushesThis = true;
        f.method = instance.iinit;
        f.initScope = instanceScope;
        BlockStatement empty{std::vector<StatementPtr>()};
        beginFunction(f, nullptr, &empty);
        f.code.getLocal(0);
        op(OP_constructsuper, 0);
        if (!frameMethods.empty()) {
            const uint32_t addFrameScript = publicName("addFrameScript");
            op(OP_findpropstrict, addFrameScript);
            for (const auto& frame : frameMethods) {
                f.code.pushInt(frame.first, pool);
                f.code.getLocal(0);
                op(OP_getproperty, frame.second);
            }
            op(OP_callpropvoid, addFrameScript, static_cast<uint32_t>(frameMethods.size() * 2));
        }
        endFunction(f);
        cur = nullptr;
    }
    {
        MethodInfo cinit;
        cinit.name = pool.string(where + "$cinit");
        file.methods.push_back(cinit);
        info.cinit = static_cast<uint32_t>(file.methods.size() - 1);
        Function f(file);
        f.unit = cls.unit;
        f.cls = &cls;
        f.isStatic = true;
        f.isMethod = true;
        f.pushesThis = true;
        f.method = info.cinit;
        f.initScope = classScope;
        BlockStatement empty{std::vector<StatementPtr>()};
        beginFunction(f, nullptr, &empty);
        endFunction(f);
        cur = nullptr;
    }
    file.instances[cls.classIndex] = std::move(instance);
    file.classes[cls.classIndex] = std::move(info);
}

void AbcCompiler::compileFile(SourceFile& source) {
    currentFile = source.fileName;
    // Each file is a script: its classes (each after what it extends in
    // the file), its functions and variables, its code outside them.
    MethodInfo init;
    init.name = pool.string("");
    file.methods.push_back(init);
    ScriptInfo script;
    script.init = static_cast<uint32_t>(file.methods.size() - 1);
    file.scripts.push_back(script);

    std::vector<ClassModel*> ordered;
    std::set<ClassModel*> placed;
    std::function<void(ClassModel*)> place = [&](ClassModel* cls) {
        if (placed.count(cls)) return;
        placed.insert(cls);
        for (ClassModel* other : source.classes) {
            if (other != cls && (other->qualified() == cls->superName || other->name == cls->superName)) place(other);
        }
        ordered.push_back(cls);
    };
    for (ClassModel* cls : source.classes) place(cls);
    for (ClassModel* cls : ordered) {
        cls->classIndex = static_cast<int>(file.instances.size());
        file.instances.emplace_back();
        file.classes.emplace_back();
    }

    Function f(file);
    f.isScript = true;
    f.pushesThis = true;
    f.method = file.scripts.back().init;
    f.initScope = 1;
    f.unit = source.units.empty() ? nullptr : source.units.back().get();

    // The script's code is its packages' and the outside's, so its
    // functions are the outside's; it has no locals of its own.
    BlockStatement none{std::vector<StatementPtr>()};
    beginFunction(f, nullptr, &none);

    if (source.timeline) {
        compileTimeline(*ordered.front());
        Trait trait;
        trait.name = ordered.front()->nameMn;
        trait.kind = TRAIT_Class;
        trait.index = static_cast<uint32_t>(ordered.front()->classIndex);
        file.scripts.back().traits.push_back(trait);
        cur = &f;
        emitClassDefinition(*ordered.front(), f);
        output.classes.push_back(ordered.front()->qualified());
    } else {
        for (ClassModel* cls : ordered) {
            compileClass(*cls, f);
            if (!cls->outside) output.classes.push_back(cls->qualified());
        }
    }
    cur = &f;

    for (auto& unit : source.units) {
        f.unit = unit.get();
        currentFile = source.fileName;
        for (AstNode* node : unit->members) {
            if (auto* function = dynamic_cast<FunctionDefinition*>(node)) {
                const Definition* definition = findDefinition(unit.get(), function->name->name);
                Trait trait;
                trait.name = definition ? definition->mn : publicName(function->name->name);
                trait.kind = TRAIT_Method;
                const std::string name = unit->package.empty() ? function->name->name : unit->package + ":" + function->name->name;
                const uint32_t m = compileClosure(function->parameters, function->returnType.get(), function->body.get(),
                                                  name, std::string());
                trait.index = m;
                file.scripts.back().traits.push_back(trait);
            } else if (auto* variables = dynamic_cast<VariableDefinition*>(node)) {
                for (auto& decl : variables->declarations) {
                    const Definition* definition = findDefinition(unit.get(), decl->id->name);
                    Trait trait;
                    trait.name = definition ? definition->mn : publicName(decl->id->name);
                    trait.kind = variables->isConst ? TRAIT_Const : TRAIT_Slot;
                    trait.typeName = resolveType(decl->type ? decl->type->name : std::string(), unit.get()).mn;
                    file.scripts.back().traits.push_back(trait);
                    if (decl->init) {
                        currentLine = variables->line;
                        f.code.op(OP_getscopeobject, 0);
                        expr(decl->init.get());
                        op(OP_initproperty, trait.name);
                    }
                }
            } else if (dynamic_cast<ClassDefinition*>(node) || dynamic_cast<NamespaceDefinition*>(node)) {
                continue;
            } else if (auto* stmt = dynamic_cast<Statement*>(node)) {
                if (!source.timeline) statement(stmt);
            }
        }
    }
    endFunction(f);
    cur = nullptr;
}

Output AbcCompiler::run(const std::vector<Source>& sources, const std::vector<Timeline>& timelines) {
    parseSources(sources);
    for (auto& source : files) collectDefinitions(*source);
    for (const Timeline& timeline : timelines) buildTimeline(timeline);
    resolveClasses();
    if (!hasErrors()) {
        // Scripts in the order given, the timelines last.
        for (auto& source : files) {
            compileFile(*source);
        }
    }
    if (!hasErrors()) {
        output.abc = write(file);
        if (options.verify) {
            for (const std::string& problem : verify(file)) {
                output.diagnostics.push_back({true, std::string(), 0, "internal error, the output does not verify: " + problem});
            }
        }
    }
    output.ok = !hasErrors();
    if (!output.ok) output.abc.clear();
    return output;
}

// Statements

void AbcCompiler::statement(Statement* stmt) {
    if (!stmt) return;
    if (stmt->line > 0) {
        currentLine = stmt->line;
        if (options.debugInfo && cur->lastLine != stmt->line && cur->code.reachable()) {
            if (cur->lastLine < 0) op(OP_debugfile, pool.string(currentFile));
            op(OP_debugline, static_cast<uint32_t>(stmt->line));
            cur->lastLine = stmt->line;
        }
    }
    if (auto* block = dynamic_cast<BlockStatement*>(stmt)) {
        statements(block->statements);
    } else if (auto* expression = dynamic_cast<ExpressionStatement*>(stmt)) {
        discard(expression->expression.get());
    } else if (auto* branch = dynamic_cast<IfStatement*>(stmt)) {
        ifStatement(branch);
    } else if (auto* loop = dynamic_cast<WhileStatement*>(stmt)) {
        whileStatement(loop, {});
    } else if (auto* doLoop = dynamic_cast<DoWhileStatement*>(stmt)) {
        doWhileStatement(doLoop, {});
    } else if (auto* forLoop = dynamic_cast<ForStatement*>(stmt)) {
        forStatement(forLoop, {});
    } else if (auto* forIn = dynamic_cast<ForInStatement*>(stmt)) {
        forInStatement(forIn, {});
    } else if (auto* choice = dynamic_cast<SwitchStatement*>(stmt)) {
        switchStatement(choice, {});
    } else if (auto* labeled = dynamic_cast<LabeledStatement*>(stmt)) {
        labeledStatement(labeled);
    } else if (auto* result = dynamic_cast<ReturnStatement*>(stmt)) {
        returnStatement(result);
    } else if (auto* variables = dynamic_cast<VariableDefinition*>(stmt)) {
        variableDefinition(variables);
    } else if (dynamic_cast<FunctionDefinition*>(stmt)) {
        // Made at the function's start.
    } else if (dynamic_cast<ClassDefinition*>(stmt)) {
        error("a class cannot be defined here");
    } else if (auto* breakStmt = dynamic_cast<BreakStatement*>(stmt)) {
        breakContinue(breakStmt->label ? breakStmt->label->name : std::string(), false);
    } else if (auto* continueStmt = dynamic_cast<ContinueStatement*>(stmt)) {
        breakContinue(continueStmt->label ? continueStmt->label->name : std::string(), true);
    } else if (auto* thrown = dynamic_cast<ThrowStatement*>(stmt)) {
        expr(thrown->argument.get());
        op(OP_throw);
    } else if (auto* attempt = dynamic_cast<TryStatement*>(stmt)) {
        tryStatement(attempt);
    } else if (auto* with = dynamic_cast<WithStatement*>(stmt)) {
        withStatement(with);
    } else if (dynamic_cast<ForOfStatement*>(stmt)) {
        error("for-of is not ActionScript 3");
    }
    // Empty statements and namespace definitions make nothing.
}

void AbcCompiler::variableDefinition(VariableDefinition* stmt) {
    for (auto& decl : stmt->declarations) {
        if (!decl->init) continue;
        // A local, or (in a script or a frame) a slot or member.
        Identifier name(decl->id->name);
        Target target;
        targetOf(&name, target, false);
        if (target.baseCount) target.pushBase();
        expr(decl->init.get());
        if (target.kind != Target::Register) coerce(target.type);
        storeTarget(target);
    }
}

void AbcCompiler::ifStatement(IfStatement* stmt) {
    auto otherwise = code().newLabel();
    condJump(stmt->test.get(), false, otherwise);
    statement(stmt->consequent.get());
    if (stmt->alternate) {
        auto end = code().newLabel();
        code().jump(OP_jump, end);
        code().bind(otherwise);
        statement(stmt->alternate.get());
        code().bind(end);
    } else {
        code().bind(otherwise);
    }
}

void AbcCompiler::whileStatement(WhileStatement* stmt, const std::vector<std::string>& labels) {
    CodeBuilder& c = code();
    auto body = c.newLabel();
    Control control;
    control.kind = Control::Loop;
    control.labels = labels;
    control.breakLabel = c.newLabel();
    control.continueLabel = c.newLabel();
    c.jump(OP_jump, control.continueLabel);
    c.bind(body, true);
    cur->controls.push_back(control);
    statement(stmt->body.get());
    cur->controls.pop_back();
    c.bind(control.continueLabel);
    condJump(stmt->test.get(), true, body);
    c.bind(control.breakLabel);
}

void AbcCompiler::doWhileStatement(DoWhileStatement* stmt, const std::vector<std::string>& labels) {
    CodeBuilder& c = code();
    auto body = c.newLabel();
    Control control;
    control.kind = Control::Loop;
    control.labels = labels;
    control.breakLabel = c.newLabel();
    control.continueLabel = c.newLabel();
    c.bind(body, true);
    cur->controls.push_back(control);
    statement(stmt->body.get());
    cur->controls.pop_back();
    c.bind(control.continueLabel);
    condJump(stmt->test.get(), true, body);
    c.bind(control.breakLabel);
}

void AbcCompiler::forStatement(ForStatement* stmt, const std::vector<std::string>& labels) {
    CodeBuilder& c = code();
    statement(stmt->init.get());
    auto body = c.newLabel();
    auto test = c.newLabel();
    Control control;
    control.kind = Control::Loop;
    control.labels = labels;
    control.breakLabel = c.newLabel();
    control.continueLabel = c.newLabel();
    if (stmt->test) c.jump(OP_jump, test);
    c.bind(body, true);
    cur->controls.push_back(control);
    statement(stmt->body.get());
    cur->controls.pop_back();
    c.bind(control.continueLabel);
    if (stmt->update) discard(stmt->update.get());
    if (stmt->test) {
        c.bind(test);
        condJump(stmt->test.get(), true, body);
    } else {
        c.jump(OP_jump, body);
    }
    c.bind(control.breakLabel);
}

void AbcCompiler::forInStatement(ForInStatement* stmt, const std::vector<std::string>& labels) {
    CodeBuilder& c = code();
    expr(stmt->right.get());
    op(OP_coerce_a);
    const uint32_t object = c.newTemp();
    c.setLocal(object);
    const uint32_t index = c.newTemp();
    c.pushInt(0, pool);
    c.setLocal(index);
    auto body = c.newLabel();
    Control control;
    control.kind = Control::Loop;
    control.labels = labels;
    control.breakLabel = c.newLabel();
    control.continueLabel = c.newLabel();
    c.jump(OP_jump, control.continueLabel);
    c.bind(body, true);
    c.getLocal(object);
    c.getLocal(index);
    op(stmt->each ? OP_nextvalue : OP_nextname);
    if (auto* variables = dynamic_cast<VariableDefinition*>(stmt->left.get())) {
        if (variables->declarations.size() != 1) error("for-in takes one variable");
        if (!variables->declarations.empty()) {
            auto id = std::make_unique<Identifier>(variables->declarations[0]->id->name);
            storeFromStack(id.get());
        } else {
            op(OP_pop);
        }
    } else if (auto* expression = dynamic_cast<ExpressionStatement*>(stmt->left.get())) {
        storeFromStack(expression->expression.get());
    } else {
        error("for-in needs a variable");
        op(OP_pop);
    }
    cur->controls.push_back(control);
    statement(stmt->body.get());
    cur->controls.pop_back();
    c.bind(control.continueLabel);
    c.op(OP_hasnext2, object, index);
    c.jump(OP_iftrue, body);
    c.bind(control.breakLabel);
    c.freeTemp(index);
    c.freeTemp(object);
}

void AbcCompiler::switchStatement(SwitchStatement* stmt, const std::vector<std::string>& labels) {
    CodeBuilder& c = code();
    expr(stmt->discriminant.get());
    const uint32_t value = c.newTemp();
    c.setLocal(value);
    std::vector<CodeBuilder::Label> cases;
    int defaultCase = -1;
    for (size_t i = 0; i < stmt->cases.size(); ++i) {
        cases.push_back(c.newLabel());
        if (!stmt->cases[i]->test) defaultCase = static_cast<int>(i);
    }
    Control control;
    control.kind = Control::Switch;
    control.labels = labels;
    control.breakLabel = c.newLabel();
    for (size_t i = 0; i < stmt->cases.size(); ++i) {
        if (!stmt->cases[i]->test) continue;
        expr(stmt->cases[i]->test.get());
        c.getLocal(value);
        c.jump(OP_ifstricteq, cases[i]);
    }
    c.jump(OP_jump, defaultCase >= 0 ? cases[defaultCase] : control.breakLabel);
    cur->controls.push_back(control);
    for (size_t i = 0; i < stmt->cases.size(); ++i) {
        c.bind(cases[i]);
        statements(stmt->cases[i]->consequent);
    }
    cur->controls.pop_back();
    c.bind(control.breakLabel);
    c.freeTemp(value);
}

void AbcCompiler::labeledStatement(LabeledStatement* stmt) {
    std::vector<std::string> labels;
    Statement* body = stmt;
    while (auto* labeled = dynamic_cast<LabeledStatement*>(body)) {
        labels.push_back(labeled->label->name);
        body = labeled->body.get();
    }
    if (auto* loop = dynamic_cast<WhileStatement*>(body)) whileStatement(loop, labels);
    else if (auto* doLoop = dynamic_cast<DoWhileStatement*>(body)) doWhileStatement(doLoop, labels);
    else if (auto* forLoop = dynamic_cast<ForStatement*>(body)) forStatement(forLoop, labels);
    else if (auto* forIn = dynamic_cast<ForInStatement*>(body)) forInStatement(forIn, labels);
    else if (auto* choice = dynamic_cast<SwitchStatement*>(body)) switchStatement(choice, labels);
    else {
        Control control;
        control.kind = Control::Block;
        control.labels = labels;
        control.breakLabel = code().newLabel();
        cur->controls.push_back(control);
        statement(body);
        cur->controls.pop_back();
        code().bind(control.breakLabel);
    }
}

void AbcCompiler::pauseFrom(size_t index) {
    const size_t here = code().here();
    for (size_t i = index; i < cur->controls.size(); ++i) {
        Control& control = cur->controls[i];
        if (control.kind != Control::Try) continue;
        if (control.catchesOpen) {
            control.catchRanges.emplace_back(control.catchStart, here);
            control.catchesOpen = false;
            control.pausedCatches = true;
        }
        if (control.finallyOpen) {
            control.finallyRanges.emplace_back(control.finallyStart, here);
            control.finallyOpen = false;
            control.pausedFinally = true;
        }
    }
}

void AbcCompiler::resumeAll() {
    const size_t here = code().here();
    for (Control& control : cur->controls) {
        if (control.pausedCatches) {
            control.catchesOpen = true;
            control.catchStart = here;
            control.pausedCatches = false;
        }
        if (control.pausedFinally) {
            control.finallyOpen = true;
            control.finallyStart = here;
            control.pausedFinally = false;
        }
    }
}

void AbcCompiler::inlineFinally(size_t index) {
    // The finally block of controls[index], outside the ranges of that try
    // and those in it, and compiled with only the controls outside it.
    pauseFrom(index);
    std::vector<Control> inner(cur->controls.begin() + static_cast<std::ptrdiff_t>(index), cur->controls.end());
    cur->controls.resize(index);
    statement(inner.front().finallyBody);
    cur->controls.insert(cur->controls.end(), inner.begin(), inner.end());
}

void AbcCompiler::exitTo(size_t depth) {
    // Leaving the controls from the innermost to depth: their withs'
    // scopes popped, their finally blocks run.
    for (size_t i = cur->controls.size(); i-- > depth;) {
        const Control& control = cur->controls[i];
        if (control.kind == Control::With) {
            op(OP_popscope);
        } else if (control.kind == Control::Try && control.finallyBody) {
            inlineFinally(i);
        }
    }
}

void AbcCompiler::breakContinue(const std::string& label, bool isContinue) {
    for (size_t i = cur->controls.size(); i-- > 0;) {
        const Control& control = cur->controls[i];
        bool match;
        if (!label.empty()) {
            match = std::find(control.labels.begin(), control.labels.end(), label) != control.labels.end();
            if (match && isContinue && control.kind != Control::Loop) {
                error("continue " + label + " is not a loop's label");
                return;
            }
        } else if (isContinue) {
            match = control.kind == Control::Loop;
        } else {
            match = control.kind == Control::Loop || control.kind == Control::Switch;
        }
        if (!match) continue;
        const CodeBuilder::Label target = isContinue ? control.continueLabel : control.breakLabel;
        exitTo(i + 1);
        code().jump(OP_jump, target);
        resumeAll();
        return;
    }
    error(std::string(isContinue ? "continue" : "break") + (label.empty() ? " outside a loop" : " to an unknown label " + label));
}

void AbcCompiler::returnStatement(ReturnStatement* stmt) {
    bool leaves = false;
    for (const Control& control : cur->controls) {
        if (control.kind == Control::With || (control.kind == Control::Try && control.finallyBody)) leaves = true;
    }
    if (stmt->argument && !cur->returnsValue && (cur->isConstructor || cur->isMethod) && root().cls &&
        file.methods[cur->method].returnType) {
        error("a function returning void returns a value");
    }
    if (!stmt->argument) {
        if (leaves) exitTo(0);
        op(OP_returnvoid);
        resumeAll();
        return;
    }
    expr(stmt->argument.get());
    if (leaves) {
        const uint32_t value = code().newTemp();
        code().setLocal(value);
        exitTo(0);
        code().getLocal(value);
        op(OP_returnvalue);
        resumeAll();
        code().freeTemp(value);
    } else {
        op(OP_returnvalue);
    }
}

void AbcCompiler::restoreScopes(size_t controlDepth) {
    // A handler starts with no scopes: those the function had pushed by
    // then, again.
    if (cur->pushesThis) {
        code().getLocal(0);
        op(OP_pushscope);
    }
    if (cur->hasActivation) {
        code().getLocal(cur->activation);
        op(OP_pushscope);
    }
    for (size_t i = 0; i < controlDepth && i < cur->controls.size(); ++i) {
        if (cur->controls[i].kind == Control::With) {
            code().getLocal(cur->controls[i].withRegister);
            op(OP_pushwith);
        }
    }
}

void AbcCompiler::tryStatement(TryStatement* stmt) {
    CodeBuilder& c = code();
    const size_t depth = cur->controls.size();
    Control control;
    control.kind = Control::Try;
    control.finallyBody = stmt->finallyBody.get();
    control.catchesOpen = !stmt->catches.empty();
    control.catchStart = c.here();
    control.finallyOpen = control.finallyBody != nullptr;
    control.finallyStart = c.here();
    cur->controls.push_back(control);

    auto end = c.newLabel();
    statement(stmt->block.get());

    Control* self = &cur->controls[depth];
    const auto close = [&c](bool& open, size_t start, std::vector<std::pair<size_t, size_t>>& ranges) {
        if (open) {
            ranges.emplace_back(start, c.here());
            open = false;
        }
    };
    close(self->catchesOpen, self->catchStart, self->catchRanges);
    close(self->finallyOpen, self->finallyStart, self->finallyRanges);
    if (c.reachable() && stmt->finallyBody) inlineFinally(depth);
    c.jump(OP_jump, end);

    std::vector<CodeBuilder::Label> handlers;
    for (auto& clause : stmt->catches) {
        auto handler = c.newLabel();
        handlers.push_back(handler);
        c.bindHandler(handler);
        restoreScopes(depth);
        auto id = std::make_unique<Identifier>(clause.variable->name);
        storeFromStack(id.get());
        self = &cur->controls[depth];
        if (stmt->finallyBody) {
            self->finallyOpen = true;
            self->finallyStart = c.here();
        }
        statement(clause.body.get());
        self = &cur->controls[depth];
        close(self->finallyOpen, self->finallyStart, self->finallyRanges);
        if (c.reachable() && stmt->finallyBody) inlineFinally(depth);
        c.jump(OP_jump, end);
    }

    CodeBuilder::Label finallyHandler;
    if (stmt->finallyBody) {
        finallyHandler = c.newLabel();
        c.bindHandler(finallyHandler);
        restoreScopes(depth);
        const uint32_t thrown = c.newTemp();
        c.setLocal(thrown);
        std::vector<Control> saved(cur->controls.begin() + static_cast<std::ptrdiff_t>(depth), cur->controls.end());
        cur->controls.resize(depth);
        statement(stmt->finallyBody.get());
        cur->controls.insert(cur->controls.end(), saved.begin(), saved.end());
        if (c.reachable()) {
            c.getLocal(thrown);
            op(OP_throw);
        }
        c.freeTemp(thrown);
    }
    c.bind(end);

    // The exception table: this try's handlers after those of the tries in
    // it, which were added as they ended.
    self = &cur->controls[depth];
    for (size_t i = 0; i < stmt->catches.size(); ++i) {
        const TypeRef type = resolveType(stmt->catches[i].type, cur->unit);
        for (const auto& range : self->catchRanges) {
            if (range.first == range.second) continue;
            ExceptionInfo entry;
            entry.from = static_cast<uint32_t>(range.first);
            entry.to = static_cast<uint32_t>(range.second);
            entry.target = static_cast<uint32_t>(c.offsetOf(handlers[i]));
            entry.excType = type.mn;
            entry.varName = publicName(stmt->catches[i].variable->name);
            c.exceptions.push_back(entry);
        }
    }
    if (stmt->finallyBody) {
        for (const auto& range : self->finallyRanges) {
            if (range.first == range.second) continue;
            ExceptionInfo entry;
            entry.from = static_cast<uint32_t>(range.first);
            entry.to = static_cast<uint32_t>(range.second);
            entry.target = static_cast<uint32_t>(c.offsetOf(finallyHandler));
            c.exceptions.push_back(entry);
        }
    }
    cur->controls.pop_back();
}

void AbcCompiler::withStatement(WithStatement* stmt) {
    CodeBuilder& c = code();
    expr(stmt->object.get());
    op(OP_dup);
    const uint32_t object = c.newTemp();
    c.setLocal(object);
    op(OP_pushwith);
    Control control;
    control.kind = Control::With;
    control.withRegister = object;
    cur->controls.push_back(control);
    statement(stmt->body.get());
    cur->controls.pop_back();
    if (c.reachable()) op(OP_popscope);
    c.freeTemp(object);
}

// Names in code

NameRef AbcCompiler::resolve(const std::string& name) {
    NameRef ref;
    auto own = cur->vars.find(name);
    if (own != cur->vars.end()) {
        ref.kind = own->second.slot ? NameRef::Slot : NameRef::Register;
        ref.index = own->second.index;
        ref.type = own->second.type;
        return ref;
    }
    for (Function* outer = cur->parent; outer; outer = outer->parent) {
        auto found = outer->vars.find(name);
        if (found != outer->vars.end()) {
            // Captured: in the outer function's activation, on the scope
            // chain.
            ref.kind = NameRef::Lexical;
            ref.mn = pool.qname(internalNs(outer->unit ? outer->unit->package : std::string()), name);
            ref.type = found->second.type;
            return ref;
        }
    }
    Function& top = root();
    Unit* unit = cur->unit;
    const bool dynamic = inWith();
    if (!dynamic && top.cls) {
        if (!top.isStatic) {
            for (ClassModel* c = top.cls; c; c = superOf(c)) {
                auto found = c->instance.find(name);
                if (found == c->instance.end() || (found->second.access == Access::Private && c != top.cls)) continue;
                ref.kind = cur == &top ? NameRef::ThisMember : NameRef::Lexical;
                ref.mn = pool.qname(found->second.ns, name);
                return ref;
            }
        }
        for (ClassModel* c = top.cls; c; c = superOf(c)) {
            auto found = c->statics.find(name);
            if (found == c->statics.end() || (found->second.access == Access::Private && c != top.cls)) continue;
            ref.kind = NameRef::Lexical;
            ref.mn = pool.qname(found->second.ns, name);
            return ref;
        }
    }
    if (!unit) {
        ref.mn = publicName(name);
        return ref;
    }
    if (!dynamic) {
        std::string package;
        if (const Definition* definition = findDefinition(unit, name, &package)) {
            ref.mn = definition->mn;
            ref.cls = definition->kind == Definition::Class ? definition->cls : nullptr;
            return ref;
        }
        auto imported = unit->imports.find(name);
        if (imported != unit->imports.end()) {
            ref.mn = pool.qname(packageNs(packageOf(imported->second)), name);
            return ref;
        }
        if (isTopLevelName(name)) {
            ref.mn = publicName(name);
            return ref;
        }
        for (const std::string& wildcard : unit->wildcards) {
            if (findPlayerClass(wildcard, name)) {
                ref.mn = pool.qname(packageNs(wildcard), name);
                return ref;
            }
        }
    }
    // Not known here: an inherited member, a stage instance, or a
    // definition in an imported package, found when it runs.
    ref.mn = pool.multiname(name, openSet(unit, top.cls));
    return ref;
}

void AbcCompiler::load(const NameRef& ref) {
    switch (ref.kind) {
        case NameRef::Register:
            code().getLocal(ref.index);
            break;
        case NameRef::Slot:
            code().getLocal(cur->activation);
            op(OP_getslot, ref.index);
            break;
        case NameRef::ThisMember:
            code().getLocal(0);
            op(OP_getproperty, ref.mn);
            break;
        default:
            op(OP_getlex, ref.mn);
            break;
    }
}

void AbcCompiler::coerce(const TypeRef& type) {
    switch (type.kind) {
        case TypeRef::Int: op(OP_convert_i); break;
        case TypeRef::Uint: op(OP_convert_u); break;
        case TypeRef::Number: op(OP_convert_d); break;
        case TypeRef::Boolean: op(OP_convert_b); break;
        case TypeRef::String: op(OP_coerce_s); break;
        case TypeRef::Other: op(OP_coerce, type.mn); break;
        default: break;
    }
}

void AbcCompiler::pushDefault(const TypeRef& type) {
    switch (type.kind) {
        case TypeRef::Int:
        case TypeRef::Uint: code().pushInt(0, pool); break;
        case TypeRef::Number: op(OP_pushnan); break;
        case TypeRef::Boolean: op(OP_pushfalse); break;
        case TypeRef::Any: op(OP_pushundefined); break;
        default: op(OP_pushnull); break;
    }
}

bool AbcCompiler::qualifiedPath(Expression* e, std::string& package, std::string& name) {
    // a.b.C, where a is not a variable or member but a package's start.
    std::vector<std::string> parts;
    Expression* at = e;
    while (at && at->kind == EK::Member) {
        auto* m = static_cast<MemberExpression*>(at);
        if (m->computed || !m->property) return false;
        parts.push_back(m->property->name);
        at = m->object.get();
    }
    if (!at || at->kind != EK::Identifier || parts.empty()) return false;
    const std::string& rootName = static_cast<Identifier*>(at)->name;
    const NameRef ref = resolve(rootName);
    if (ref.kind != NameRef::Global || file.pool.multinames[ref.mn].kind != MN_Multiname) return false;
    parts.push_back(rootName);
    std::reverse(parts.begin(), parts.end());
    name = parts.back();
    package.clear();
    for (size_t i = 0; i + 1 < parts.size(); ++i) package += (i ? "." : "") + parts[i];
    return isPackage(cur->unit, package);
}

ClassModel* AbcCompiler::classNamed(Expression* e) {
    if (!e) return nullptr;
    if (e->kind == EK::Identifier) {
        const std::string& name = static_cast<Identifier*>(e)->name;
        if (cur) {
            const NameRef ref = resolve(name);
            return ref.cls;
        }
        if (constantClass) {
            const Definition* definition = findDefinition(constantClass->unit, name);
            return definition && definition->kind == Definition::Class ? definition->cls : nullptr;
        }
        return nullptr;
    }
    std::string package, name;
    if (cur && qualifiedPath(e, package, name)) return sourceClass(package + "." + name);
    return nullptr;
}

uint32_t AbcCompiler::propertyName(MemberExpression* e, bool& runtime) {
    Unit* unit = cur->unit;
    ClassModel* context = root().cls;
    const uint32_t set = openSet(unit, context);
    runtime = false;
    if (e->computed) {
        runtime = true;
        // .@[name] is "@" + name.
        if (e->computedProperty && e->computedProperty->kind == EK::Binary) {
            auto* b = static_cast<BinaryExpression*>(e->computedProperty.get());
            if (b->op == OperatorType::Add && b->left->kind == EK::Literal &&
                static_cast<Literal*>(b->left.get())->stringValue == "@") {
                return pool.multinameL(set, true);
            }
        }
        return pool.multinameL(set);
    }
    const std::string& name = e->property->name;
    if (name == "*") return pool.multiname("", set);
    if (name == "@*") return pool.multiname("", set, true);
    if (name.size() > 1 && name[0] == '@') return pool.multiname(name.substr(1), set, true);
    // this.member and Class.static by their namespaces.
    if (e->object->kind == EK::This && context && !root().isStatic) {
        for (ClassModel* c = context; c; c = superOf(c)) {
            auto found = c->instance.find(name);
            if (found != c->instance.end() && (found->second.access != Access::Private || c == context)) {
                return pool.qname(found->second.ns, name);
            }
        }
    }
    if (ClassModel* owner = e->object->kind == EK::This ? nullptr : classNamed(e->object.get())) {
        for (ClassModel* c = owner; c; c = superOf(c)) {
            auto found = c->statics.find(name);
            if (found != c->statics.end() && (found->second.access != Access::Private || c == context)) {
                return pool.qname(found->second.ns, name);
            }
        }
    }
    if (e->object->kind == EK::Super && context) {
        for (ClassModel* c = superOf(context); c; c = superOf(c)) {
            auto found = c->instance.find(name);
            if (found != c->instance.end() && found->second.access != Access::Private) {
                return pool.qname(found->second.ns, name);
            }
        }
    }
    return pool.multiname(name, set);
}

// Expressions

void AbcCompiler::discard(Expression* e) {
    if (!e) return;
    switch (e->kind) {
        case EK::Assignment:
            assignment(static_cast<AssignmentExpression*>(e), false);
            return;
        case EK::Call:
            call(static_cast<CallExpression*>(e), false);
            return;
        case EK::Unary: {
            auto* u = static_cast<UnaryExpression*>(e);
            if (u->op == OperatorType::PostIncrement || u->op == OperatorType::PostDecrement ||
                u->op == OperatorType::PreIncrement || u->op == OperatorType::PreDecrement) {
                unary(u, false);
                return;
            }
            break;
        }
        case EK::Sequence:
            for (auto& part : static_cast<SequenceExpression*>(e)->expressions) discard(part.get());
            return;
        default:
            break;
    }
    expr(e);
    op(OP_pop);
}

void AbcCompiler::expr(Expression* e) {
    if (!e) {
        op(OP_pushundefined);
        return;
    }
    if (e->line > 0) currentLine = e->line;
    switch (e->kind) {
        case EK::Identifier: identifier(static_cast<Identifier*>(e)); break;
        case EK::Literal: literal(static_cast<Literal*>(e)); break;
        case EK::Binary: binary(static_cast<BinaryExpression*>(e)); break;
        case EK::Unary: unary(static_cast<UnaryExpression*>(e), true); break;
        case EK::Conditional: {
            auto* conditional = static_cast<ConditionalExpression*>(e);
            auto otherwise = code().newLabel();
            auto end = code().newLabel();
            condJump(conditional->test.get(), false, otherwise);
            expr(conditional->consequent.get());
            op(OP_coerce_a);
            code().jump(OP_jump, end);
            code().bind(otherwise);
            expr(conditional->alternate.get());
            op(OP_coerce_a);
            code().bind(end);
            break;
        }
        case EK::Assignment: assignment(static_cast<AssignmentExpression*>(e), true); break;
        case EK::Call: call(static_cast<CallExpression*>(e), true); break;
        case EK::New: construct(static_cast<NewExpression*>(e)); break;
        case EK::Member: member(static_cast<MemberExpression*>(e)); break;
        case EK::Function: {
            auto* function = static_cast<FunctionExpression*>(e);
            const std::string name = function->name ? function->name->name : std::string();
            bool usesSelf = false;
            if (!name.empty()) {
                Scan scan;
                scanStatement(function->body.get(), scan);
                usesSelf = scan.refs.count(name) || scan.innerFree.count(name);
                for (const auto& param : function->parameters) {
                    if (param->rest || param->name->name == name) usesSelf = false;
                }
            }
            op(OP_newfunction, compileClosure(function->parameters, function->returnType.get(), function->body.get(),
                                              name, usesSelf ? name : std::string()));
            break;
        }
        case EK::Array: arrayLiteral(static_cast<ArrayExpression*>(e)); break;
        case EK::Object: objectLiteral(static_cast<ObjectExpression*>(e)); break;
        case EK::This: code().getLocal(0); break;
        case EK::Sequence: {
            auto& parts = static_cast<SequenceExpression*>(e)->expressions;
            for (size_t i = 0; i + 1 < parts.size(); ++i) discard(parts[i].get());
            if (parts.empty()) op(OP_pushundefined);
            else expr(parts.back().get());
            break;
        }
        case EK::Super:
            error("super must be followed by a call or a property");
            op(OP_pushundefined);
            break;
        case EK::TypeApplication: {
            auto* application = static_cast<TypeApplication*>(e);
            std::string written;
            if (application->base->kind == EK::Identifier) written = static_cast<Identifier*>(application->base.get())->name;
            if (written != "Vector" || application->typeArguments.size() != 1) {
                error("only Vector.<T> takes a type");
                op(OP_pushundefined);
                break;
            }
            typeValue("Vector.<" + application->typeArguments[0] + ">");
            break;
        }
    }
}

void AbcCompiler::typeValue(const std::string& written) {
    // A type as a value: its class, Vector.<T> applied.
    const size_t open = written.find(".<");
    if (open != std::string::npos && written.back() == '>') {
        op(OP_getlex, pool.qname(packageNs("__AS3__.vec"), "Vector"));
        const std::string parameter = written.substr(open + 2, written.size() - open - 3);
        if (parameter == "*") op(OP_pushnull);
        else typeValue(parameter);
        op(OP_applytype, 1);
        return;
    }
    const TypeRef type = resolveType(written, cur->unit, true);
    if (type.mn) {
        op(OP_getlex, type.mn);
    } else {
        op(OP_getlex, pool.multiname(written, openSet(cur->unit, root().cls)));
    }
}

void AbcCompiler::identifier(Identifier* id) {
    const std::string& name = id->name;
    if (name.size() > 1 && name[0] == '@') {
        // An attribute, in an E4X filter.
        const uint32_t set = openSet(cur->unit, root().cls);
        const uint32_t mn = name == "@*" ? pool.multiname("", set, true) : pool.multiname(name.substr(1), set, true);
        op(OP_findpropstrict, mn);
        op(OP_getproperty, mn);
        return;
    }
    const NameRef ref = resolve(name);
    if (ref.kind == NameRef::Global && (name == "undefined" || name == "NaN" || name == "Infinity")) {
        if (name == "undefined") op(OP_pushundefined);
        else if (name == "NaN") op(OP_pushnan);
        else op(OP_pushdouble, pool.number(std::numeric_limits<double>::infinity()));
        return;
    }
    if (name == "arguments" && ref.kind == NameRef::Global) {
        error("arguments is not available here");
    }
    load(ref);
}

void AbcCompiler::literal(Literal* lit) {
    switch (lit->kind) {
        case Literal::LiteralKind::Null: op(OP_pushnull); break;
        case Literal::LiteralKind::Undefined: op(OP_pushundefined); break;
        case Literal::LiteralKind::Boolean: op(lit->boolValue ? OP_pushtrue : OP_pushfalse); break;
        case Literal::LiteralKind::Numeric: code().pushDouble(lit->numberValue, pool); break;
        case Literal::LiteralKind::String: pushString(lit->stringValue); break;
        case Literal::LiteralKind::RegExp:
            op(OP_getlex, publicName("RegExp"));
            pushString(lit->stringValue);
            pushString(lit->flags);
            op(OP_construct, 2);
            break;
        default:
            error("this literal is not supported");
            op(OP_pushundefined);
            break;
    }
}

bool AbcCompiler::binaryOp(OperatorType o) {
    switch (o) {
        case OperatorType::Add: op(OP_add); return true;
        case OperatorType::Subtract: op(OP_subtract); return true;
        case OperatorType::Multiply: op(OP_multiply); return true;
        case OperatorType::Divide: op(OP_divide); return true;
        case OperatorType::Remainder: op(OP_modulo); return true;
        case OperatorType::ShiftLeft: op(OP_lshift); return true;
        case OperatorType::ShiftRight: op(OP_rshift); return true;
        case OperatorType::ShiftRightUnsigned: op(OP_urshift); return true;
        case OperatorType::BitwiseAnd: op(OP_bitand); return true;
        case OperatorType::BitwiseOr: op(OP_bitor); return true;
        case OperatorType::BitwiseXor: op(OP_bitxor); return true;
        case OperatorType::Lt: op(OP_lessthan); return true;
        case OperatorType::Gt: op(OP_greaterthan); return true;
        case OperatorType::Le: op(OP_lessequals); return true;
        case OperatorType::Ge: op(OP_greaterequals); return true;
        case OperatorType::Equals: op(OP_equals); return true;
        case OperatorType::NotEquals: op(OP_equals); op(OP_not); return true;
        case OperatorType::StrictEquals: op(OP_strictequals); return true;
        case OperatorType::StrictNotEquals: op(OP_strictequals); op(OP_not); return true;
        case OperatorType::Instanceof: op(OP_instanceof); return true;
        case OperatorType::In: op(OP_in); return true;
        case OperatorType::Is: op(OP_istypelate); return true;
        case OperatorType::As: op(OP_astypelate); return true;
        case OperatorType::LogicalXor: {
            // Both operands are booleans by now.
            op(OP_equals);
            op(OP_not);
            return true;
        }
        default:
            return false;
    }
}

void AbcCompiler::binary(BinaryExpression* e) {
    const OperatorType o = e->op;
    if (o == OperatorType::LogicalAnd || o == OperatorType::LogicalOr || o == OperatorType::NullCoalescing) {
        auto end = code().newLabel();
        expr(e->left.get());
        op(OP_coerce_a);
        op(OP_dup);
        if (o == OperatorType::NullCoalescing) {
            op(OP_pushnull);
            code().jump(OP_ifne, end);
        } else {
            code().jump(o == OperatorType::LogicalAnd ? OP_iffalse : OP_iftrue, end);
        }
        op(OP_pop);
        expr(e->right.get());
        op(OP_coerce_a);
        code().bind(end);
        return;
    }
    if (o == OperatorType::Power) {
        error("** is not ActionScript 3");
        op(OP_pushundefined);
        return;
    }
    // Down a long chain (a + b + c...) without recursing for each link.
    std::vector<BinaryExpression*> chain;
    Expression* at = e;
    while (at->kind == EK::Binary) {
        auto* b = static_cast<BinaryExpression*>(at);
        if (b->op == OperatorType::LogicalAnd || b->op == OperatorType::LogicalOr ||
            b->op == OperatorType::NullCoalescing || b->op == OperatorType::Power) {
            break;
        }
        chain.push_back(b);
        at = b->left.get();
    }
    expr(at);
    for (size_t i = chain.size(); i-- > 0;) {
        BinaryExpression* b = chain[i];
        if (b->op == OperatorType::LogicalXor) op(OP_convert_b);
        if ((b->op == OperatorType::Is || b->op == OperatorType::As) && b->right->kind == EK::Identifier) {
            typeValue(static_cast<Identifier*>(b->right.get())->name);
        } else {
            expr(b->right.get());
        }
        if (b->op == OperatorType::LogicalXor) op(OP_convert_b);
        if (!binaryOp(b->op)) {
            error("this operator is not supported");
        }
    }
}

void AbcCompiler::condJump(Expression* e, bool jumpIf, CodeBuilder::Label target) {
    if (e && e->kind == EK::Unary && static_cast<UnaryExpression*>(e)->op == OperatorType::LogicalNot) {
        condJump(static_cast<UnaryExpression*>(e)->operand.get(), !jumpIf, target);
        return;
    }
    if (e && e->kind == EK::Literal && static_cast<Literal*>(e)->kind == Literal::LiteralKind::Boolean) {
        if (static_cast<Literal*>(e)->boolValue == jumpIf) code().jump(OP_jump, target);
        return;
    }
    if (e && e->kind == EK::Binary) {
        auto* b = static_cast<BinaryExpression*>(e);
        if (b->op == OperatorType::LogicalAnd || b->op == OperatorType::LogicalOr) {
            const bool isAnd = b->op == OperatorType::LogicalAnd;
            if (isAnd != jumpIf) {
                // and, jumping when false (or or, when true): either side.
                condJump(b->left.get(), jumpIf, target);
                condJump(b->right.get(), jumpIf, target);
            } else {
                auto skip = code().newLabel();
                condJump(b->left.get(), !jumpIf, skip);
                condJump(b->right.get(), jumpIf, target);
                code().bind(skip);
            }
            return;
        }
        uint8_t yes = 0, no = 0;
        switch (b->op) {
            case OperatorType::Lt: yes = OP_iflt; no = OP_ifnlt; break;
            case OperatorType::Le: yes = OP_ifle; no = OP_ifnle; break;
            case OperatorType::Gt: yes = OP_ifgt; no = OP_ifngt; break;
            case OperatorType::Ge: yes = OP_ifge; no = OP_ifnge; break;
            case OperatorType::Equals: yes = OP_ifeq; no = OP_ifne; break;
            case OperatorType::NotEquals: yes = OP_ifne; no = OP_ifeq; break;
            case OperatorType::StrictEquals: yes = OP_ifstricteq; no = OP_ifstrictne; break;
            case OperatorType::StrictNotEquals: yes = OP_ifstrictne; no = OP_ifstricteq; break;
            default: break;
        }
        if (yes) {
            expr(b->left.get());
            expr(b->right.get());
            code().jump(jumpIf ? yes : no, target);
            return;
        }
    }
    expr(e);
    code().jump(jumpIf ? OP_iftrue : OP_iffalse, target);
}

void AbcCompiler::unary(UnaryExpression* e, bool keep) {
    switch (e->op) {
        case OperatorType::PreIncrement: incDec(e->operand.get(), true, false, keep); return;
        case OperatorType::PreDecrement: incDec(e->operand.get(), false, false, keep); return;
        case OperatorType::PostIncrement: incDec(e->operand.get(), true, true, keep); return;
        case OperatorType::PostDecrement: incDec(e->operand.get(), false, true, keep); return;
        default: break;
    }
    Expression* operand = e->operand.get();
    switch (e->op) {
        case OperatorType::Negative: {
            Constant value;
            if (constantValue(operand, value) && value.kind == Constant::Number) {
                code().pushDouble(-value.number, pool);
            } else {
                expr(operand);
                op(OP_negate);
            }
            break;
        }
        case OperatorType::Positive: expr(operand); op(OP_convert_d); break;
        case OperatorType::BitwiseNot: expr(operand); op(OP_bitnot); break;
        case OperatorType::LogicalNot: expr(operand); op(OP_not); break;
        case OperatorType::NonNull: expr(operand); break;
        case OperatorType::Void: discard(operand); op(OP_pushundefined); break;
        case OperatorType::Typeof:
            if (operand && operand->kind == EK::Identifier) {
                // A name not defined is "undefined", not an error.
                const NameRef ref = resolve(static_cast<Identifier*>(operand)->name);
                if (ref.kind == NameRef::Global || ref.kind == NameRef::Lexical) {
                    op(OP_findproperty, ref.mn);
                    op(OP_getproperty, ref.mn);
                } else {
                    load(ref);
                }
            } else {
                expr(operand);
            }
            op(OP_typeof);
            break;
        case OperatorType::Delete:
            if (operand && operand->kind == EK::Member) {
                auto* m = static_cast<MemberExpression*>(operand);
                bool runtime = false;
                expr(m->object.get());
                const uint32_t mn = propertyName(m, runtime);
                if (runtime) expr(m->computedProperty.get());
                op(OP_deleteproperty, mn);
            } else if (operand && operand->kind == EK::Identifier) {
                const NameRef ref = resolve(static_cast<Identifier*>(operand)->name);
                if (ref.kind == NameRef::Register || ref.kind == NameRef::Slot) {
                    op(OP_pushfalse);
                } else if (ref.kind == NameRef::ThisMember) {
                    code().getLocal(0);
                    op(OP_deleteproperty, ref.mn);
                } else {
                    op(OP_findproperty, ref.mn);
                    op(OP_deleteproperty, ref.mn);
                }
            } else {
                discard(operand);
                op(OP_pushtrue);
            }
            break;
        default:
            error("this operator is not supported");
            op(OP_pushundefined);
            break;
    }
    if (!keep) op(OP_pop);
}

bool AbcCompiler::targetOf(Expression* e, Target& out, bool strict) {
    if (!e) return false;
    if (e->kind == EK::Identifier) {
        const std::string& name = static_cast<Identifier*>(e)->name;
        const NameRef ref = resolve(name);
        switch (ref.kind) {
            case NameRef::Register:
                out.kind = Target::Register;
                out.index = ref.index;
                out.type = ref.type;
                return true;
            case NameRef::Slot:
                out.kind = Target::Slot;
                out.index = ref.index;
                out.type = ref.type;
                out.pushBase = [this]() { code().getLocal(cur->activation); };
                out.baseCount = 1;
                return true;
            case NameRef::ThisMember:
                out.kind = Target::Property;
                out.mn = ref.mn;
                out.pushBase = [this]() { code().getLocal(0); };
                out.baseCount = 1;
                return true;
            default: {
                out.kind = Target::Property;
                out.mn = ref.mn;
                const uint32_t mn = ref.mn;
                const uint8_t find = strict ? OP_findpropstrict : OP_findproperty;
                out.pushBase = [this, mn, find]() { op(find, mn); };
                out.baseCount = 1;
                return true;
            }
        }
    }
    if (e->kind == EK::Member) {
        auto* m = static_cast<MemberExpression*>(e);
        bool runtime = false;
        if (m->object->kind == EK::Super) {
            out.kind = Target::Super;
            out.mn = propertyName(m, runtime);
            out.pushBase = [this]() { code().getLocal(0); };
            out.baseCount = 1;
            if (runtime) {
                Expression* key = m->computedProperty.get();
                out.pushBase = [this, key]() { code().getLocal(0); expr(key); };
                out.baseCount = 2;
            }
            return true;
        }
        out.kind = Target::Property;
        out.mn = propertyName(m, runtime);
        Expression* object = m->object.get();
        std::string package, name;
        if (!runtime && qualifiedPath(object, package, name)) {
            const uint32_t qualified = pool.qname(packageNs(package), name);
            out.pushBase = [this, qualified]() { op(OP_getlex, qualified); };
        } else if (runtime) {
            Expression* key = m->computedProperty.get();
            out.pushBase = [this, object, key]() { expr(object); expr(key); };
            out.baseCount = 2;
            return true;
        } else {
            out.pushBase = [this, object]() { expr(object); };
        }
        out.baseCount = 1;
        return true;
    }
    return false;
}

void AbcCompiler::loadTarget(const Target& target) {
    switch (target.kind) {
        case Target::Register: code().getLocal(target.index); break;
        case Target::Slot: op(OP_getslot, target.index); break;
        case Target::Property: op(OP_getproperty, target.mn); break;
        case Target::Super: op(OP_getsuper, target.mn); break;
    }
}

void AbcCompiler::storeTarget(const Target& target) {
    switch (target.kind) {
        case Target::Register: coerce(target.type); code().setLocal(target.index); break;
        case Target::Slot: op(OP_setslot, target.index); break;
        case Target::Property: op(OP_setproperty, target.mn); break;
        case Target::Super: op(OP_setsuper, target.mn); break;
    }
}

// The base of a target twice over, for reading and then writing it: a
// single value duplicated, a pair kept in registers.
static void pushBaseTwice(CodeBuilder& c, const Target& target, std::vector<uint32_t>& temps) {
    if (target.baseCount == 0) return;
    target.pushBase();
    if (target.baseCount == 1) {
        c.op(OP_dup);
        return;
    }
    const uint32_t key = c.newTemp();
    const uint32_t object = c.newTemp();
    c.setLocal(key);
    c.setLocal(object);
    c.getLocal(object);
    c.getLocal(key);
    c.getLocal(object);
    c.getLocal(key);
    temps.push_back(key);
    temps.push_back(object);
}

void AbcCompiler::storeFromStack(Expression* left) {
    Target target;
    if (!targetOf(left, target, false)) {
        error("this cannot be assigned to");
        op(OP_pop);
        return;
    }
    if (target.baseCount == 0) {
        storeTarget(target);
        return;
    }
    if (target.kind == Target::Slot) {
        code().getLocal(cur->activation);
        op(OP_swap);
        storeTarget(target);
        return;
    }
    const uint32_t value = code().newTemp();
    code().setLocal(value);
    target.pushBase();
    code().getLocal(value);
    storeTarget(target);
    code().freeTemp(value);
}

void AbcCompiler::assignment(AssignmentExpression* e, bool keep) {
    Target target;
    if (!targetOf(e->left.get(), target, e->op != OperatorType::Assign)) {
        error("this cannot be assigned to");
        expr(e->right.get());
        if (!keep) op(OP_pop);
        return;
    }
    CodeBuilder& c = code();
    std::vector<uint32_t> temps;
    if (e->op == OperatorType::Assign) {
        if (target.baseCount) target.pushBase();
        expr(e->right.get());
    } else {
        pushBaseTwice(c, target, temps);
        loadTarget(target);
        if (e->op == OperatorType::LogicalAnd || e->op == OperatorType::LogicalOr ||
            e->op == OperatorType::NullCoalescing) {
            auto end = c.newLabel();
            op(OP_coerce_a);
            op(OP_dup);
            if (e->op == OperatorType::NullCoalescing) {
                op(OP_pushnull);
                c.jump(OP_ifne, end);
            } else {
                c.jump(e->op == OperatorType::LogicalAnd ? OP_iffalse : OP_iftrue, end);
            }
            op(OP_pop);
            expr(e->right.get());
            op(OP_coerce_a);
            c.bind(end);
        } else {
            expr(e->right.get());
            if (!binaryOp(e->op)) error("this operator is not supported");
        }
    }
    if (target.kind != Target::Register) coerce(target.type);
    uint32_t kept = 0;
    if (keep) {
        if (target.kind == Target::Register) {
            coerce(target.type);
            op(OP_dup);
        } else {
            op(OP_dup);
            kept = c.newTemp();
            c.setLocal(kept);
        }
    }
    storeTarget(target);
    if (keep && target.kind != Target::Register) {
        c.getLocal(kept);
        c.freeTemp(kept);
    }
    for (uint32_t temp : temps) c.freeTemp(temp);
}

void AbcCompiler::incDec(Expression* operand, bool increment, bool postfix, bool keep) {
    Target target;
    if (!targetOf(operand, target, true)) {
        error("this cannot be incremented");
        op(OP_pushundefined);
        if (!keep) op(OP_pop);
        return;
    }
    CodeBuilder& c = code();
    if (target.kind == Target::Register && !keep &&
        (target.type.kind == TypeRef::Int || target.type.kind == TypeRef::Any || target.type.kind == TypeRef::Number)) {
        if (target.type.kind == TypeRef::Int) op(increment ? OP_inclocal_i : OP_declocal_i, target.index);
        else op(increment ? OP_inclocal : OP_declocal, target.index);
        return;
    }
    std::vector<uint32_t> temps;
    pushBaseTwice(c, target, temps);
    loadTarget(target);
    const bool asInt = target.type.kind == TypeRef::Int;
    op(asInt ? OP_convert_i : OP_convert_d);
    uint32_t kept = 0;
    const bool keepTemp = keep && target.kind != Target::Register;
    if (keep && postfix) {
        if (keepTemp) {
            op(OP_dup);
            kept = c.newTemp();
            c.setLocal(kept);
        } else {
            op(OP_dup);
        }
    }
    op(asInt ? (increment ? OP_increment_i : OP_decrement_i) : (increment ? OP_increment : OP_decrement));
    if (target.kind != Target::Register) coerce(target.type);
    if (keep && !postfix) {
        if (target.kind == Target::Register) coerce(target.type);
        op(OP_dup);
        if (keepTemp) {
            kept = c.newTemp();
            c.setLocal(kept);
        }
    }
    storeTarget(target);
    if (keepTemp) {
        c.getLocal(kept);
        c.freeTemp(kept);
    }
    for (uint32_t temp : temps) c.freeTemp(temp);
}

int AbcCompiler::arguments(const ExpressionList& args) {
    for (const auto& arg : args) expr(arg.get());
    return static_cast<int>(args.size());
}

void AbcCompiler::call(CallExpression* e, bool keep) {
    Expression* callee = e->callee.get();
    const auto result = [this, keep](bool pushed) {
        if (pushed && !keep) op(OP_pop);
        if (!pushed && keep) op(OP_pushundefined);
    };
    if (callee->kind == EK::Super) {
        if (!cur->isConstructor) error("super() outside a constructor");
        code().getLocal(0);
        op(OP_constructsuper, static_cast<uint32_t>(arguments(e->arguments)));
        result(false);
        return;
    }
    if (callee->kind == EK::Identifier) {
        const std::string& name = static_cast<Identifier*>(callee)->name;
        if (Expression* predicate = filterPredicate(e)) {
            xmlFilter(e, predicate);
            result(true);
            return;
        }
        if (name == "$xmlPart" && e->arguments.size() == 2) {
            expr(e->arguments[0].get());
            Constant kind;
            constantValue(e->arguments[1].get(), kind);
            if (kind.number == 1) {
                op(OP_esc_xelem);
            } else if (kind.number == 2) {
                op(OP_esc_xattr);
                const uint32_t text = code().newTemp();
                code().setLocal(text);
                pushString("\"");
                code().getLocal(text);
                op(OP_add);
                pushString("\"");
                op(OP_add);
                code().freeTemp(text);
            } else {
                op(OP_convert_s);
            }
            result(true);
            return;
        }
        const NameRef ref = resolve(name);
        const uint32_t argc = static_cast<uint32_t>(e->arguments.size());
        switch (ref.kind) {
            case NameRef::Register:
            case NameRef::Slot:
                load(ref);
                op(OP_pushnull);
                arguments(e->arguments);
                op(OP_call, argc);
                result(true);
                return;
            case NameRef::ThisMember:
                code().getLocal(0);
                break;
            default:
                op(OP_findpropstrict, ref.mn);
                break;
        }
        arguments(e->arguments);
        if (keep) op(OP_callproperty, ref.mn, argc);
        else op(OP_callpropvoid, ref.mn, argc);
        return;
    }
    if (callee->kind == EK::Member) {
        auto* m = static_cast<MemberExpression*>(callee);
        const uint32_t argc = static_cast<uint32_t>(e->arguments.size());
        bool runtime = false;
        if (m->object->kind == EK::Super) {
            const uint32_t mn = propertyName(m, runtime);
            code().getLocal(0);
            if (runtime) expr(m->computedProperty.get());
            arguments(e->arguments);
            if (keep) op(OP_callsuper, mn, argc);
            else op(OP_callsupervoid, mn, argc);
            return;
        }
        std::string package, name;
        if (qualifiedPath(callee, package, name)) {
            const uint32_t mn = pool.qname(packageNs(package), name);
            op(OP_findpropstrict, mn);
            arguments(e->arguments);
            if (keep) op(OP_callproperty, mn, argc);
            else op(OP_callpropvoid, mn, argc);
            return;
        }
        const uint32_t mn = propertyName(m, runtime);
        if (!runtime && qualifiedPath(m->object.get(), package, name)) {
            op(OP_getlex, pool.qname(packageNs(package), name));
        } else {
            expr(m->object.get());
        }
        if (runtime) expr(m->computedProperty.get());
        arguments(e->arguments);
        if (keep) op(OP_callproperty, mn, argc);
        else op(OP_callpropvoid, mn, argc);
        return;
    }
    expr(callee);
    op(OP_pushnull);
    op(OP_call, static_cast<uint32_t>(arguments(e->arguments)));
    result(true);
}

void AbcCompiler::construct(NewExpression* e) {
    Expression* callee = e->callee.get();
    const uint32_t argc = static_cast<uint32_t>(e->arguments.size());
    if (callee->kind == EK::Identifier) {
        const NameRef ref = resolve(static_cast<Identifier*>(callee)->name);
        if (ref.kind == NameRef::Register || ref.kind == NameRef::Slot) {
            load(ref);
            arguments(e->arguments);
            op(OP_construct, argc);
            return;
        }
        if (ref.kind == NameRef::ThisMember) code().getLocal(0);
        else op(OP_findpropstrict, ref.mn);
        arguments(e->arguments);
        op(OP_constructprop, ref.mn, argc);
        return;
    }
    if (callee->kind == EK::Member) {
        auto* m = static_cast<MemberExpression*>(callee);
        std::string package, name;
        if (qualifiedPath(callee, package, name)) {
            const uint32_t mn = pool.qname(packageNs(package), name);
            op(OP_findpropstrict, mn);
            arguments(e->arguments);
            op(OP_constructprop, mn, argc);
            return;
        }
        if (m->object->kind != EK::Super) {
            bool runtime = false;
            const uint32_t mn = propertyName(m, runtime);
            expr(m->object.get());
            if (runtime) expr(m->computedProperty.get());
            arguments(e->arguments);
            op(OP_constructprop, mn, argc);
            return;
        }
    }
    expr(callee);
    arguments(e->arguments);
    op(OP_construct, argc);
}

void AbcCompiler::member(MemberExpression* e) {
    std::string package, name;
    if (qualifiedPath(e, package, name)) {
        op(OP_getlex, pool.qname(packageNs(package), name));
        return;
    }
    bool runtime = false;
    const uint32_t mn = propertyName(e, runtime);
    if (e->object->kind == EK::Super) {
        code().getLocal(0);
        if (runtime) expr(e->computedProperty.get());
        op(OP_getsuper, mn);
        return;
    }
    expr(e->object.get());
    if (runtime) expr(e->computedProperty.get());
    op(OP_getproperty, mn);
}

void AbcCompiler::arrayLiteral(ArrayExpression* e) {
    if (!e->vectorType.empty()) {
        // new <T>[a, b]: a vector of the length, then each element.
        typeValue("Vector.<" + e->vectorType + ">");
        code().pushInt(static_cast<int32_t>(e->elements.size()), pool);
        op(OP_construct, 1);
        const uint32_t index = pool.multinameL(openSet(cur->unit, root().cls));
        for (size_t i = 0; i < e->elements.size(); ++i) {
            op(OP_dup);
            code().pushInt(static_cast<int32_t>(i), pool);
            expr(e->elements[i].get());
            op(OP_setproperty, index);
        }
        return;
    }
    for (const auto& element : e->elements) {
        if (element) expr(element.get());
        else op(OP_pushundefined);
    }
    op(OP_newarray, static_cast<uint32_t>(e->elements.size()));
}

void AbcCompiler::objectLiteral(ObjectExpression* e) {
    for (const auto& property : e->properties) {
        Expression* key = property.first.get();
        if (key && key->kind == EK::Identifier) {
            pushString(static_cast<Identifier*>(key)->name);
        } else if (key && key->kind == EK::Literal && static_cast<Literal*>(key)->kind == Literal::LiteralKind::Numeric) {
            pushString(numberText(static_cast<Literal*>(key)->numberValue));
        } else if (key && key->kind == EK::Literal) {
            pushString(static_cast<Literal*>(key)->stringValue);
        } else {
            expr(key);
            op(OP_convert_s);
        }
        expr(property.second.get());
    }
    op(OP_newobject, static_cast<uint32_t>(e->properties.size()));
}

void AbcCompiler::xmlFilter(CallExpression* e, Expression* predicate) {
    // list.(predicate): each item tested with its names found on it first,
    // those it is true of kept in a new list.
    CodeBuilder& c = code();
    expr(e->arguments[0].get());
    op(OP_checkfilter);
    op(OP_coerce_a);
    const uint32_t list = c.newTemp();
    c.setLocal(list);
    const uint32_t index = c.newTemp();
    c.pushInt(0, pool);
    c.setLocal(index);
    const uint32_t xmlList = publicName("XMLList");
    op(OP_findpropstrict, xmlList);
    op(OP_constructprop, xmlList, 0);
    op(OP_coerce_a);
    const uint32_t result = c.newTemp();
    c.setLocal(result);
    const uint32_t count = c.newTemp();
    c.pushInt(0, pool);
    c.setLocal(count);
    const uint32_t item = c.newTemp();
    auto body = c.newLabel();
    auto next = c.newLabel();
    auto test = c.newLabel();
    c.jump(OP_jump, test);
    c.bind(body, true);
    c.getLocal(list);
    c.getLocal(index);
    op(OP_nextvalue);
    op(OP_dup);
    c.setLocal(item);
    op(OP_pushwith);
    Control control;
    control.kind = Control::With;
    control.withRegister = item;
    cur->controls.push_back(control);
    expr(predicate);
    cur->controls.pop_back();
    op(OP_popscope);
    c.jump(OP_iffalse, next);
    c.getLocal(result);
    c.getLocal(count);
    c.getLocal(item);
    op(OP_setproperty, pool.multinameL(openSet(cur->unit, root().cls)));
    op(OP_inclocal_i, count);
    c.bind(next);
    c.bind(test);
    op(OP_hasnext2, list, index);
    c.jump(OP_iftrue, body);
    c.getLocal(result);
    c.freeTemp(item);
    c.freeTemp(count);
    c.freeTemp(result);
    c.freeTemp(index);
    c.freeTemp(list);
}

} // namespace

Output compile(const std::vector<Source>& sources, const std::vector<Timeline>& timelines, const Options& options) {
    AbcCompiler compiler(options);
    return compiler.run(sources, timelines);
}

} // namespace abc
} // namespace as3
