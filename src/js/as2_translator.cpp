#include "as2.h"
#include "js_writer.h"
#include "../parser/parser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <sstream>

// ActionScript 1 and 2 to JavaScript. Names are resolved as AVM1 resolves
// them: a function's parameters and variables are JavaScript's own locals;
// other names go along the scope chain at run time ($s.get("score"), with
// blocks' objects, the timeline, _global), a class's members by their bare
// names are this.name or Class.name, and the global functions that are
// actions of their own (play, gotoAndStop, setProperty...) act on the
// scope's target. Operators convert as AVM1 does ($.add, $.n, $.eq...),
// except where both sides are known numbers.

namespace as3 {
namespace js {
namespace as2 {

std::string Output::errorText() const {
    std::string out;
    for (const std::string& error : errors) out += error + "\n";
    return out;
}

namespace {

struct Abandon {};

enum class Kind { Any, Num, Str, Bool };

// An expression as JavaScript: its text, what it is known to give, and
// whether it can be an operand as it is.
struct E {
    std::string t;
    Kind k = Kind::Any;
    bool atom = true;
};

std::string numberText(double v) {
    if (std::isnan(v)) return "NaN";
    if (std::isinf(v)) return v > 0 ? "Infinity" : "-Infinity";
    if (v == std::floor(v) && std::fabs(v) < 9007199254740992.0) {
        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "%.0f", v);
        return buffer;
    }
    char buffer[40];
    for (int precision = 15; precision <= 17; ++precision) {
        std::snprintf(buffer, sizeof buffer, "%.*g", precision, v);
        if (std::strtod(buffer, nullptr) == v) break;
    }
    return buffer;
}

// What a movie clip has of its own, for a class that extends MovieClip.
const std::vector<std::string>& movieClipMembers() {
    static const std::vector<std::string> names = {
        "_x", "_y", "_xscale", "_yscale", "_alpha", "_visible", "_rotation", "_width", "_height", "_name",
        "_parent", "_currentframe", "_totalframes", "_framesloaded", "_target", "_url", "_droptarget", "_xmouse",
        "_ymouse", "_focusrect", "_lockroot", "_quality", "_highquality", "_soundbuftime", "enabled",
        "useHandCursor", "hitArea", "tabEnabled", "tabIndex", "tabChildren", "trackAsMenu", "menu", "focusEnabled",
        "filters", "blendMode", "cacheAsBitmap", "opaqueBackground", "scrollRect", "scale9Grid", "transform",
        "attachMovie", "createEmptyMovieClip", "createTextField", "duplicateMovieClip", "removeMovieClip",
        "getBounds", "getRect", "getBytesLoaded", "getBytesTotal", "getDepth", "getInstanceAtDepth",
        "getNextHighestDepth", "getURL", "globalToLocal", "localToGlobal", "gotoAndPlay", "gotoAndStop", "hitTest",
        "loadMovie", "loadVariables", "nextFrame", "prevFrame", "play", "stop", "setMask", "startDrag", "stopDrag",
        "swapDepths", "unloadMovie", "beginFill", "beginGradientFill", "beginBitmapFill", "clear", "curveTo",
        "endFill", "lineStyle", "lineGradientStyle", "lineTo", "moveTo", "attachBitmap", "attachAudio",
        "getSWFVersion", "getTextSnapshot", "onEnterFrame", "onLoad", "onUnload", "onPress", "onRelease",
        "onReleaseOutside", "onRollOver", "onRollOut", "onDragOver", "onDragOut", "onMouseDown", "onMouseUp",
        "onMouseMove", "onKeyDown", "onKeyUp", "onData", "onSetFocus", "onKillFocus"};
    return names;
}

// Names whose spelling wins where case does not count: the player's.
const std::vector<std::string>& playerNames() {
    static const std::vector<std::string> names = {
        "trace", "getTimer", "setInterval", "clearInterval", "setTimeout", "clearTimeout", "escape", "unescape",
        "parseInt", "parseFloat", "isNaN", "isFinite", "Number", "String", "Boolean", "Object", "Array", "Function",
        "Math", "Date", "Error", "MovieClip", "Button", "TextField", "TextFormat", "Key", "Mouse", "Stage", "Sound",
        "Color", "LoadVars", "XML", "XMLNode", "SharedObject", "Selection", "System", "Capabilities", "MovieClipLoader",
        "_root", "_parent", "_global", "_level0", "this", "length", "prototype", "__proto__", "constructor",
        "toString", "valueOf", "push", "pop", "shift", "unshift", "splice", "slice", "join", "concat", "reverse",
        "sort", "sortOn", "charAt", "charCodeAt", "indexOf", "lastIndexOf", "substr", "substring", "split",
        "toUpperCase", "toLowerCase", "fromCharCode", "random", "round", "floor", "ceil", "abs", "min", "max",
        "sqrt", "pow", "sin", "cos", "tan", "atan2", "PI", "isDown", "getCode", "getAscii", "addListener",
        "removeListener", "text", "htmlText", "textColor", "autoSize", "setRGB", "setTransform", "start",
        "attachSound", "setVolume", "setPan", "registerClass", "addProperty", "apply", "call", "undefined",
        "random", "int", "chr", "ord", "eval", "targetPath", "getProperty", "setProperty", "fscommand",
        "tellTarget", "ifFrameLoaded", "nextScene", "prevScene", "stopAllSounds", "getVersion", "loadMovieNum",
        "loadVariablesNum", "unloadMovieNum", "mbchr", "mbord", "mblength", "mbsubstring", "updateAfterEvent"};
    return names;
}

// A visit to a node and, while visit returns true, its children.
void walk(AstNode* node, const std::function<bool(AstNode*)>& visit);

void walkExpression(Expression* e, const std::function<bool(AstNode*)>& visit) {
    if (e) walk(e, visit);
}

void walk(AstNode* node, const std::function<bool(AstNode*)>& visit) {
    if (!node || !visit(node)) return;
    if (auto* b = dynamic_cast<BinaryExpression*>(node)) {
        walkExpression(b->left.get(), visit);
        walkExpression(b->right.get(), visit);
    } else if (auto* u = dynamic_cast<UnaryExpression*>(node)) {
        walkExpression(u->operand.get(), visit);
    } else if (auto* c = dynamic_cast<ConditionalExpression*>(node)) {
        walkExpression(c->test.get(), visit);
        walkExpression(c->consequent.get(), visit);
        walkExpression(c->alternate.get(), visit);
    } else if (auto* a = dynamic_cast<AssignmentExpression*>(node)) {
        walkExpression(a->left.get(), visit);
        walkExpression(a->right.get(), visit);
    } else if (auto* call = dynamic_cast<CallExpression*>(node)) {
        walkExpression(call->callee.get(), visit);
        for (auto& arg : call->arguments) walkExpression(arg.get(), visit);
    } else if (auto* n = dynamic_cast<NewExpression*>(node)) {
        walkExpression(n->callee.get(), visit);
        for (auto& arg : n->arguments) walkExpression(arg.get(), visit);
    } else if (auto* m = dynamic_cast<MemberExpression*>(node)) {
        walkExpression(m->object.get(), visit);
        if (m->property) walk(m->property.get(), visit);
        walkExpression(m->computedProperty.get(), visit);
    } else if (auto* f = dynamic_cast<FunctionExpression*>(node)) {
        for (auto& p : f->parameters) walk(p.get(), visit);
        walk(f->body.get(), visit);
    } else if (auto* arr = dynamic_cast<ArrayExpression*>(node)) {
        for (auto& e : arr->elements) walkExpression(e.get(), visit);
    } else if (auto* o = dynamic_cast<ObjectExpression*>(node)) {
        for (auto& p : o->properties) {
            walkExpression(p.first.get(), visit);
            walkExpression(p.second.get(), visit);
        }
    } else if (auto* s = dynamic_cast<SequenceExpression*>(node)) {
        for (auto& e : s->expressions) walkExpression(e.get(), visit);
    } else if (auto* block = dynamic_cast<BlockStatement*>(node)) {
        for (auto& st : block->statements) walk(st.get(), visit);
    } else if (auto* es = dynamic_cast<ExpressionStatement*>(node)) {
        walkExpression(es->expression.get(), visit);
    } else if (auto* is = dynamic_cast<IfStatement*>(node)) {
        walkExpression(is->test.get(), visit);
        walk(is->consequent.get(), visit);
        walk(is->alternate.get(), visit);
    } else if (auto* ws = dynamic_cast<WhileStatement*>(node)) {
        walkExpression(ws->test.get(), visit);
        walk(ws->body.get(), visit);
    } else if (auto* ds = dynamic_cast<DoWhileStatement*>(node)) {
        walk(ds->body.get(), visit);
        walkExpression(ds->test.get(), visit);
    } else if (auto* fs = dynamic_cast<ForStatement*>(node)) {
        walk(fs->init.get(), visit);
        walkExpression(fs->test.get(), visit);
        walkExpression(fs->update.get(), visit);
        walk(fs->body.get(), visit);
    } else if (auto* fi = dynamic_cast<ForInStatement*>(node)) {
        walk(fi->left.get(), visit);
        walkExpression(fi->right.get(), visit);
        walk(fi->body.get(), visit);
    } else if (auto* sw = dynamic_cast<SwitchStatement*>(node)) {
        walkExpression(sw->discriminant.get(), visit);
        for (auto& c : sw->cases) walk(c.get(), visit);
    } else if (auto* cc = dynamic_cast<CaseClause*>(node)) {
        walkExpression(cc->test.get(), visit);
        for (auto& st : cc->consequent) walk(st.get(), visit);
    } else if (auto* rs = dynamic_cast<ReturnStatement*>(node)) {
        walkExpression(rs->argument.get(), visit);
    } else if (auto* ts = dynamic_cast<ThrowStatement*>(node)) {
        walkExpression(ts->argument.get(), visit);
    } else if (auto* tr = dynamic_cast<TryStatement*>(node)) {
        walk(tr->block.get(), visit);
        for (auto& c : tr->catches) {
            if (c.variable) walk(c.variable.get(), visit);
            walk(c.body.get(), visit);
        }
        walk(tr->finallyBody.get(), visit);
    } else if (auto* w = dynamic_cast<WithStatement*>(node)) {
        walkExpression(w->object.get(), visit);
        walk(w->body.get(), visit);
    } else if (auto* ls = dynamic_cast<LabeledStatement*>(node)) {
        walk(ls->body.get(), visit);
    } else if (auto* vd = dynamic_cast<VariableDefinition*>(node)) {
        for (auto& d : vd->declarations) {
            if (d->id) walk(d->id.get(), visit);
            walkExpression(d->init.get(), visit);
        }
    } else if (auto* p = dynamic_cast<Parameter*>(node)) {
        if (p->name) walk(p->name.get(), visit);
        walkExpression(p->defaultValue.get(), visit);
    } else if (auto* fd = dynamic_cast<FunctionDefinition*>(node)) {
        if (fd->name) walk(fd->name.get(), visit);
        for (auto& p : fd->parameters) walk(p.get(), visit);
        walk(fd->body.get(), visit);
    } else if (auto* cd = dynamic_cast<ClassDefinition*>(node)) {
        for (auto& m : cd->members) walk(m.get(), visit);
    } else if (auto* prog = dynamic_cast<Program*>(node)) {
        for (auto& st : prog->statements) walk(st.get(), visit);
    }
}

// The names a function declares: parameters, variables, functions and
// caught errors, not those of the functions in it.
std::set<std::string> declaredIn(const std::vector<std::unique_ptr<Parameter>>& parameters, Statement* body) {
    std::set<std::string> names;
    for (const auto& p : parameters) {
        if (p->name) names.insert(p->name->name);
    }
    walk(body, [&](AstNode* node) {
        if (auto* fd = dynamic_cast<FunctionDefinition*>(node)) {
            if (fd->name) names.insert(fd->name->name);
            return false;
        }
        if (dynamic_cast<FunctionExpression*>(node)) return false;
        if (auto* vd = dynamic_cast<VariableDefinition*>(node)) {
            for (auto& d : vd->declarations) {
                if (d->id) names.insert(d->id->name);
            }
        }
        if (auto* tr = dynamic_cast<TryStatement*>(node)) {
            for (auto& c : tr->catches) {
                if (c.variable) names.insert(c.variable->name);
            }
        }
        return true;
    });
    return names;
}

bool callsSuper(AstNode* node) {
    bool found = false;
    walk(node, [&](AstNode* n) {
        if (dynamic_cast<FunctionExpression*>(n) || found) return false;
        if (auto* call = dynamic_cast<CallExpression*>(n)) {
            if (dynamic_cast<SuperExpression*>(call->callee.get())) found = true;
        }
        return true;
    });
    return found;
}

bool usesName(AstNode* node, const std::string& name) {
    bool found = false;
    walk(node, [&](AstNode* n) {
        if (auto* id = dynamic_cast<Identifier*>(n)) {
            if (id->name == name) found = true;
        }
        return !found;
    });
    return found;
}

// The parts of a script between #initclip and #endinitclip, and the rest,
// lines kept where they were; the order #initclip gives, 0 for none.
void splitInitClip(const std::string& source, std::string& frame, std::string& init, int& order) {
    frame.clear();
    init.clear();
    order = 0;
    bool inInit = false;
    size_t start = 0;
    while (start <= source.size()) {
        size_t end = source.find('\n', start);
        const bool last = end == std::string::npos;
        if (last) end = source.size();
        const std::string line = source.substr(start, end - start);
        const size_t first = line.find_first_not_of(" \t\r");
        const std::string word = first == std::string::npos ? std::string() : line.substr(first);
        if (word.compare(0, 9, "#initclip") == 0) {
            inInit = true;
            order = std::atoi(word.c_str() + 9);
        } else if (word.compare(0, 12, "#endinitclip") == 0) {
            inInit = false;
        } else {
            (inInit ? init : frame) += line;
        }
        if (last) break;
        frame += '\n';
        init += '\n';
        start = end + 1;
    }
}

bool blank(const std::string& text) { return text.find_first_not_of(" \t\r\n") == std::string::npos; }

// Lines of code, indented.
struct Out {
    std::vector<std::string> lines;
    int depth = 0;
    void line(const std::string& text) {
        const std::string pad(static_cast<size_t>(depth) * 4, ' ');
        size_t start = 0;
        while (start <= text.size()) {
            const size_t end = text.find('\n', start);
            const std::string part = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
            lines.push_back(part.empty() ? part : pad + part);
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }
    std::string text() const {
        std::string out;
        for (size_t i = 0; i < lines.size(); ++i) out += (i ? "\n" : "") + lines[i];
        return out;
    }
};

struct ClassInfo {
    std::string superName;
    std::vector<std::string> instance;
    std::vector<std::string> statics;
};

// A function being translated (or a script's top level, where names are
// the timeline's).
struct Fn {
    Fn* outer = nullptr;
    bool function = false;
    std::set<std::string> locals;
    std::set<std::string> numeric;
};

class Translator {
public:
    Translator(const Options& options, std::vector<std::string>& errors) : _options(options), _errors(errors) {}

