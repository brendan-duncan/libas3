#include "abc_model.h"
#include "abc_player.h"
#include "../parser/parser.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace as3 {
namespace abc {

using EK = Expression::ExpressionKind;

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

// Names

ClassModel* Model::sourceClass(const std::string& qualified) const {
    auto found = globals.find(qualified);
    return found != globals.end() && found->second.kind == Definition::Class ? found->second.cls : nullptr;
}

const Definition* Model::findDefinition(Unit* unit, const std::string& name, std::string* package) const {
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

bool Model::isPackage(Unit* unit, const std::string& name) const {
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

TypeName Model::resolveType(const std::string& written, Unit* unit, bool quiet) {
    TypeName type;
    if (written.empty() || written == "*") return type;
    if (written == "void") {
        type.kind = TypeName::Void;
        type.qualified = "void";
        return type;
    }
    static const std::map<std::string, TypeName::Kind> builtins = {
        {"int", TypeName::Int}, {"uint", TypeName::Uint}, {"Number", TypeName::Number},
        {"Boolean", TypeName::Boolean}, {"String", TypeName::String}, {"Object", TypeName::Object},
    };
    // Vector.<T>
    const size_t open = written.find(".<");
    if (open != std::string::npos && written.back() == '>') {
        const std::string base = written.substr(0, open);
        const std::string parameter = written.substr(open + 2, written.size() - open - 3);
        if (base == "Vector" || base == "__AS3__.vec.Vector") {
            type.kind = TypeName::Vector;
            type.qualified = "__AS3__.vec.Vector";
            type.element = std::make_shared<TypeName>(resolveType(parameter, unit, quiet));
            return type;
        }
        if (!quiet) warning("unknown generic type " + written + "; taken as *");
        return type;
    }
    if (written.find('.') != std::string::npos) {
        type.kind = TypeName::Class;
        type.qualified = written;
        auto found = globals.find(written);
        if (found != globals.end()) type.definition = &found->second;
        return type;
    }
    std::string package;
    if (const Definition* definition = findDefinition(unit, written, &package)) {
        if (definition->kind == Definition::Class) {
            type.kind = TypeName::Class;
            type.qualified = definition->cls->qualified();
            type.definition = definition;
            return type;
        }
    }
    if (!package.empty() || unit->imports.count(written)) {
        // Imported by name: its package is known even without its
        // definition.
        type.kind = TypeName::Class;
        type.qualified = unit->imports[written];
        return type;
    }
    auto builtin = builtins.find(written);
    if (builtin != builtins.end()) {
        type.kind = builtin->second;
        type.qualified = written;
        return type;
    }
    if (findPlayerClass("", written)) {
        type.kind = TypeName::Class;
        type.qualified = written;
        return type;
    }
    for (const std::string& wildcard : unit->wildcards) {
        if (findPlayerClass(wildcard, written)) {
            type.kind = TypeName::Class;
            type.qualified = wildcard + "." + written;
            return type;
        }
    }
    if (!quiet) warning("type " + written + " is not known (import it by name); taken as *");
    return type;
}

std::vector<std::string> Model::superChain(const ClassModel* cls) const {
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

// Building the model

void Model::parseSources(const std::vector<Source>& sources) {
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

void Model::addMember(ClassModel& cls, const std::string& name, Member member) {
    auto& table = member.isStatic ? cls.statics : cls.instance;
    auto found = table.find(name);
    if (found != table.end()) {
        // A getter and its setter are one member.
        if (found->second.kind == Member::Accessor && member.kind == Member::Accessor) {
            found->second.hasGetter = found->second.hasGetter || member.hasGetter;
            found->second.hasSetter = found->second.hasSetter || member.hasSetter;
            if (found->second.type.empty()) found->second.type = member.type;
            return;
        }
        error("'" + name + "' is defined twice in " + cls.qualified());
        return;
    }
    member.name = name;
    table[name] = member;
}

void Model::addClass(ClassDefinition* definition, Unit* unit, bool outside) {
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
    for (auto& node : definition->members) {
        if (auto* function = dynamic_cast<FunctionDefinition*>(node.get())) {
            const bool isStatic = hasAttribute(function->attributes, AttributeType::Static);
            if (!isStatic && function->name->name == cls.name && !function->isGetter && !function->isSetter) continue;
            Member member;
            member.isStatic = isStatic;
            member.access = cls.isInterface ? Access::Public : accessOf(function->attributes, Access::Internal);
            member.kind = function->isGetter || function->isSetter ? Member::Accessor : Member::Method;
            member.hasGetter = function->isGetter;
            member.hasSetter = function->isSetter;
            if (function->isGetter && function->returnType) member.type = function->returnType->name;
            if (function->isSetter && !function->parameters.empty() && function->parameters[0]->type) {
                member.type = function->parameters[0]->type->name;
            }
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
    entry.name = cls.name;
    entry.package = cls.package;
    entry.isPublic = cls.isPublic;
    entry.outside = outside;
    entry.file = unit->file;
    if (outside) {
        unit->file->privateDefinitions[cls.name] = entry;
    } else {
        if (globals.count(cls.qualified())) error(cls.qualified() + " is defined twice", cls.line);
        globals[cls.qualified()] = entry;
    }
    unit->file->classes.push_back(&cls);
    classes.push_back(std::move(model));
}

void Model::collectDefinitions(SourceFile& source) {
    currentFile = source.fileName;
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
        const auto define = [&](const std::string& name, Definition entry) {
            entry.name = name;
            entry.package = unit->outside ? std::string() : unit->package;
            entry.outside = unit->outside;
            entry.file = &source;
            if (unit->outside) {
                source.privateDefinitions[name] = entry;
            } else {
                globals[unit->package.empty() ? name : unit->package + "." + name] = entry;
            }
        };
        for (AstNode* node : unit->members) {
            if (auto* definition = dynamic_cast<ClassDefinition*>(node)) {
                addClass(definition, unit.get(), unit->outside);
            } else if (auto* function = dynamic_cast<FunctionDefinition*>(node)) {
                Definition entry;
                entry.kind = Definition::Function;
                entry.isPublic = hasAttribute(function->attributes, AttributeType::Public);
                define(function->name->name, entry);
                source.functions.push_back(function);
            } else if (auto* variables = dynamic_cast<VariableDefinition*>(node)) {
                const bool isPublic = hasAttribute(variables->attributes, AttributeType::Public);
                for (auto& decl : variables->declarations) {
                    Definition entry;
                    entry.kind = Definition::Variable;
                    entry.isPublic = isPublic;
                    entry.type = decl->type ? decl->type->name : std::string();
                    entry.isConst = variables->isConst;
                    entry.init = decl->init.get();
                    define(decl->id->name, entry);
                }
            }
        }
    }
}

void Model::buildTimeline(const Timeline& timeline) {
    auto source = std::make_unique<SourceFile>();
    source->fileName = timeline.className;
    source->timeline = true;
    source->program = std::make_unique<Program>();
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
    entry.name = cls.name;
    entry.package = cls.package;
    entry.isPublic = true;
    entry.file = source.get();
    if (globals.count(timeline.className)) error(timeline.className + " is defined twice");
    globals[timeline.className] = entry;
    source->classes.push_back(&cls);
    source->units.push_back(std::move(unit));
    classes.push_back(std::move(model));
    files.push_back(std::move(source));
}

void Model::resolveClasses() {
    for (auto& model : classes) {
        ClassModel& cls = *model;
        currentFile = cls.unit->fileName;
        currentLine = cls.line;
        const auto addInterfaces = [&]() {
            for (auto& face : cls.ast->interfaces) {
                const TypeName type = resolveType(face->name, cls.unit, true);
                if (type.kind == TypeName::Any) {
                    error("interface " + face->name + " is not known; import it by name");
                    continue;
                }
                cls.interfaceNames.push_back(type.qualified);
                cls.interfaceModels.push_back(type.definition ? type.definition->cls : nullptr);
            }
        };
        if (cls.isInterface) {
            addInterfaces();
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
            addInterfaces();
        }
        // Outside-package classes are found in their file.
        ClassModel* base = sourceClass(cls.superName);
        if (!base && cls.unit->file) {
            auto found = cls.unit->file->privateDefinitions.find(cls.superName);
            if (found != cls.unit->file->privateDefinitions.end() && found->second.kind == Definition::Class) {
                base = found->second.cls;
            }
        }
        if (base) cls.superName = base->qualified();
        cls.superClass = base;
        if (base == &cls) {
            error(cls.qualified() + " extends itself");
            cls.superName = "Object";
            cls.superClass = nullptr;
        }
    }
}

} // namespace abc
} // namespace as3