    // Every class's members, for the names their methods find.
    std::map<std::string, ClassInfo> classes;
    // Where case does not count: each name's spelling, by its lower case.
    std::map<std::string, std::string> spellings;

    std::unique_ptr<Program> parse(const std::string& source, const std::string& where) {
        Parser parser(source, where);
        if (_options.includeHandler) parser.setIncludeHandler(_options.includeHandler);
        std::unique_ptr<Program> program = parser.parse();
        if (parser.hasErrors()) {
            std::istringstream lines(parser.getErrors());
            std::string line;
            while (std::getline(lines, line)) {
                if (!line.empty()) _errors.push_back((where.empty() ? std::string() : where + ", ") + line);
            }
            return nullptr;
        }
        return program;
    }

    // The names a program has, for their spellings.
    void gatherNames(Program* program) {
        walk(program, [this](AstNode* node) {
            if (auto* id = dynamic_cast<Identifier*>(node)) spell(id->name);
            if (auto* m = dynamic_cast<MemberExpression*>(node)) {
                if (m->property) spell(m->property->name);
            }
            return true;
        });
    }

    void spell(const std::string& name) {
        if (name.empty() || name.find('.') != std::string::npos) return;
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        spellings.emplace(lower, name);
    }

    void seedSpellings() {
        for (const std::string& name : playerNames()) spell(name);
        for (const std::string& name : movieClipMembers()) spell(name);
    }

    // A name as the movie spells it.
    std::string F(const std::string& name) const {
        if (_options.swfVersion >= 7) return name;
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        const auto found = spellings.find(lower);
        return found == spellings.end() ? name : found->second;
    }

    // ---- Scripts

    // A frame's (or #initclip's) code as function ($s) { ... }.
    std::string script(Program* program, const std::string& where, const std::string& scene) {
        _where = where;
        _scene = scene;
        reset();
        Fn top;
        _fn = &top;
        Out out;
        out.depth = 1;
        std::vector<Statement*> statements;
        for (auto& node : program->statements) {
            if (auto* import = dynamic_cast<ImportDirective*>(node.get())) addImport(import);
            else if (auto* s = dynamic_cast<Statement*>(node.get())) statements.push_back(s);
        }
        for (auto& import : program->imports) addImport(import.get());
        try {
            topLevel(statements, out);
        } catch (const Abandon&) {
            _fn = nullptr;
            return std::string();
        }
        _fn = nullptr;
        return "function ($s) {\n" + out.text() + (out.lines.empty() ? "" : "\n") + "}";
    }

    // An instance's handlers as [{events: [...], clip: true, run: function ($s) {...}}, ...].
    std::string handlers(Program* program, const std::string& where) {
        _where = where;
        _scene.clear();
        reset();
        Out list;
        list.depth = 1;
        for (auto& import : program->imports) addImport(import.get());
        for (auto& node : program->statements) {
            if (dynamic_cast<EmptyStatement*>(node.get())) continue;
            if (auto* import = dynamic_cast<ImportDirective*>(node.get())) {
                addImport(import);
                continue;
            }
            auto* statement = dynamic_cast<ExpressionStatement*>(node.get());
            auto* call = statement ? dynamic_cast<CallExpression*>(statement->expression.get()) : nullptr;
            auto* callee = call ? dynamic_cast<Identifier*>(call->callee.get()) : nullptr;
            if (!callee || (callee->name != "__on" && callee->name != "__onClipEvent") || call->arguments.size() != 2) {
                auto* s = dynamic_cast<Statement*>(node.get());
                error(s ? s->line : 0, "an instance's script has only on() and onClipEvent() handlers");
                return std::string();
            }
            std::string events;
            if (auto* array = dynamic_cast<ArrayExpression*>(call->arguments[0].get())) {
                for (auto& event : array->elements) {
                    if (auto* literal = dynamic_cast<Literal*>(event.get())) {
                        events += (events.empty() ? "" : ", ") + quote(literal->stringValue);
                    }
                }
            }
            auto* block = dynamic_cast<FunctionExpression*>(call->arguments[1].get());
            auto* body = block ? dynamic_cast<BlockStatement*>(block->body.get()) : nullptr;
            std::vector<Statement*> statements;
            if (body) {
                for (auto& s : body->statements) statements.push_back(s.get());
            }
            Fn top;
            _fn = &top;
            Out out;
            out.depth = 1;
            try {
                topLevel(statements, out);
            } catch (const Abandon&) {
                _fn = nullptr;
                return std::string();
            }
            _fn = nullptr;
            list.line(std::string("{events: [") + events + "], clip: " +
                      (callee->name == "__onClipEvent" ? "true" : "false") + ", run: function ($s) {\n" + out.text() +
                      (out.lines.empty() ? "" : "\n") + "}},");
        }
        return "[\n" + list.text() + (list.lines.empty() ? "" : "\n") + "]";
    }

    // An ActionScript 2 class file as function ($s) { ... } that defines it
    // on _global; its name, and the classes it needs first.
    std::string classFile(Program* program, const std::string& where, std::string& name,
                          std::vector<std::string>& needs) {
        _where = where;
        _scene.clear();
        reset();
        ClassDefinition* definition = nullptr;
        for (auto& import : program->imports) addImport(import.get());
        for (auto& node : program->statements) {
            if (auto* cd = dynamic_cast<ClassDefinition*>(node.get())) {
                if (definition) {
                    error(cd->line, "a class file has one class");
                    return std::string();
                }
                definition = cd;
            } else if (auto* import = dynamic_cast<ImportDirective*>(node.get())) {
                addImport(import);
            }
        }
        if (!definition || !definition->name) {
            error(0, "the file has no class");
            return std::string();
        }
        name = definition->name->name;
        _className = name;
        const size_t dot = name.find_last_of('.');
        _package = dot == std::string::npos ? std::string() : name.substr(0, dot);
        _imports[name.substr(dot == std::string::npos ? 0 : dot + 1)] = name;
        _superName = definition->superClass ? qualify(definition->superClass->name) : std::string();
        if (!_superName.empty()) needs.push_back(_superName);
        for (const auto& iface : definition->interfaces) {
            if (iface) needs.push_back(qualify(iface->name));
        }
        gatherMembers();
        Fn top;
        _fn = &top;
        Out out;
        out.depth = 1;
        try {
            classBody(definition, out);
        } catch (const Abandon&) {
            _fn = nullptr;
            return std::string();
        }
        _fn = nullptr;
        return "function ($s) {\n" + out.text() + "\n}";
    }

    // A class's members, for the others' methods.
    static ClassInfo membersOf(ClassDefinition* definition) {
        ClassInfo out;
        if (definition->superClass) out.superName = definition->superClass->name;
        const std::string full = definition->name ? definition->name->name : std::string();
        const std::string shortName = full.substr(full.find_last_of('.') == std::string::npos ? 0 : full.find_last_of('.') + 1);
        for (auto& member : definition->members) {
            if (auto* fd = dynamic_cast<FunctionDefinition*>(member.get())) {
                if (!fd->name || fd->name->name == shortName) continue;
                (hasAttribute(fd->attributes, AttributeType::Static) ? out.statics : out.instance).push_back(fd->name->name);
            } else if (auto* vd = dynamic_cast<VariableDefinition*>(member.get())) {
                for (auto& decl : vd->declarations) {
                    if (decl->id) {
                        (hasAttribute(vd->attributes, AttributeType::Static) ? out.statics : out.instance)
                            .push_back(decl->id->name);
                    }
                }
            }
        }
        return out;
    }

private:
    const Options& _options;
    std::vector<std::string>& _errors;
    std::string _where;
    std::string _scene;
    Fn* _fn = nullptr;
    // The variable the scope chain is in: $s, or a with block's or
    // tellTarget's.
    std::string _scope = "$s";
    // The with blocks around the code, by their scope's variable.
    std::vector<std::string> _withs;
    int _scopes = 0;
    int _temps = 0;
    std::map<std::string, std::string> _imports;
    std::vector<std::string> _wildcards;
    std::string _className;
    std::string _package;
    std::string _superName;
    std::set<std::string> _instanceMembers;
    std::map<std::string, std::string> _staticMembers;
    bool _inMethod = false;
    bool _inConstructor = false;

    void reset() {
        _scope = "$s";
        _withs.clear();
        _scopes = 0;
        _temps = 0;
        _imports.clear();
        _wildcards.clear();
        _className.clear();
        _package.clear();
        _superName.clear();
        _instanceMembers.clear();
        _staticMembers.clear();
        _inMethod = false;
    }

    [[noreturn]] void fail(int line, const std::string& message) {
        error(line, message);
        throw Abandon();
    }

    void error(int line, const std::string& message) {
        std::string where = _where.empty() ? std::string() : _where + ", ";
        _errors.push_back(where + "line " + std::to_string(line) + ": " + message);
    }

    void addImport(ImportDirective* import) {
        if (!import || !import->name) return;
        if (import->isPackage || import->name->name == "*") {
            _wildcards.push_back(import->path.empty() ? import->name->name : import->path);
        } else {
            _imports[import->name->name] = import->path.empty() ? import->name->name : import->path + "." + import->name->name;
        }
    }

    // The qualified name of a class named short, when it is one of the
    // movie's; or the name as it is.
    std::string qualify(const std::string& name) const {
        const auto imported = _imports.find(name);
        if (imported != _imports.end()) return imported->second;
        if (name.find('.') != std::string::npos) return name;
        if (!_package.empty() && classes.count(_package + "." + name)) return _package + "." + name;
        for (const std::string& package : _wildcards) {
            if (classes.count(package + "." + name)) return package + "." + name;
        }
        return name;
    }

    void gatherMembers() {
        const auto own = classes.find(_className);
        if (own != classes.end()) {
            _instanceMembers.insert(own->second.instance.begin(), own->second.instance.end());
            for (const std::string& n : own->second.statics) _staticMembers.emplace(n, _className);
        }
        std::string super = _superName;
        std::set<std::string> seen;
        while (!super.empty() && seen.insert(super).second) {
            const auto found = classes.find(super);
            if (found == classes.end()) {
                if (super == "MovieClip") _instanceMembers.insert(movieClipMembers().begin(), movieClipMembers().end());
                break;
            }
            _instanceMembers.insert(found->second.instance.begin(), found->second.instance.end());
            for (const std::string& n : found->second.statics) _staticMembers.emplace(n, super);
            std::string next = found->second.superName;
            const size_t dot = super.find_last_of('.');
            if (next.find('.') == std::string::npos && dot != std::string::npos && classes.count(super.substr(0, dot + 1) + next)) {
                next = super.substr(0, dot + 1) + next;
            }
            super = next;
        }
    }

    // A class by its qualified name, on _global.
    std::string classPath(const std::string& qualified) const {
        std::string out = "$g";
        size_t start = 0;
        while (start <= qualified.size()) {
            const size_t dot = qualified.find('.', start);
            const std::string part = qualified.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
            out += isIdentifier(part) ? "." + part : "[" + quote(part) + "]";
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        return out;
    }

    std::string temp() { return "$t" + std::to_string(++_temps); }

    // ---- Names

    // A local's JavaScript name: one JavaScript keeps for itself, or one
    // like the translation's own ($s, $g), changed.
    static std::string localName(const std::string& name) {
        if (name == "arguments") return name;
        if (isReserved(name) || (!name.empty() && name[0] == '$')) return "$_" + name;
        if (!isIdentifier(name)) return "$_" + std::to_string(std::hash<std::string>()(name) % 100000);
        return name;
    }

    Fn* localOwner(const std::string& name) const {
        for (Fn* f = _fn; f; f = f->outer) {
            if (f->function && f->locals.count(name)) return f;
        }
        return nullptr;
    }

    bool isLocal(const std::string& name) const { return localOwner(name) != nullptr; }

    bool isNumericLocal(const std::string& name) const {
        Fn* owner = localOwner(name);
        return owner && owner->numeric.count(name) && _withs.empty();
    }

    // A class's member by its bare name, inside its methods: "this.x",
    // "$g.a.Hat.x", or empty.
    std::string memberFor(const std::string& name) const {
        if (!_inMethod || isLocal(name)) return std::string();
        if (_instanceMembers.count(name)) return "this." + name;
        const auto found = _staticMembers.find(name);
        if (found != _staticMembers.end()) return classPath(found->second) + "." + name;
        return std::string();
    }

    // The scope chain's lookup of a name: with blocks', the timeline's,
    // _global's.
    E loadName(const std::string& rawName, int line) {
        const std::string name = F(rawName);
        if (name == "undefined") return {"undefined"};
        if (name == "NaN" || name == "Infinity") return {name, Kind::Num};
        if (name == "this") return {"this"};
        if (isLocal(name)) {
            const std::string js = localName(name);
            if (!_withs.empty()) return {_withs.back() + ".getLocal(" + quote(name) + ", () => " + js + ")"};
            return {js, isNumericLocal(name) ? Kind::Num : Kind::Any};
        }
        const std::string member = memberFor(name);
        if (!member.empty()) return {member};
        const std::string path = qualify(name);
        if (path != name && classes.count(path)) return {classPath(path)};
        if (path != name) return {"$.qualified(" + quote(path) + ")"};
        (void)line;
        return {_scope + ".get(" + quote(name) + ")"};
    }

    // name = value; the expression's value is the value.
    std::string storeName(const std::string& rawName, const std::string& value, bool define) {
        const std::string name = F(rawName);
        if (isLocal(name)) {
            const std::string js = localName(name);
            if (!_withs.empty()) return _withs.back() + ".setLocal(" + quote(name) + ", " + value + ", (v$) => " + js + " = v$)";
            return js + " = " + value;
        }
        const std::string member = memberFor(name);
        if (!member.empty()) return member + " = " + value;
        if (define && _withs.empty()) return _scope + ".def(" + quote(name) + ", " + value + ")";
        return _scope + ".set(" + quote(name) + ", " + value + ")";
    }

    // ---- Kinds of values

    Kind kindOf(Expression* e) const {
        if (!e) return Kind::Any;
        if (auto* l = dynamic_cast<Literal*>(e)) {
            if (l->kind == Literal::LiteralKind::Numeric) return Kind::Num;
            if (l->kind == Literal::LiteralKind::String) return Kind::Str;
            if (l->kind == Literal::LiteralKind::Boolean) return Kind::Bool;
            return Kind::Any;
        }
        if (auto* id = dynamic_cast<Identifier*>(e)) {
            if (id->name == "NaN" || id->name == "Infinity") return Kind::Num;
            return isNumericLocal(F(id->name)) ? Kind::Num : Kind::Any;
        }
        if (auto* u = dynamic_cast<UnaryExpression*>(e)) {
            switch (u->op) {
                case OperatorType::Negative:
                case OperatorType::Positive:
                case OperatorType::BitwiseNot:
                case OperatorType::PreIncrement:
                case OperatorType::PreDecrement:
                case OperatorType::PostIncrement:
                case OperatorType::PostDecrement: return Kind::Num;
                case OperatorType::LogicalNot:
                case OperatorType::Delete: return Kind::Bool;
                case OperatorType::Typeof: return Kind::Str;
                default: return Kind::Any;
            }
        }
        if (auto* b = dynamic_cast<BinaryExpression*>(e)) {
            switch (b->op) {
                case OperatorType::Add: {
                    const Kind l = kindOf(b->left.get()), r = kindOf(b->right.get());
                    if (l == Kind::Str || r == Kind::Str) return Kind::Str;
                    if (l == Kind::Num && r == Kind::Num) return Kind::Num;
                    return Kind::Any;
                }
                case OperatorType::Subtract:
                case OperatorType::Multiply:
                case OperatorType::Divide:
                case OperatorType::Remainder:
                case OperatorType::ShiftLeft:
                case OperatorType::ShiftRight:
                case OperatorType::ShiftRightUnsigned:
                case OperatorType::BitwiseAnd:
                case OperatorType::BitwiseOr:
                case OperatorType::BitwiseXor: return Kind::Num;
                case OperatorType::Equals:
                case OperatorType::NotEquals:
                case OperatorType::StrictEquals:
                case OperatorType::StrictNotEquals:
                case OperatorType::Instanceof: return Kind::Bool;
                default: return Kind::Any;
            }
        }
        if (auto* a = dynamic_cast<AssignmentExpression*>(e)) {
            if (a->op == OperatorType::Assign) return kindOf(a->right.get());
            if (a->op == OperatorType::Add) {
                return kindOf(a->left.get()) == Kind::Num && kindOf(a->right.get()) == Kind::Num ? Kind::Num : Kind::Any;
            }
            return Kind::Num;
        }
        if (auto* call = dynamic_cast<CallExpression*>(e)) {
            if (auto* id = dynamic_cast<Identifier*>(call->callee.get())) {
                const std::string n = F(id->name);
                if (!isLocal(n) && (n == "getTimer" || n == "random" || n == "int" || n == "ord" || n == "length")) return Kind::Num;
            }
            if (auto* m = dynamic_cast<MemberExpression*>(call->callee.get())) {
                auto* owner = dynamic_cast<Identifier*>(m->object.get());
                if (owner && F(owner->name) == "Math" && !isLocal("Math") && !m->computed) return Kind::Num;
            }
        }
        return Kind::Any;
    }

    // Which of a function's variables only ever hold numbers: each given
    // one where declared, and only numbers after.
    void inferNumeric(Fn& fn, Statement* body, const std::set<std::string>& parameters) {
        std::set<std::string> candidates;
        std::set<std::string> initialized;
        walk(body, [&](AstNode* node) {
            if (dynamic_cast<FunctionDefinition*>(node) || dynamic_cast<FunctionExpression*>(node)) return false;
            if (auto* vd = dynamic_cast<VariableDefinition*>(node)) {
                for (auto& d : vd->declarations) {
                    if (d->id && d->init) initialized.insert(d->id->name);
                }
            }
            return true;
        });
        for (const std::string& name : fn.locals) {
            if (!parameters.count(name) && initialized.count(name)) candidates.insert(name);
        }
        // Declared twice, or assigned by a for-in, or in a nested function:
        // not a number for sure.
        Fn* saved = _fn;
        _fn = &fn;
        fn.numeric = candidates;
        for (bool changed = true; changed;) {
            changed = false;
            const auto drop = [&](const std::string& name) {
                if (fn.numeric.erase(name)) changed = true;
            };
            walk(body, [&](AstNode* node) {
                if (auto* vd = dynamic_cast<VariableDefinition*>(node)) {
                    for (auto& d : vd->declarations) {
                        if (d->id && fn.numeric.count(d->id->name) && (!d->init || kindOf(d->init.get()) != Kind::Num)) drop(d->id->name);
                    }
                } else if (auto* a = dynamic_cast<AssignmentExpression*>(node)) {
                    auto* id = dynamic_cast<Identifier*>(a->left.get());
                    if (id && fn.numeric.count(id->name) && kindOf(a) != Kind::Num) drop(id->name);
                } else if (auto* fi = dynamic_cast<ForInStatement*>(node)) {
                    walk(fi->left.get(), [&](AstNode* n) {
                        if (auto* id = dynamic_cast<Identifier*>(n)) drop(id->name);
                        return true;
                    });
                } else if (dynamic_cast<FunctionExpression*>(node) || dynamic_cast<FunctionDefinition*>(node)) {
                    // A nested function changing it: whatever it stores.
                    walk(node, [&](AstNode* n) {
                        if (auto* a = dynamic_cast<AssignmentExpression*>(n)) {
                            if (auto* id = dynamic_cast<Identifier*>(a->left.get())) drop(id->name);
                        }
                        if (auto* u = dynamic_cast<UnaryExpression*>(n)) {
                            if (auto* id = dynamic_cast<Identifier*>(u->operand.get())) drop(id->name);
                        }
                        return true;
                    });
                    return false;
                }
                return true;
            });
        }
        // eval, set and with may change any.
        bool dynamicNames = false;
        walk(body, [&](AstNode* node) {
            if (dynamic_cast<WithStatement*>(node)) dynamicNames = true;
            if (auto* call = dynamic_cast<CallExpression*>(node)) {
                auto* id = dynamic_cast<Identifier*>(call->callee.get());
                if (id && (id->name == "eval" || id->name == "set")) dynamicNames = true;
            }
            return !dynamicNames;
        });
        if (dynamicNames) fn.numeric.clear();
        _fn = saved;
    }

    // ---- Statements

    // A script's top level: its functions defined on the timeline first.
    void topLevel(const std::vector<Statement*>& statements, Out& out) {
        for (Statement* s : statements) {
            if (auto* fd = dynamic_cast<FunctionDefinition*>(s)) {
                if (!fd->name) continue;
                out.line(_scope + ".def(" + quote(F(fd->name->name)) + ", " + function(fd->name->name, fd->parameters, fd->body.get(), fd->line) + ");");
            }
        }
        for (Statement* s : statements) {
            if (!dynamic_cast<FunctionDefinition*>(s)) statement(s, out);
        }
    }

    void block(Statement* s, Out& out) {
        if (auto* b = dynamic_cast<BlockStatement*>(s)) {
            for (auto& st : b->statements) statement(st.get(), out);
        } else if (s) {
            statement(s, out);
        }
    }

    // A statement as the body of if, while, for...: in braces.
    std::string body(Statement* s, int depth) {
        Out inner;
        inner.depth = depth;
        block(s, inner);
        return inner.text();
    }

    void braced(const std::string& head, Statement* s, Out& out, const std::string& tail = "}") {
        out.line(head + " {");
        ++out.depth;
        block(s, out);
        --out.depth;
        out.line(tail);
    }

    void statement(Statement* s, Out& out) {
        if (!s || dynamic_cast<EmptyStatement*>(s)) return;
        const int line = s->line;
        if (auto* es = dynamic_cast<ExpressionStatement*>(s)) {
            if (auto* call = dynamic_cast<CallExpression*>(es->expression.get())) {
                if (auto* id = dynamic_cast<Identifier*>(call->callee.get())) {
                    if (id->name == "__tellTarget") {
                        tellTarget(call, out);
                        return;
                    }
                    if (id->name == "__on" || id->name == "__onClipEvent") {
                        fail(line, std::string(id->name == "__on" ? "on()" : "onClipEvent()") +
                                       " goes on a button or a movie clip instance, not a frame");
                    }
                }
            }
            out.line(discarded(es->expression.get()) + ";");
            return;
        }
        if (auto* vd = dynamic_cast<VariableDefinition*>(s)) {
            variables(vd, out);
            return;
        }
        if (auto* fd = dynamic_cast<FunctionDefinition*>(s)) {
            if (!_fn->function) {
                // In a block at the top level: defined where it is.
                if (fd->name) out.line(_scope + ".def(" + quote(F(fd->name->name)) + ", " + function(fd->name->name, fd->parameters, fd->body.get(), fd->line) + ");");
                return;
            }
            out.line(function(fd->name ? fd->name->name : std::string(), fd->parameters, fd->body.get(), fd->line, true));
            return;
        }
        if (auto* b = dynamic_cast<BlockStatement*>(s)) {
            out.line("{");
            ++out.depth;
            block(b, out);
            --out.depth;
            out.line("}");
            return;
        }
        if (auto* is = dynamic_cast<IfStatement*>(s)) {
            out.line("if (" + condition(is->test.get()) + ") {");
            ++out.depth;
            block(is->consequent.get(), out);
            --out.depth;
            Statement* alternate = is->alternate.get();
            while (auto* elseIf = dynamic_cast<IfStatement*>(alternate)) {
                out.line("} else if (" + condition(elseIf->test.get()) + ") {");
                ++out.depth;
                block(elseIf->consequent.get(), out);
                --out.depth;
                alternate = elseIf->alternate.get();
            }
            if (alternate) {
                out.line("} else {");
                ++out.depth;
                block(alternate, out);
                --out.depth;
            }
            out.line("}");
            return;
        }
        if (auto* ws = dynamic_cast<WhileStatement*>(s)) {
            braced("while (" + condition(ws->test.get()) + ")", ws->body.get(), out);
            return;
        }
        if (auto* ds = dynamic_cast<DoWhileStatement*>(s)) {
            out.line("do {");
            ++out.depth;
            block(ds->body.get(), out);
            --out.depth;
            out.line("} while (" + condition(ds->test.get()) + ");");
            return;
        }
        if (auto* fs = dynamic_cast<ForStatement*>(s)) {
            forLoop(fs, out);
            return;
        }
        if (auto* fi = dynamic_cast<ForInStatement*>(s)) {
            forIn(fi, out);
            return;
        }
        if (auto* sw = dynamic_cast<SwitchStatement*>(s)) {
            out.line("switch (" + expression(sw->discriminant.get()).t + ") {");
            ++out.depth;
            for (auto& c : sw->cases) {
                out.line(c->test ? "case " + expression(c->test.get()).t + ":" : "default:");
                ++out.depth;
                for (auto& st : c->consequent) statement(st.get(), out);
                --out.depth;
            }
            --out.depth;
            out.line("}");
            return;
        }
        if (auto* bs = dynamic_cast<BreakStatement*>(s)) {
            out.line(bs->label ? "break " + bs->label->name + ";" : "break;");
            return;
        }
        if (auto* cs = dynamic_cast<ContinueStatement*>(s)) {
            out.line(cs->label ? "continue " + cs->label->name + ";" : "continue;");
            return;
        }
        if (auto* rs = dynamic_cast<ReturnStatement*>(s)) {
            out.line(rs->argument ? "return " + expression(rs->argument.get()).t + ";" : "return;");
            return;
        }
        if (auto* ts = dynamic_cast<ThrowStatement*>(s)) {
            out.line("throw " + expression(ts->argument.get()).t + ";");
            return;
        }
        if (auto* tr = dynamic_cast<TryStatement*>(s)) {
            tryStatement(tr, out);
            return;
        }
        if (auto* w = dynamic_cast<WithStatement*>(s)) {
            const std::string scope = "$s" + std::to_string(++_scopes);
            out.line("{");
            ++out.depth;
            out.line("const " + scope + " = " + _scope + ".with(" + expression(w->object.get()).t + ");");
            const std::string saved = _scope;
            _scope = scope;
            _withs.push_back(scope);
            block(w->body.get(), out);
            _withs.pop_back();
            _scope = saved;
            --out.depth;
            out.line("}");
            return;
        }
        if (auto* ls = dynamic_cast<LabeledStatement*>(s)) {
            out.line(ls->label->name + ":");
            statement(ls->body.get(), out);
            return;
        }
        if (dynamic_cast<ClassDefinition*>(s)) fail(line, "a class goes in a file of its own, on the class path");
        fail(line, "this statement is not in ActionScript 2");
    }

    void tellTarget(CallExpression* call, Out& out) {
        auto* block = call->arguments.size() == 2 ? dynamic_cast<FunctionExpression*>(call->arguments[1].get()) : nullptr;
        if (!block) fail(call->line, "tellTarget needs a target and a block");
        const std::string scope = "$s" + std::to_string(++_scopes);
        out.line("{");
        ++out.depth;
        out.line("const " + scope + " = " + _scope + ".tell(" + expression(call->arguments[0].get()).t + ");");
        const std::string saved = _scope;
        _scope = scope;
        this->block(block->body.get(), out);
        _scope = saved;
        --out.depth;
        out.line("}");
    }

    void variables(VariableDefinition* vd, Out& out) {
        for (auto& d : vd->declarations) {
            if (!d->id) continue;
            const std::string name = F(d->id->name);
            if (_fn->function) {
                if (!_withs.empty() && d->init) {
                    out.line("var " + localName(name) + ";");
                    out.line(storeName(name, expression(d->init.get()).t, true) + ";");
                } else {
                    out.line("var " + localName(name) + (d->init ? " = " + expression(d->init.get()).t : std::string()) + ";");
                }
            } else if (d->init) {
                out.line(storeName(name, expression(d->init.get()).t, true) + ";");
            } else {
                // var x; on a timeline: x stays what it is.
            }
        }
    }

    void forLoop(ForStatement* fs, Out& out) {
        std::string init;
        if (auto* vd = dynamic_cast<VariableDefinition*>(fs->init.get())) {
            if (_fn->function && _withs.empty()) {
                std::string list;
                for (auto& d : vd->declarations) {
                    if (!d->id) continue;
                    list += (list.empty() ? "" : ", ") + localName(F(d->id->name)) +
                            (d->init ? " = " + expression(d->init.get()).t : std::string());
                }
                init = "var " + list;
            } else {
                std::string list;
                for (auto& d : vd->declarations) {
                    if (!d->id || !d->init) continue;
                    if (_fn->function) out.line("var " + localName(F(d->id->name)) + ";");
                    list += (list.empty() ? "" : ", ") + storeName(d->id->name, expression(d->init.get()).t, true);
                }
                init = list;
            }
        } else if (auto* es = dynamic_cast<ExpressionStatement*>(fs->init.get())) {
            init = discarded(es->expression.get());
        }
        const std::string test = fs->test ? condition(fs->test.get()) : std::string();
        const std::string update = fs->update ? discarded(fs->update.get()) : std::string();
        braced("for (" + init + "; " + test + "; " + update + ")", fs->body.get(), out);
    }

    void forIn(ForInStatement* fi, Out& out) {
        if (fi->each) fail(fi->line, "for each is not in ActionScript 2");
        std::string name;
        Expression* target = nullptr;
        bool declared = false;
        if (auto* vd = dynamic_cast<VariableDefinition*>(fi->left.get())) {
            if (!vd->declarations.empty() && vd->declarations[0]->id) name = vd->declarations[0]->id->name;
            declared = true;
        } else if (auto* es = dynamic_cast<ExpressionStatement*>(fi->left.get())) {
            target = es->expression.get();
            if (auto* id = dynamic_cast<Identifier*>(target)) name = id->name;
        }
        const std::string object = expression(fi->right.get()).t;
        if (!name.empty() && isLocal(F(name)) && _withs.empty()) {
            braced("for (" + std::string(declared ? "var " : "") + localName(F(name)) + " of $.keys(" + object + "))", fi->body.get(), out);
            return;
        }
        const std::string key = "$k" + std::to_string(++_temps);
        out.line("for (const " + key + " of $.keys(" + object + ")) {");
        ++out.depth;
        if (!name.empty()) {
            out.line(storeName(name, key, declared) + ";");
        } else if (target) {
            out.line(assignTo(target, key, fi->line) + ";");
        }
        block(fi->body.get(), out);
        --out.depth;
        out.line("}");
    }

    void tryStatement(TryStatement* tr, Out& out) {
        out.line("try {");
        ++out.depth;
        block(tr->block.get(), out);
        --out.depth;
        if (!tr->catches.empty()) {
            // The first clause whose type the error is: by instanceof.
            const bool simple = tr->catches.size() == 1 && tr->catches[0].type.empty();
            const std::string caught = simple && tr->catches[0].variable ? localName(F(tr->catches[0].variable->name)) : "$e";
            out.line("} catch (" + caught + ") {");
            ++out.depth;
            if (simple) {
                catchBody(tr->catches[0], caught, out);
            } else {
                bool first = true;
                bool all = false;
                for (auto& c : tr->catches) {
                    if (c.type.empty() || c.type == "Object" || c.type == "*") {
                        out.line(first ? "{" : "} else {");
                        all = true;
                    } else {
                        const std::string type = typeExpression(c.type);
                        out.line(std::string(first ? "if" : "} else if") + " ($.instanceOf($e, " + type + ")) {");
                    }
                    ++out.depth;
                    catchBody(c, "$e", out);
                    --out.depth;
                    first = false;
                    if (all) break;
                }
                if (!all) {
                    out.line("} else {");
                    out.line("    throw $e;");
                }
                out.line("}");
            }
            --out.depth;
        }
        if (tr->finallyBody) {
            out.line("} finally {");
            ++out.depth;
            block(tr->finallyBody.get(), out);
            --out.depth;
        }
        out.line("}");
    }

    void catchBody(CatchClause& c, const std::string& caught, Out& out) {
        if (c.variable) {
            const std::string name = F(c.variable->name);
            if (_fn->function) {
                if (localName(name) != caught) out.line("var " + localName(name) + " = " + caught + ";");
            } else {
                out.line(_scope + ".def(" + quote(name) + ", " + caught + ");");
            }
        }
        block(c.body.get(), out);
    }

    // A type named in a catch clause or after instanceof, as a value.
    std::string typeExpression(const std::string& type) {
        const std::string path = qualify(type);
        if (classes.count(path)) return classPath(path);
        if (path.find('.') != std::string::npos) return "$.qualified(" + quote(path) + ")";
        return loadName(type, 0).t;
    }

    // ---- Functions

    std::string function(const std::string& name, const std::vector<std::unique_ptr<Parameter>>& parameters, Statement* body,
                         int line, bool declaration = false, const std::string& prologue = std::string()) {
        Fn fn;
        fn.function = true;
        fn.outer = _fn;
        std::set<std::string> params;
        std::string list;
        for (const auto& p : parameters) {
            if (!p->name) continue;
            if (p->rest) fail(line, "rest parameters are not in ActionScript 2");
            params.insert(F(p->name->name));
            list += (list.empty() ? "" : ", ") + localName(F(p->name->name));
        }
        for (const std::string& n : declaredIn(parameters, body)) fn.locals.insert(F(n));
        if (usesName(body, "arguments")) fn.locals.insert("arguments");
        inferNumeric(fn, body, params);
        Fn* saved = _fn;
        _fn = &fn;
        const std::vector<std::string> savedWiths = _withs;
        Out out;
        out.depth = 1;
        if (!prologue.empty()) out.line(prologue);
        // Default values: not in ActionScript 2, but the parser keeps them.
        for (const auto& p : parameters) {
            if (p->name && p->defaultValue) {
                const std::string js = localName(F(p->name->name));
                out.line("if (" + js + " === undefined) " + js + " = " + expression(p->defaultValue.get()).t + ";");
            }
        }
        block(body, out);
        _fn = saved;
        _withs = savedWiths;
        const std::string head = "function" + (declaration || !name.empty() ? " " + localName(F(name)) : std::string()) + "(" + list + ") {";
        if (out.lines.empty()) return head + "}";
        return head + "\n" + out.text() + "\n}";
    }

    // ---- Classes

    void classBody(ClassDefinition* definition, Out& out) {
        const std::string& name = _className;
        const std::string shortName = name.substr(name.find_last_of('.') == std::string::npos ? 0 : name.find_last_of('.') + 1);
        const std::string self = localName(shortName);
        out.line("const $c = $.owner(" + quote(name) + ");");
        out.line("if ($c[0][$c[1]]) return;");
        if (definition->isInterface) {
            out.line("$c[0][$c[1]] = function " + self + "() {};");
            return;
        }
        FunctionDefinition* constructor = nullptr;
        for (auto& member : definition->members) {
            auto* fd = dynamic_cast<FunctionDefinition*>(member.get());
            if (fd && fd->name && fd->name->name == shortName && !hasAttribute(fd->attributes, AttributeType::Static)) constructor = fd;
        }
        if (!_superName.empty()) {
            out.line("const $super = " + typeExpression(_superName) + ";");
        }
        static const std::vector<std::unique_ptr<Parameter>> none;
        _inMethod = true;
        {
            std::string prologue;
            if (!_superName.empty() && !(constructor && callsSuper(constructor->body.get()))) prologue = "$super.call(this);";
            const std::string fn = function(shortName, constructor ? constructor->parameters : none,
                                            constructor ? constructor->body.get() : nullptr,
                                            constructor ? constructor->line : definition->line, false, prologue);
            out.line("const " + self + " = $c[0][$c[1]] = " + fn + ";");
        }
        if (!_superName.empty()) out.line("$.extend(" + self + ", $super);");
        std::string interfaces;
        for (const auto& iface : definition->interfaces) {
            if (iface) interfaces += (interfaces.empty() ? "" : ", ") + typeExpression(iface->name);
        }
        if (!interfaces.empty()) out.line("$.implement(" + self + ", [" + interfaces + "]);");
        out.line("const $p = " + self + ".prototype;");
        std::set<std::string> instanceAccessors, staticAccessors;
        for (auto& member : definition->members) {
            if (auto* fd = dynamic_cast<FunctionDefinition*>(member.get())) {
                if (fd == constructor || !fd->name || !fd->body) continue;
                const bool isStatic = hasAttribute(fd->attributes, AttributeType::Static);
                std::string method = fd->name->name;
                if (fd->isGetter || fd->isSetter) {
                    (isStatic ? staticAccessors : instanceAccessors).insert(method);
                    method = (fd->isGetter ? "__get__" : "__set__") + method;
                }
                const std::string on = isStatic ? self : "$p";
                out.line(on + member_(method) + " = " + function(std::string(), fd->parameters, fd->body.get(), fd->line) + ";");
            } else if (auto* vd = dynamic_cast<VariableDefinition*>(member.get())) {
                const bool isStatic = hasAttribute(vd->attributes, AttributeType::Static);
                for (auto& decl : vd->declarations) {
                    // Instance values go on the prototype, as ActionScript 2
                    // has them.
                    if (!decl->id || !decl->init) continue;
                    out.line((isStatic ? self : std::string("$p")) + member_(decl->id->name) + " = " + expression(decl->init.get()).t + ";");
                }
            } else if (auto* statement = dynamic_cast<Statement*>(member.get())) {
                if (!dynamic_cast<EmptyStatement*>(statement)) fail(statement->line, "a class has only variables and functions");
            }
        }
        for (const std::string& property : instanceAccessors) {
            out.line("$.accessor($p, " + quote(property) + ", $p.__get__" + property + ", $p.__set__" + property + ");");
        }
        for (const std::string& property : staticAccessors) {
            out.line("$.accessor(" + self + ", " + quote(property) + ", " + self + ".__get__" + property + ", " + self + ".__set__" + property + ");");
        }
        out.line("$.hide($p, null);");
        _inMethod = false;
    }

    static std::string member_(const std::string& name) {
        return isIdentifier(name) ? "." + name : "[" + quote(name) + "]";
    }

    // ---- Expressions

    static std::string wrap(const E& e) { return e.atom ? e.t : "(" + e.t + ")"; }

    // An operand as a number.
    std::string num(Expression* x) {
        const E e = expression(x);
        if (e.k == Kind::Num) return wrap(e);
        return "$.n(" + e.t + ")";
    }

    std::string str(Expression* x) {
        const E e = expression(x);
        if (e.k == Kind::Str) return wrap(e);
        return "$.s(" + e.t + ")";
    }

    // A test, as a JavaScript boolean.
    std::string condition(Expression* x) {
        const E e = expression(x);
        if (_options.swfVersion < 7 && e.k != Kind::Bool && e.k != Kind::Num) return "$.b(" + e.t + ")";
        return e.t;
    }

    // An expression whose value is not used.
    std::string discarded(Expression* x) {
        if (auto* u = dynamic_cast<UnaryExpression*>(x)) {
            if (u->op == OperatorType::PostIncrement || u->op == OperatorType::PreIncrement ||
                u->op == OperatorType::PostDecrement || u->op == OperatorType::PreDecrement) {
                return increment(u->operand.get(), u->op == OperatorType::PostIncrement || u->op == OperatorType::PreIncrement, false, true, u->line);
            }
        }
        if (auto* s = dynamic_cast<SequenceExpression*>(x)) {
            std::string out;
            for (auto& e : s->expressions) out += (out.empty() ? "" : ", ") + discarded(e.get());
            return out;
        }
        return expression(x).t;
    }

    E expression(Expression* x) {
        if (!x) return {"undefined"};
        const int line = x->line;
        switch (x->kind) {
            case Expression::ExpressionKind::Identifier: return loadName(static_cast<Identifier*>(x)->name, line);
            case Expression::ExpressionKind::Literal: return literal(static_cast<Literal*>(x));
            case Expression::ExpressionKind::This: return {"this"};
            case Expression::ExpressionKind::Binary: return binary(static_cast<BinaryExpression*>(x));
            case Expression::ExpressionKind::Unary: return unary(static_cast<UnaryExpression*>(x));
            case Expression::ExpressionKind::Conditional: {
                auto* c = static_cast<ConditionalExpression*>(x);
                const E a = expression(c->consequent.get()), b = expression(c->alternate.get());
                return {condition(c->test.get()) + " ? " + wrap(a) + " : " + wrap(b), a.k == b.k ? a.k : Kind::Any, false};
            }
            case Expression::ExpressionKind::Assignment: {
                auto* a = static_cast<AssignmentExpression*>(x);
                return {assign(a->op, a->left.get(), a->right.get(), a->line), kindOf(a), false};
            }
            case Expression::ExpressionKind::Call: return call(static_cast<CallExpression*>(x));
            case Expression::ExpressionKind::New: return construct(static_cast<NewExpression*>(x));
            case Expression::ExpressionKind::Member: return member(static_cast<MemberExpression*>(x));
            case Expression::ExpressionKind::Function: {
                auto* f = static_cast<FunctionExpression*>(x);
                return {function(f->name ? f->name->name : std::string(), f->parameters, f->body.get(), f->line), Kind::Any, false};
            }
            case Expression::ExpressionKind::Array: {
                auto* a = static_cast<ArrayExpression*>(x);
                std::string out;
                for (auto& e : a->elements) out += (out.empty() ? "" : ", ") + (e ? expression(e.get()).t : std::string("undefined"));
                return {"[" + out + "]"};
            }
            case Expression::ExpressionKind::Object: {
                auto* o = static_cast<ObjectExpression*>(x);
                std::string out;
                for (auto& p : o->properties) {
                    std::string key;
                    if (auto* id = dynamic_cast<Identifier*>(p.first.get())) key = F(id->name);
                    else if (auto* l = dynamic_cast<Literal*>(p.first.get())) key = l->kind == Literal::LiteralKind::Numeric ? numberText(l->numberValue) : l->stringValue;
                    out += (out.empty() ? "" : ", ") + (isIdentifier(key) ? key : quote(key)) + ": " + expression(p.second.get()).t;
                }
                return {out.empty() ? "{}" : "{" + out + "}"};
            }
            case Expression::ExpressionKind::Sequence: {
                auto* s = static_cast<SequenceExpression*>(x);
                std::string out;
                for (size_t i = 0; i < s->expressions.size(); ++i) {
                    out += (i ? ", " : "") + (i + 1 < s->expressions.size() ? discarded(s->expressions[i].get()) : expression(s->expressions[i].get()).t);
                }
                return {"(" + out + ")"};
            }
            case Expression::ExpressionKind::Super: fail(line, "super is for calls: super(...) and super.method(...)");
            case Expression::ExpressionKind::TypeApplication: fail(line, "Vector.<T> is not in ActionScript 2");
        }
        fail(line, "this expression is not in ActionScript 2");
    }

    E literal(Literal* l) {
        switch (l->kind) {
            case Literal::LiteralKind::Null: return {"null"};
            case Literal::LiteralKind::Undefined: return {"undefined"};
            case Literal::LiteralKind::Boolean: return {l->boolValue ? "true" : "false", Kind::Bool};
            case Literal::LiteralKind::Numeric: {
                const std::string text = numberText(l->numberValue);
                return {text, Kind::Num, text[0] != '-'};
            }
            case Literal::LiteralKind::String: return {quote(l->stringValue), Kind::Str};
            default: fail(l->line, "this literal is not in ActionScript 2");
        }
    }

    E binary(BinaryExpression* b) {
        Expression* l = b->left.get();
        Expression* r = b->right.get();
        const auto arithmetic = [&](const char* op) { return E{num(l) + " " + op + " " + num(r), Kind::Num, false}; };
        const auto bits = [&](const char* op) {
            return E{wrap(expression(l)) + " " + op + " " + wrap(expression(r)), Kind::Num, false};
        };
        switch (b->op) {
            case OperatorType::Add: {
                const Kind lk = kindOf(l), rk = kindOf(r);
                if (lk == Kind::Num && rk == Kind::Num) return {num(l) + " + " + num(r), Kind::Num, false};
                if (lk == Kind::Str || rk == Kind::Str) return {str(l) + " + " + str(r), Kind::Str, false};
                return {"$.add(" + expression(l).t + ", " + expression(r).t + ")"};
            }
            case OperatorType::Subtract: return arithmetic("-");
            case OperatorType::Multiply: return arithmetic("*");
            case OperatorType::Divide: return arithmetic("/");
            case OperatorType::Remainder: return arithmetic("%");
            case OperatorType::ShiftLeft: return bits("<<");
            case OperatorType::ShiftRight: return bits(">>");
            case OperatorType::ShiftRightUnsigned: return bits(">>>");
            case OperatorType::BitwiseAnd: return bits("&");
            case OperatorType::BitwiseOr: return bits("|");
            case OperatorType::BitwiseXor: return bits("^");
            case OperatorType::Lt:
            case OperatorType::Gt:
            case OperatorType::Le:
            case OperatorType::Ge: {
                const char* op = b->op == OperatorType::Lt ? "<" : b->op == OperatorType::Gt ? ">" : b->op == OperatorType::Le ? "<=" : ">=";
                const char* fn = b->op == OperatorType::Lt ? "lt" : b->op == OperatorType::Gt ? "gt" : b->op == OperatorType::Le ? "le" : "ge";
                if (kindOf(l) == Kind::Num && kindOf(r) == Kind::Num) return {num(l) + " " + op + " " + num(r), Kind::Bool, false};
                return {std::string("$.") + fn + "(" + expression(l).t + ", " + expression(r).t + ")"};
            }
            case OperatorType::Equals:
            case OperatorType::NotEquals: {
                const bool ne = b->op == OperatorType::NotEquals;
                const auto nothing = [](Expression* e) {
                    auto* lit = dynamic_cast<Literal*>(e);
                    if (lit) return lit->kind == Literal::LiteralKind::Null || lit->kind == Literal::LiteralKind::Undefined;
                    auto* id = dynamic_cast<Identifier*>(e);
                    return id && id->name == "undefined";
                };
                const Kind lk = kindOf(l), rk = kindOf(r);
                if (nothing(l) || nothing(r) || (lk == rk && lk != Kind::Any)) {
                    return {wrap(expression(l)) + (ne ? " != " : " == ") + wrap(expression(r)), Kind::Bool, false};
                }
                const std::string test = "$.eq(" + expression(l).t + ", " + expression(r).t + ")";
                return {ne ? "!" + test : test, Kind::Bool, !ne};
            }
            case OperatorType::StrictEquals: return {wrap(expression(l)) + " === " + wrap(expression(r)), Kind::Bool, false};
            case OperatorType::StrictNotEquals: return {wrap(expression(l)) + " !== " + wrap(expression(r)), Kind::Bool, false};
            case OperatorType::LogicalAnd:
            case OperatorType::LogicalOr: {
                const char* op = b->op == OperatorType::LogicalAnd ? " && " : " || ";
                if (_options.swfVersion < 7) {
                    // The value of the side that decides, tested AVM1's way.
                    const std::string t = temp();
                    const std::string left = expression(l).t, right = expression(r).t;
                    return {"((" + t + ") => " + (b->op == OperatorType::LogicalAnd ? "$.b(" + t + ") ? " + right + " : " + t
                                                                                     : "$.b(" + t + ") ? " + t + " : " + right) +
                                ")(" + left + ")"};
                }
                const E a = expression(l), c = expression(r);
                return {wrap(a) + op + wrap(c), a.k == c.k ? a.k : Kind::Any, false};
            }
            case OperatorType::Instanceof: return {"$.instanceOf(" + expression(l).t + ", " + expression(r).t + ")", Kind::Bool};
            case OperatorType::In: return {"$.has(" + expression(r).t + ", " + expression(l).t + ")", Kind::Bool};
            default: fail(b->line, "this operator is not in ActionScript 2");
        }
    }

    E unary(UnaryExpression* u) {
        Expression* x = u->operand.get();
        switch (u->op) {
            case OperatorType::Negative: return {"-" + num(x), Kind::Num, false};
            case OperatorType::Positive: return {num(x), Kind::Num, kindOf(x) == Kind::Num && expression(x).atom};
            case OperatorType::BitwiseNot: return {"~" + wrap(expression(x)), Kind::Num, false};
            case OperatorType::LogicalNot: return {"!" + (_options.swfVersion < 7 ? "$.b(" + expression(x).t + ")" : wrap(expression(x))), Kind::Bool, false};
            case OperatorType::Typeof: return {"$.typeOf(" + expression(x).t + ")", Kind::Str};
            case OperatorType::Void: return {"void " + wrap(expression(x)), Kind::Any, false};
            case OperatorType::Delete: return {remove(x, u->line), Kind::Bool};
            case OperatorType::PreIncrement:
            case OperatorType::PreDecrement:
            case OperatorType::PostIncrement:
            case OperatorType::PostDecrement: {
                const bool up = u->op == OperatorType::PreIncrement || u->op == OperatorType::PostIncrement;
                const bool post = u->op == OperatorType::PostIncrement || u->op == OperatorType::PostDecrement;
                return {increment(x, up, post, false, u->line), Kind::Num, false};
            }
            default: fail(u->line, "this operator is not in ActionScript 2");
        }
    }

    std::string remove(Expression* x, int line) {
        if (auto* id = dynamic_cast<Identifier*>(x)) {
            const std::string name = F(id->name);
            if (isLocal(name)) return "false";
            return _scope + ".del(" + quote(name) + ")";
        }
        if (auto* m = dynamic_cast<MemberExpression*>(x)) {
            return "$.del(" + expression(m->object.get()).t + ", " + key(m) + ")";
        }
        (void)line;
        return "true";
    }

    // A member's key as an expression.
    std::string key(MemberExpression* m) {
        if (!m->computed) return quote(F(m->property->name));
        Expression* k = m->computedProperty.get();
        const E e = expression(k);
        if (auto* l = dynamic_cast<Literal*>(k)) {
            if (l->kind == Literal::LiteralKind::String) return quote(F(l->stringValue));
            if (l->kind == Literal::LiteralKind::Numeric && l->numberValue == std::floor(l->numberValue)) return e.t;
        }
        if (_options.swfVersion < 7 || e.k != Kind::Num) return "$.key(" + e.t + ")";
        return e.t;
    }

    bool isThis(Expression* e) const { return dynamic_cast<ThisExpression*>(e) != nullptr; }

    // super.name: the superclass's prototype's.
    bool isSuperMember(MemberExpression* m) const { return dynamic_cast<SuperExpression*>(m->object.get()) != nullptr; }

    E member(MemberExpression* m) {
        if (isSuperMember(m)) {
            if (_superName.empty()) fail(m->line, "super is only for the methods of a class that extends another");
            return {"$super.prototype" + (m->computed ? "[" + key(m) + "]" : member_(F(m->property->name)))};
        }
        const E object = expression(m->object.get());
        const bool sure = isThis(m->object.get());
        const std::string dot = sure ? "" : "?.";
        if (m->computed) return {wrap(object) + dot + "[" + key(m) + "]"};
        const std::string name = F(m->property->name);
        if (isIdentifier(name)) return {wrap(object) + (sure ? "." : "?.") + name};
        return {wrap(object) + dot + "[" + quote(name) + "]"};
    }

    // target = value as an expression giving the value.
    std::string assignTo(Expression* target, const std::string& value, int line) {
        if (auto* id = dynamic_cast<Identifier*>(target)) return storeName(id->name, value, false);
        if (auto* m = dynamic_cast<MemberExpression*>(target)) {
            if (isSuperMember(m)) fail(line, "super's members are not assigned");
            if (isThis(m->object.get())) {
                return "this" + (m->computed ? "[" + key(m) + "]" : member_(F(m->property->name))) + " = " + value;
            }
            return "$.put(" + expression(m->object.get()).t + ", " + key(m) + ", " + value + ")";
        }
        fail(line, "this cannot be assigned to");
    }

    // What op= does to the old value.
    std::string combine(OperatorType op, const std::string& old, Kind oldKind, Expression* right, int line) {
        const E r = expression(right);
        const std::string n = oldKind == Kind::Num ? old : "$.n(" + old + ")";
        const std::string rn = r.k == Kind::Num ? wrap(r) : "$.n(" + r.t + ")";
        switch (op) {
            case OperatorType::Add:
                if (oldKind == Kind::Num && r.k == Kind::Num) return old + " + " + wrap(r);
                return "$.add(" + old + ", " + r.t + ")";
            case OperatorType::Subtract: return n + " - " + rn;
            case OperatorType::Multiply: return n + " * " + rn;
            case OperatorType::Divide: return n + " / " + rn;
            case OperatorType::Remainder: return n + " % " + rn;
            case OperatorType::ShiftLeft: return old + " << " + wrap(r);
            case OperatorType::ShiftRight: return old + " >> " + wrap(r);
            case OperatorType::ShiftRightUnsigned: return old + " >>> " + wrap(r);
            case OperatorType::BitwiseAnd: return old + " & " + wrap(r);
            case OperatorType::BitwiseOr: return old + " | " + wrap(r);
            case OperatorType::BitwiseXor: return old + " ^ " + wrap(r);
            default: fail(line, "this assignment is not in ActionScript 2");
        }
    }

    std::string assign(OperatorType op, Expression* target, Expression* right, int line) {
        if (op == OperatorType::Assign) return assignTo(target, expression(right).t, line);
        if (auto* id = dynamic_cast<Identifier*>(target)) {
            const E old = loadName(id->name, line);
            return storeName(id->name, combine(op, wrap(old), old.k, right, line), false);
        }
        if (auto* m = dynamic_cast<MemberExpression*>(target)) {
            if (isThis(m->object.get())) {
                const std::string place = "this" + (m->computed ? "[" + key(m) + "]" : member_(F(m->property->name)));
                if (!m->computed) return place + " = " + combine(op, place, Kind::Any, right, line);
            }
            return "$.update(" + expression(m->object.get()).t + ", " + key(m) + ", (v$) => " + combine(op, "v$", Kind::Any, right, line) + ")";
        }
        fail(line, "this cannot be assigned to");
    }

    // ++ and --: the new value, or with post the old, as a number.
    std::string increment(Expression* target, bool up, bool post, bool discard, int line) {
        const std::string delta = up ? " + 1" : " - 1";
        if (auto* id = dynamic_cast<Identifier*>(target)) {
            const std::string name = F(id->name);
            if (isNumericLocal(name)) return localName(name) + (up ? "++" : "--");
            const E old = loadName(name, line);
            const std::string store = storeName(name, (old.k == Kind::Num ? old.t : "$.n(" + old.t + ")") + delta, false);
            if (discard || !post) return discard ? store : "(" + store + ")";
            return "(" + store + ")" + (up ? " - 1" : " + 1");
        }
        if (auto* m = dynamic_cast<MemberExpression*>(target)) {
            const std::string call = "$.update(" + (isThis(m->object.get()) ? std::string("this") : expression(m->object.get()).t) + ", " + key(m) + ", (v$) => $.n(v$)" + delta + ")";
            if (discard || !post) return call;
            return call + (up ? " - 1" : " + 1");
        }
        fail(line, "this cannot be incremented");
    }

    std::string arguments(const ExpressionList& args, size_t from = 0) {
        std::string out;
        for (size_t i = from; i < args.size(); ++i) out += (out.empty() ? "" : ", ") + expression(args[i].get()).t;
        return out;
    }

    void need(const ExpressionList& args, size_t least, size_t most, const std::string& name, int line) {
        if (args.size() < least || args.size() > most) {
            fail(line, name + "() takes " + (least == most ? std::to_string(least) : std::to_string(least) + " to " + std::to_string(most)) +
                           " argument" + (most == 1 ? "" : "s"));
        }
    }

    // A property named as getProperty and setProperty name it: _x.
    std::string propertyName(Expression* e, int line) {
        if (auto* id = dynamic_cast<Identifier*>(e)) return quote(F(id->name));
        if (auto* l = dynamic_cast<Literal*>(e)) {
            if (l->kind == Literal::LiteralKind::String) return quote(l->stringValue);
            if (l->kind == Literal::LiteralKind::Numeric) {
                static const char* names[] = {"_x", "_y", "_xscale", "_yscale", "_currentframe", "_totalframes", "_alpha",
                    "_visible", "_width", "_height", "_rotation", "_target", "_framesloaded", "_name", "_droptarget", "_url",
                    "_highquality", "_focusrect", "_soundbuftime", "_quality", "_xmouse", "_ymouse"};
                const int i = static_cast<int>(l->numberValue);
                if (i >= 0 && i < 22) return quote(names[i]);
            }
        }
        fail(line, "getProperty() and setProperty() need a property such as _x");
    }

    // The global functions that are actions; empty when name is not one.
    std::string action(const std::string& name, const ExpressionList& args, int line) {
        const auto on = [&](const std::string& method, size_t count) {
            need(args, count, count, name, line);
            return _scope + "." + method + "(" + arguments(args) + ")";
        };
        const auto helper = [&](const std::string& fn, size_t count) {
            need(args, count, count, name, line);
            return "$." + fn + "(" + arguments(args) + ")";
        };
        if (name == "play" || name == "stop" || name == "nextFrame" || name == "prevFrame" || name == "nextScene" ||
            name == "prevScene" || name == "stopDrag") {
            return on(name, 0);
        }
        if (name == "gotoAndPlay" || name == "gotoAndStop") {
            need(args, 1, 2, name, line);
            const std::string play = name == "gotoAndPlay" ? "true" : "false";
            if (args.size() == 2) return _scope + ".goto(" + play + ", " + expression(args[1].get()).t + ", " + expression(args[0].get()).t + ")";
            return _scope + ".goto(" + play + ", " + expression(args[0].get()).t + (_scene.empty() ? "" : ", " + quote(_scene)) + ")";
        }
        if (name == "stopAllSounds") return helper("stopAllSounds", 0);
        if (name == "toggleHighQuality") return "undefined";
        if (name == "trace") return helper("trace", 1);
        if (name == "getTimer") return helper("getTimer", 0);
        if (name == "random") return helper("random", 1);
        if (name == "int") return helper("int", 1);
        if (name == "chr" || name == "mbchr") return "$.chr(" + (need(args, 1, 1, name, line), arguments(args)) + ")";
        if (name == "ord" || name == "mbord") return "$.ord(" + (need(args, 1, 1, name, line), arguments(args)) + ")";
        if ((name == "length" || name == "mblength") && args.size() == 1) return "$.length(" + arguments(args) + ")";
        if (name == "substring" || name == "mbsubstring") return "$.substring(" + (need(args, 3, 3, name, line), arguments(args)) + ")";
        if (name == "targetPath") return on("targetPath", 1);
        if (name == "eval") return on("eval", 1);
        if (name == "removeMovieClip") return on("removeMovieClip", 1);
        if (name == "call") return on("callFrame", 1);
        if (name == "set") {
            need(args, 2, 2, name, line);
            return _scope + ".setPath(" + arguments(args) + ")";
        }
        if (name == "Number" && args.size() == 1) return "$.n(" + arguments(args) + ")";
        if (name == "String" && args.size() == 1) return "$.s(" + arguments(args) + ")";
        if (name == "Boolean" && args.size() == 1) return "$.b(" + arguments(args) + ")";
        if (name == "getProperty") {
            need(args, 2, 2, name, line);
            return _scope + ".getProperty(" + expression(args[0].get()).t + ", " + propertyName(args[1].get(), line) + ")";
        }
        if (name == "setProperty") {
            need(args, 3, 3, name, line);
            return _scope + ".setProperty(" + expression(args[0].get()).t + ", " + propertyName(args[1].get(), line) + ", " +
                   expression(args[2].get()).t + ")";
        }
        if (name == "duplicateMovieClip") return on("duplicateMovieClip", 3);
        if (name == "startDrag") {
            need(args, 1, 6, name, line);
            if (args.size() != 1 && args.size() != 2 && args.size() != 6) {
                fail(line, "startDrag() takes a target, whether to lock the center, and the four sides of a box");
            }
            return _scope + ".startDrag(" + arguments(args) + ")";
        }
        if (name == "getURL") {
            need(args, 1, 3, name, line);
            return "$.getURL(" + arguments(args) + ")";
        }
        if (name == "fscommand") {
            need(args, 1, 2, name, line);
            return "$.fscommand(" + arguments(args) + ")";
        }
        if (name == "loadMovie" || name == "loadVariables") {
            need(args, 2, 3, name, line);
            return _scope + "." + name + "(" + arguments(args) + ")";
        }
        if (name == "loadMovieNum" || name == "loadVariablesNum") {
            need(args, 2, 3, name, line);
            return _scope + "." + name + "(" + arguments(args) + ")";
        }
        if (name == "unloadMovie" || name == "unloadMovieNum") return on(name, 1);
        if (name == "ifFrameLoaded") {
            need(args, 1, 2, name, line);
            return _scope + ".frameLoaded(" + arguments(args) + ")";
        }
        return std::string();
    }

    E call(CallExpression* c) {
        Expression* callee = c->callee.get();
        const int line = c->line;
        if (auto* id = dynamic_cast<Identifier*>(callee)) {
            const std::string name = F(id->name);
            if (isLocal(name)) {
                if (!_withs.empty()) return {"$.callValue(" + loadName(name, line).t + ", undefined, [" + arguments(c->arguments) + "])"};
                return {localName(name) + "(" + arguments(c->arguments) + ")"};
            }
            const std::string member = memberFor(name);
            if (!member.empty()) return {member + "(" + arguments(c->arguments) + ")"};
            const std::string act = action(name, c->arguments, line);
            if (!act.empty()) return {act, kindOf(c)};
            const std::string path = qualify(name);
            if (path != name) return {"$.callValue(" + loadName(name, line).t + ", undefined, [" + arguments(c->arguments) + "])"};
            return {_scope + ".call(" + quote(name) + ", [" + arguments(c->arguments) + "])"};
        }
        if (dynamic_cast<SuperExpression*>(callee)) {
            if (_superName.empty()) fail(line, "super() is only for a class that extends another");
            const std::string args = arguments(c->arguments);
            return {"$super.call(this" + (args.empty() ? "" : ", " + args) + ")"};
        }
        if (auto* m = dynamic_cast<MemberExpression*>(callee)) {
            const std::string args = arguments(c->arguments);
            if (isSuperMember(m)) {
                if (_superName.empty()) fail(line, "super is only for the methods of a class that extends another");
                return {"$super.prototype" + (m->computed ? "[" + key(m) + "]" : member_(F(m->property->name))) + ".call(this" + (args.empty() ? "" : ", " + args) + ")"};
            }
            if (!m->computed && (F(m->property->name) == "sort" || F(m->property->name) == "sortOn")) {
                return {"$." + F(m->property->name) + "(" + expression(m->object.get()).t + (args.empty() ? "" : ", " + args) + ")"};
            }
            const E object = expression(m->object.get());
            const bool sure = isThis(m->object.get()) || object.k == Kind::Str;
            if (!m->computed && isIdentifier(F(m->property->name))) {
                return {wrap(object) + (sure ? "." : "?.") + F(m->property->name) + "?.(" + args + ")"};
            }
            return {"$.callMethod(" + object.t + ", " + key(m) + ", [" + args + "])"};
        }
        return {"$.callValue(" + expression(callee).t + ", undefined, [" + arguments(c->arguments) + "])"};
    }

    E construct(NewExpression* n) {
        Expression* callee = n->callee.get();
        const std::string args = arguments(n->arguments);
        if (auto* id = dynamic_cast<Identifier*>(callee)) {
            const std::string name = F(id->name);
            if (!isLocal(name) && memberFor(name).empty() && qualify(name) == name &&
                (name == "Array" || name == "Object" || name == "Date" || name == "Error")) {
                return {"new " + name + "(" + args + ")"};
            }
        }
        return {"$.make(" + expression(callee).t + ", [" + args + "])"};
    }
};

} // namespace

Output translate(const Movie& movie, const Options& options) {
    Output output;
    Translator translator(options, output.errors);

    // The classes first, for their members; and every name, for their
    // spellings where case does not count.
    struct Parsed {
        std::unique_ptr<Program> program;
        std::string where;
    };
    std::vector<Parsed> classPrograms;
    for (const ClassFile& file : movie.classes) {
        Parsed parsed{translator.parse(file.source, file.fileName), file.fileName};
        if (!parsed.program) continue;
        for (auto& node : parsed.program->statements) {
            if (auto* definition = dynamic_cast<ClassDefinition*>(node.get())) {
                if (definition->name) translator.classes[definition->name->name] = Translator::membersOf(definition);
            }
        }
        classPrograms.push_back(std::move(parsed));
    }
    // A superclass named short, in the class's own package or imported.
    for (auto& [name, info] : translator.classes) {
        const size_t dot = name.find_last_of('.');
        if (info.superName.find('.') == std::string::npos && dot != std::string::npos &&
            translator.classes.count(name.substr(0, dot + 1) + info.superName)) {
            info.superName = name.substr(0, dot + 1) + info.superName;
        }
    }

    struct Frame {
        std::unique_ptr<Program> frame;
        std::unique_ptr<Program> init;
        int order = 0;
        const FrameScript* script = nullptr;
    };
    std::vector<Frame> frames;
    for (const FrameScript& script : movie.frames) {
        Frame f;
        f.script = &script;
        std::string frame, init;
        splitInitClip(script.source, frame, init, f.order);
        if (!blank(init)) f.init = translator.parse(init, script.where);
        if (!blank(frame)) f.frame = translator.parse(frame, script.where);
        frames.push_back(std::move(f));
    }
    std::vector<std::unique_ptr<Program>> instances;
    for (const InstanceScript& script : movie.instances) instances.push_back(translator.parse(script.source, script.where));

    if (options.swfVersion < 7) {
        translator.seedSpellings();
        for (Parsed& parsed : classPrograms) translator.gatherNames(parsed.program.get());
        for (Frame& f : frames) {
            if (f.init) translator.gatherNames(f.init.get());
            if (f.frame) translator.gatherNames(f.frame.get());
        }
        for (auto& program : instances) {
            if (program) translator.gatherNames(program.get());
        }
    }

    Out out;
    out.line("// ActionScript 1 and 2, translated to JavaScript by libas3.");
    out.line("avm1.movie(function ($, movie) {");
    out.depth = 1;
    out.line("const $g = $.global;");
    out.line("$.version = " + std::to_string(options.swfVersion) + ";");
    if (options.swfVersion < 7 && !translator.spellings.empty()) {
        std::string names;
        for (const auto& [lower, name] : translator.spellings) {
            if (lower != name) names += (names.empty() ? "" : ", ") + quote(lower) + ": " + quote(name);
        }
        out.line("Object.assign($.names, {" + names + "});");
    }

    if (!classPrograms.empty()) {
        out.line("movie.classes({");
        ++out.depth;
        for (Parsed& parsed : classPrograms) {
            std::string name;
            std::vector<std::string> needs;
            const std::string code = translator.classFile(parsed.program.get(), parsed.where, name, needs);
            if (code.empty()) continue;
            std::string list;
            for (const std::string& need : needs) list += (list.empty() ? "" : ", ") + quote(need);
            out.line(quote(name) + ": [[" + list + "], " + code + "],");
            output.classes.push_back(name);
        }
        --out.depth;
        out.line("});");
    }
    for (const auto& [linkage, cls] : movie.registrations) {
        out.line("movie.register(" + quote(linkage) + ", " + quote(cls) + ");");
    }
    for (Frame& f : frames) {
        if (!f.init) continue;
        const std::string code = translator.script(f.init.get(), f.script->where, std::string());
        if (code.empty()) continue;
        out.line("// " + f.script->where + ", #initclip");
        out.line("movie.initclip(" + quote(f.script->timeline) + ", " + std::to_string(f.order) + ", " + code + ");");
    }
    for (Frame& f : frames) {
        if (!f.frame) continue;
        const std::string code = translator.script(f.frame.get(), f.script->where, f.script->timeline.empty() ? f.script->scene : std::string());
        if (code.empty()) continue;
        out.line("// " + f.script->where);
        out.line("movie.frame(" + quote(f.script->timeline) + ", " + std::to_string(f.script->frame) + ", " + code + ");");
    }
    for (size_t i = 0; i < movie.instances.size(); ++i) {
        if (!instances[i]) continue;
        const std::string code = translator.handlers(instances[i].get(), movie.instances[i].where);
        if (code.empty()) continue;
        out.line("// " + movie.instances[i].where);
        out.line("movie.handlers(" + std::to_string(movie.instances[i].id) + ", " + code + ");");
    }
    out.depth = 0;
    out.line("});");
    output.code = out.text() + "\n";
    output.ok = output.errors.empty();
    return output;
}

namespace {

// avm1.js, as the build embeds it.
const unsigned char kAvm1[] = {
#include "avm1_js.inc"
    0};

} // namespace

const std::string& runtime() {
    static const std::string text(reinterpret_cast<const char*>(kAvm1), sizeof kAvm1 - 1);
    return text;
}

} // namespace as2
} // namespace js
} // namespace as3
