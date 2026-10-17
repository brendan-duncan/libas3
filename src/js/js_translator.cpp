#include "js.h"
#include "js_writer.h"
#include "../abc/abc_model.h"
#include "../abc/abc_player.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <memory>
#include <set>
#include <sstream>

namespace as3 {
namespace js {

std::string Output::errorText() const {
    std::string all;
    for (const Diagnostic& diagnostic : diagnostics) all += diagnostic.text() + "\n";
    return all;
}

namespace {

using abc::Access;
using abc::ClassModel;
using abc::Definition;
using abc::Member;
using abc::Model;
using abc::Scan;
using abc::SourceFile;
using abc::TypeName;
using abc::Unit;
using abc::packageOf;
using abc::shortOf;
using EK = Expression::ExpressionKind;
using OT = OperatorType;

// Static types, as far as the translator follows them.

struct Ty {
    enum Kind { Any, Void, Int, Uint, Number, Boolean, String, Null, Object, Instance, ClassValue, Function };
    Kind kind = Any;
    // Instance and ClassValue: qualified with dots ("Array", "XML",
    // "flash.display.Sprite", "__AS3__.vec.Vector").
    std::string name;
    ClassModel* cls = nullptr;
    // A Vector's elements.
    std::shared_ptr<Ty> element;

    static Ty of(Kind kind) {
        Ty type;
        type.kind = kind;
        return type;
    }
    static Ty instance(const std::string& name, ClassModel* cls = nullptr) {
        Ty type;
        type.kind = Instance;
        type.name = name;
        type.cls = cls;
        return type;
    }
    static Ty classValue(const std::string& name, ClassModel* cls = nullptr) {
        Ty type = instance(name, cls);
        type.kind = ClassValue;
        return type;
    }
    bool numeric() const { return kind == Int || kind == Uint || kind == Number; }
    bool primitive() const { return numeric() || kind == Boolean || kind == String; }
    bool is(const char* qualified) const { return kind == Instance && name == qualified; }
    bool isXml() const { return is("XML") || is("XMLList"); }
    bool isVector() const { return is("__AS3__.vec.Vector"); }
    bool isArrayLike() const { return is("Array") || isVector(); }
    bool isDictionary() const { return is("flash.utils.Dictionary"); }
};

// Precedences, as JavaScript has them.
enum Prec {
    P_Comma = 1,
    P_Assign = 2,
    P_Cond = 3,
    P_Coalesce = 4,
    P_Or = 5,
    P_And = 6,
    P_BitOr = 7,
    P_BitXor = 8,
    P_BitAnd = 9,
    P_Eq = 10,
    P_Rel = 11,
    P_Shift = 12,
    P_Add = 13,
    P_Mul = 14,
    P_Exp = 15,
    P_Unary = 16,
    P_Postfix = 17,
    P_Call = 19,
    P_Primary = 20,
};

// An expression as JavaScript, with its precedence and static type.
struct E {
    std::string s;
    int p = P_Primary;
    Ty t;
};

E make(const std::string& s, int p, Ty t = Ty()) {
    E e;
    e.s = s;
    e.p = p;
    e.t = std::move(t);
    return e;
}

std::string paren(const E& e, int min) {
    return e.p < min ? "(" + e.s + ")" : e.s;
}

// A member's property: obj.name, or obj["name"] when it is not a name.
std::string dotted(const std::string& object, const std::string& name) {
    return isIdentifier(name) ? object + "." + name : object + "[" + quote(name) + "]";
}

// The names flash.* classes have that a script class's private member must
// not take (DisplayObject's and the rest, EventDispatcher's, Object's).
const std::set<std::string>& runtimeMemberNames() {
    static const std::set<std::string> names = {
        // DisplayObject
        "x", "y", "z", "width", "height", "scaleX", "scaleY", "scaleZ", "rotation", "rotationX", "rotationY",
        "rotationZ", "alpha", "visible", "name", "parent", "root", "stage", "mask", "filters", "blendMode",
        "blendShader", "transform", "cacheAsBitmap", "cacheAsBitmapMatrix", "opaqueBackground", "scrollRect",
        "loaderInfo", "mouseX", "mouseY", "accessibilityProperties", "scale9Grid", "getBounds", "getRect",
        "globalToLocal", "localToGlobal", "globalToLocal3D", "local3DToGlobal", "hitTestObject", "hitTestPoint",
        // InteractiveObject
        "mouseEnabled", "doubleClickEnabled", "tabEnabled", "tabIndex", "focusRect", "contextMenu",
        "accessibilityImplementation", "needsSoftKeyboard", "softKeyboardInputAreaOfInterest",
        // DisplayObjectContainer
        "numChildren", "mouseChildren", "tabChildren", "textSnapshot", "addChild", "addChildAt", "removeChild",
        "removeChildAt", "removeChildren", "getChildAt", "getChildByName", "getChildIndex", "setChildIndex",
        "swapChildren", "swapChildrenAt", "contains", "areInaccessibleObjectsUnderPoint", "getObjectsUnderPoint",
        "stopAllMovieClips",
        // Sprite, MovieClip
        "graphics", "buttonMode", "useHandCursor", "hitArea", "dropTarget", "soundTransform", "startDrag",
        "stopDrag", "currentFrame", "totalFrames", "currentLabel", "currentFrameLabel", "currentLabels",
        "currentScene", "scenes", "framesLoaded", "enabled", "trackAsMenu", "isPlaying", "play", "stop",
        "gotoAndPlay", "gotoAndStop", "nextFrame", "prevFrame", "nextScene", "prevScene", "addFrameScript",
        // EventDispatcher, Event
        "addEventListener", "removeEventListener", "dispatchEvent", "hasEventListener", "willTrigger", "type",
        "target", "currentTarget", "bubbles", "cancelable", "eventPhase", "clone", "stopPropagation",
        "stopImmediatePropagation", "preventDefault", "isDefaultPrevented", "formatToString",
        // TextField, Bitmap, components
        "text", "htmlText", "length", "textColor", "bitmapData", "smoothing", "pixelSnapping", "value", "label",
        "selected", "selectedIndex", "selectedItem", "dataProvider", "setSize", "move", "drawNow", "data",
        // Object
        "toString", "valueOf", "hasOwnProperty", "isPrototypeOf", "propertyIsEnumerable", "toLocaleString",
        "constructor", "$construct",
    };
    return names;
}

// Package-level functions of the player's packages, found through their
// packages' wildcard imports (frame scripts have flash.*'s).
const char* playerFunctionPackage(const std::string& name) {
    static const std::map<std::string, const char*> functions = {
        {"getTimer", "flash.utils"}, {"setTimeout", "flash.utils"}, {"setInterval", "flash.utils"},
        {"clearTimeout", "flash.utils"}, {"clearInterval", "flash.utils"}, {"getDefinitionByName", "flash.utils"},
        {"getQualifiedClassName", "flash.utils"}, {"getQualifiedSuperclassName", "flash.utils"},
        {"describeType", "flash.utils"}, {"escapeMultiByte", "flash.utils"}, {"unescapeMultiByte", "flash.utils"},
        {"navigateToURL", "flash.net"}, {"sendToURL", "flash.net"}, {"registerClassAlias", "flash.net"},
        {"getClassByAlias", "flash.net"}, {"fscommand", "flash.system"},
    };
    auto found = functions.find(name);
    return found == functions.end() ? nullptr : found->second;
}

// The top level's names, as JavaScript has them.
std::string topLevelName(const std::string& name) {
    static const std::map<std::string, std::string> names = {
        {"trace", "as3.trace"}, {"int", "as3.int"}, {"uint", "as3.uint"}, {"Class", "as3.Class"},
        {"XML", "as3.XML"}, {"XMLList", "as3.XMLList"}, {"Namespace", "as3.Namespace"}, {"QName", "as3.QName"},
        {"Vector", "as3.Vector"}, {"isXMLName", "as3.isXMLName"}, {"Error", "as3.Error"},
        {"ArgumentError", "as3.ArgumentError"}, {"DefinitionError", "as3.DefinitionError"},
        {"EvalError", "as3.EvalError"}, {"RangeError", "as3.RangeError"}, {"ReferenceError", "as3.ReferenceError"},
        {"SecurityError", "as3.SecurityError"}, {"SyntaxError", "as3.SyntaxError"}, {"TypeError", "as3.TypeError"},
        {"URIError", "as3.URIError"}, {"VerifyError", "as3.VerifyError"},
        {"UninitializedError", "as3.UninitializedError"},
    };
    auto found = names.find(name);
    return found == names.end() ? name : found->second;
}

// Names XML's methods have, which x.name reads as a method rather than as
// the children called so.
bool isXmlMethod(const std::string& name) {
    static const std::set<std::string> names = {
        "addNamespace", "appendChild", "attribute", "attributes", "child", "childIndex", "children", "comments",
        "contains", "copy", "descendants", "elements", "hasComplexContent", "hasOwnProperty", "hasSimpleContent",
        "inScopeNamespaces", "insertChildAfter", "insertChildBefore", "length", "localName", "name", "namespace",
        "namespaceDeclarations", "nodeKind", "normalize", "parent", "prependChild", "processingInstructions",
        "propertyIsEnumerable", "removeNamespace", "replace", "setChildren", "setLocalName", "setName",
        "setNamespace", "text", "toString", "toXMLString", "valueOf",
    };
    return names.count(name) > 0;
}

// A comment of the source on a line of its own.
struct Comment {
    int line = 0;
    std::vector<std::string> text;
};

std::vector<Comment> scanComments(const std::string& source) {
    std::vector<Comment> comments;
    std::istringstream in(source);
    std::string line;
    int number = 0;
    Comment* open = nullptr;
    const auto trim = [](const std::string& text) {
        const size_t first = text.find_first_not_of(" \t\r");
        if (first == std::string::npos) return std::string();
        const size_t last = text.find_last_not_of(" \t\r");
        return text.substr(first, last - first + 1);
    };
    while (std::getline(in, line)) {
        ++number;
        std::string text = trim(line);
        if (open) {
            open->text.push_back(text.empty() || text[0] != '*' ? text : " " + text);
            if (text.find("*/") != std::string::npos) open = nullptr;
            continue;
        }
        if (text.compare(0, 2, "//") == 0) {
            comments.push_back({number, {text}});
        } else if (text.compare(0, 2, "/*") == 0) {
            comments.push_back({number, {text}});
            const size_t close = text.find("*/", 2);
            // Code after the comment's end: not a comment of its own line.
            if (close != std::string::npos) {
                if (close + 2 < text.size()) comments.pop_back();
            } else {
                open = &comments.back();
            }
        }
    }
    return comments;
}

// A local variable, parameter or nested function, as JavaScript names it.
struct Local {
    std::string js;
    Ty type;
};

// A function being translated.
struct Fn {
    enum Kind {
        Method,      // a class's method, getter, setter or constructor
        Static,      // a class's static method or initializer
        Frame,       // a frame's script: its variables are the timeline's
        Script,      // a file's code outside its packages
        PackageCode, // a package's function
        Arrow,       // a nested function, as an arrow function
        Plain,       // a nested function using this or arguments
    };
    Kind kind = Method;
    Fn* parent = nullptr;
    ClassModel* cls = nullptr;
    bool isStatic = false;
    bool isConstructor = false;
    std::map<std::string, Local> locals;
    // How each variable is declared: with let at its declaration, with
    // var there, or at the function's start (the declaration then an
    // assignment).
    std::map<std::string, char> declaration;   // 'l', 'v', 'h'
    std::set<std::string> declared;
    std::vector<std::string> hoistedLines;
    // Active with statements' objects, innermost last.
    std::vector<std::string> withs;
    Ty returnType;
    bool returnsValue = false;
    // $this (the method's this) used inside plain nested functions.
    bool needsThis = false;
    int temps = 0;
    // A named function expression's own name.
    std::string selfName;

    bool nested() const { return kind == Arrow || kind == Plain; }
};

// The bindings a definition's code needs: classes, functions and the
// player's names, by qualified name.
struct Bindings {
    // In the order first needed.
    std::vector<std::pair<std::string, std::string>> order;   // alias, expression
    std::map<std::string, std::string> byName;                 // key, alias
    std::set<std::string> taken;
};

class Translator {
public:
    explicit Translator(const Options& options) : options(options), model(modelOptions(options), output.diagnostics) {}

    Output run(const std::vector<Source>& sources, const std::vector<Timeline>& timelines);

private:
    static const abc::Options& modelOptions(const Options& options) {
        static thread_local abc::Options made;
        made.constants = options.constants;
        made.includeHandler = options.includeHandler;
        return made;
    }

    const Options& options;
    Output output;
    Model model;
    std::map<std::string, std::string> sourceTexts;

    // Where the translation is.
    SourceFile* file = nullptr;
    Unit* unit = nullptr;
    ClassModel* currentClass = nullptr;
    Fn* fn = nullptr;
    Bindings* bindings = nullptr;
    std::vector<Comment> comments;
    size_t commentAt = 0;
    std::vector<std::string> sourceLines;

    // Classes: their subclasses, private names, method definitions.
    std::map<const ClassModel*, std::vector<ClassModel*>> subclasses;
    std::map<std::pair<const ClassModel*, std::string>, std::string> privateNames;
    std::map<std::pair<const ClassModel*, std::string>, FunctionDefinition*> methods;

    // Diagnostics
    void error(const std::string& message, int line = -1) { model.error(message, line); }
    void warning(const std::string& message, int line = -1) { model.warning(message, line); }
    void unsupported(const std::string& construct, int line = -1) {
        error("cannot translate " + construct + " to JavaScript", line);
    }

    // Files
    std::string translateFile(SourceFile& source);
    std::string translateClass(ClassModel& cls);
    std::string translateInterface(ClassModel& cls);
    std::string translateTimeline(ClassModel& cls);
    std::string translatePackageFunction(FunctionDefinition* function, const Definition& definition);
    std::string translatePackageVariable(VariableDeclaration* decl, const Definition& definition, bool isConst);
    std::string definitionCall(const Definition& definition) const;
    std::string fileHeader(const std::string& sourceName) const;
    void flushComments(Lines& out, int line);

    // Names
    std::string bind(const std::string& qualified, ClassModel* source = nullptr, const Definition* definition = nullptr);
    std::string bindPlayer(const std::string& qualified) { return bind(qualified); }
    std::string classRef(ClassModel* cls);
    std::string bindingLines(const std::string& assign) const;
    std::string memberName(const ClassModel* owner, const std::string& name, const Member& member);
    const Member* findInstance(ClassModel* cls, const std::string& name, ClassModel** owner, bool fromInside);
    const Member* findStatic(ClassModel* cls, const std::string& name, ClassModel** owner, bool fromInside);
    bool hasRuntimeBase(const ClassModel* cls) const;
    FunctionDefinition* methodDefinition(const ClassModel* cls, const std::string& name, bool isStatic);
    Ty typeOf(const std::string& written, Unit* where = nullptr);
    Ty memberType(const ClassModel* owner, const std::string& name, const Member& member);
    std::string localName(const std::string& name) const;
    std::string thisRef();
    Fn* methodFn() const;
    ClassModel* contextClass() const;
    bool staticContext() const;
    std::string temp(const std::string& base);

    // Functions
    std::string functionText(const std::vector<std::unique_ptr<Parameter>>& params, Statement* body, Fn& f,
                             const std::string& head, bool arrow);
    void prepareLocals(Fn& f, const std::vector<std::unique_ptr<Parameter>>* params, Statement* body);
    void prepareTops(Fn& f, const std::vector<Statement*>& tops);
    // Code a function's start runs (a constructor's field values).
    std::function<void(Lines&)> prologueHook;
    std::string parameterList(const std::vector<std::unique_ptr<Parameter>>& params, Fn& f);
    void parameterCoercions(const std::vector<std::unique_ptr<Parameter>>& params, Fn& f, Lines& out);
    std::string defaultValue(const Ty& type) const;
    std::string literalValue(Expression* e) const;
    bool usesThisOrArguments(Statement* body, const std::vector<std::unique_ptr<Parameter>>& params);

    // Statements
    void statement(Statement* stmt, Lines& out);
    void statements(const std::vector<StatementPtr>& list, Lines& out) {
        for (const auto& s : list) statement(s.get(), out);
    }
    void block(Statement* stmt, Lines& out);
    std::string blockText(Statement* stmt);
    void variableDefinition(VariableDefinition* stmt, Lines& out);
    void forStatement(ForStatement* stmt, Lines& out, const std::string& label);
    void forInStatement(ForInStatement* stmt, Lines& out, const std::string& label);
    void tryStatement(TryStatement* stmt, Lines& out);
    void withStatement(WithStatement* stmt, Lines& out);
    void switchStatement(SwitchStatement* stmt, Lines& out, const std::string& label);
    std::string forInTarget(Statement* left, int line);

    // Expressions
    E expr(Expression* e);
    E exprAs(Expression* e, const Ty& target);
    E coerce(E value, const Ty& target);
    E identifier(Identifier* id);
    E literal(Literal* lit);
    E binary(BinaryExpression* e);
    E unary(UnaryExpression* e, bool discard);
    E assignment(AssignmentExpression* e, bool discard);
    E incDec(Expression* operand, bool increment, bool postfix, bool discard);
    E call(CallExpression* e);
    E construct(NewExpression* e);
    E member(MemberExpression* e, bool callee);
    E arrayLiteral(ArrayExpression* e);
    E objectLiteral(ObjectExpression* e);
    E functionExpression(FunctionExpression* e);
    E xmlFilter(CallExpression* e, Expression* predicate);
    E typeValue(const std::string& written);
    E typeOperand(Expression* e);
    std::string arguments(const ExpressionList& args, const std::vector<Ty>* types = nullptr);
    bool qualifiedPath(Expression* e, std::string& qualified);
    std::string numberLiteral(double value, int line) const;
    std::string discardText(Expression* e);

    // An lvalue: how to read it, write it, and its type.
    struct Place {
        std::string get;
        std::function<std::string(const std::string&)> set;
        Ty type;
        bool simple = true;   // reading it twice is safe
        bool error = false;
    };
    Place place(Expression* e);

    // Name resolution
    struct Ref {
        enum Kind { Local, Member, Static, Class, Function, Variable, Global, Unknown };
        Kind kind = Unknown;
        std::string text;
        Ty type;
        bool isMethod = false;
        // Member: the object and property, for binding and calls.
        std::string object;
        std::string property;
    };
    Ref resolve(const std::string& name);
    std::vector<std::string> activeWiths() const;
    std::string withRead(const std::string& name, const std::string& otherwise) const;
};

// Names

std::string Translator::localName(const std::string& name) const {
    if (isReserved(name) || name == "as3" || name == "$private") return name + "_";
    return name;
}

Fn* Translator::methodFn() const {
    Fn* f = fn;
    while (f && f->nested()) f = f->parent;
    return f;
}

ClassModel* Translator::contextClass() const {
    Fn* f = methodFn();
    return f ? f->cls : currentClass;
}

bool Translator::staticContext() const {
    Fn* f = methodFn();
    return !f || f->isStatic || f->kind == Fn::Script || f->kind == Fn::PackageCode;
}

std::string Translator::thisRef() {
    // In a plain nested function, this is its own: the method's is $this.
    for (Fn* f = fn; f; f = f->parent) {
        if (f->kind == Fn::Plain) {
            Fn* method = methodFn();
            if (method) method->needsThis = true;
            return "$this";
        }
        if (!f->nested()) break;
    }
    return "this";
}

std::string Translator::temp(const std::string& base) {
    Fn* f = methodFn();
    const int n = f ? ++f->temps : 1;
    return "$" + base + (n > 1 ? std::to_string(n) : std::string());
}

bool Translator::hasRuntimeBase(const ClassModel* cls) const {
    for (const ClassModel* c = cls; c; c = c->superClass) {
        if (!c->superClass) return !c->superName.empty() && c->superName != "Object";
    }
    return false;
}

std::string Translator::memberName(const ClassModel* owner, const std::string& name, const Member& member) {
    if (member.isStatic) {
        // A class's own properties that are not writable.
        if (name == "name" || name == "length" || name == "prototype" || name == "caller" || name == "arguments") {
            return name + "$";
        }
        return name;
    }
    if (member.access != Access::Private || !owner) return name;
    auto found = privateNames.find({owner, name});
    if (found != privateNames.end()) return found->second;
    // A private name another class of the chain has (a superclass's, a
    // subclass's, the player's) would be the same property: it takes its
    // class's name.
    bool collides = name == "constructor" || name == "$construct";
    for (const ClassModel* c = owner->superClass; c && !collides; c = c->superClass) {
        if (c->instance.count(name)) collides = true;
    }
    if (!collides && hasRuntimeBase(owner) && runtimeMemberNames().count(name)) collides = true;
    std::vector<const ClassModel*> pending = {owner};
    while (!collides && !pending.empty()) {
        const ClassModel* c = pending.back();
        pending.pop_back();
        auto children = subclasses.find(c);
        if (children == subclasses.end()) continue;
        for (const ClassModel* child : children->second) {
            if (child->instance.count(name)) collides = true;
            pending.push_back(child);
        }
    }
    const std::string made = collides ? name + "$" + owner->name : name;
    privateNames[{owner, name}] = made;
    return made;
}

const Member* Translator::findInstance(ClassModel* cls, const std::string& name, ClassModel** owner, bool fromInside) {
    for (ClassModel* c = cls; c; c = c->superClass) {
        auto found = c->instance.find(name);
        if (found == c->instance.end()) continue;
        if (found->second.access == Access::Private && fromInside && c != contextClass()) continue;
        if (owner) *owner = c;
        return &found->second;
    }
    return nullptr;
}

const Member* Translator::findStatic(ClassModel* cls, const std::string& name, ClassModel** owner, bool fromInside) {
    for (ClassModel* c = cls; c; c = c->superClass) {
        auto found = c->statics.find(name);
        if (found == c->statics.end()) continue;
        if (found->second.access == Access::Private && fromInside && c != contextClass()) continue;
        if (owner) *owner = c;
        return &found->second;
    }
    return nullptr;
}

FunctionDefinition* Translator::methodDefinition(const ClassModel* cls, const std::string& name, bool isStatic) {
    if (!cls) return nullptr;
    auto found = methods.find({cls, (isStatic ? "static " : "") + name});
    if (found != methods.end()) return found->second;
    FunctionDefinition* definition = nullptr;
    if (cls->ast) {
        for (auto& node : cls->ast->members) {
            auto* function = dynamic_cast<FunctionDefinition*>(node.get());
            if (function && function->name->name == name &&
                hasAttribute(function->attributes, AttributeType::Static) == isStatic) {
                definition = function;
                if (!function->isSetter) break;
            }
        }
    }
    methods[{cls, (isStatic ? "static " : "") + name}] = definition;
    return definition;
}

Ty Translator::typeOf(const std::string& written, Unit* where) {
    if (written.empty() || written == "*") return Ty();
    Unit* in = where ? where : unit;
    if (!in) return Ty();
    const TypeName type = model.resolveType(written, in, true);
    std::function<Ty(const TypeName&)> convert = [&](const TypeName& t) -> Ty {
        switch (t.kind) {
            case TypeName::Any: return Ty();
            case TypeName::Void: return Ty::of(Ty::Void);
            case TypeName::Int: return Ty::of(Ty::Int);
            case TypeName::Uint: return Ty::of(Ty::Uint);
            case TypeName::Number: return Ty::of(Ty::Number);
            case TypeName::Boolean: return Ty::of(Ty::Boolean);
            case TypeName::String: return Ty::of(Ty::String);
            case TypeName::Object: return Ty::of(Ty::Object);
            case TypeName::Class:
                if (t.qualified == "Function") return Ty::of(Ty::Function);
                return Ty::instance(t.qualified, t.definition && t.definition->kind == Definition::Class ? t.definition->cls
                                                                                                      : nullptr);
            case TypeName::Vector: {
                Ty vector = Ty::instance("__AS3__.vec.Vector");
                vector.element = std::make_shared<Ty>(convert(*t.element));
                return vector;
            }
        }
        return Ty();
    };
    return convert(type);
}

Ty Translator::memberType(const ClassModel* owner, const std::string& name, const Member& member) {
    if (member.kind == Member::Method) return Ty::of(Ty::Function);
    (void)name;
    return typeOf(member.type, owner ? owner->unit : nullptr);
}

std::string Translator::bind(const std::string& qualified, ClassModel* source, const Definition* definition) {
    // A file's own definitions outside its packages come from its private
    // scope, the rest by their qualified names.
    const bool outside = (source && source->outside) || (definition && definition->outside);
    const std::string key = (outside ? "private:" : "") + qualified;
    auto found = bindings->byName.find(key);
    if (found != bindings->byName.end()) return found->second;
    std::string alias = shortOf(qualified);
    // Names the translated code keeps for JavaScript's own, and its own.
    static const std::set<std::string> kept = {
        "Object", "Array", "String", "Number", "Boolean", "Math", "Date", "RegExp", "JSON", "Function", "Symbol",
        "Map", "Set", "Proxy", "Reflect", "Promise", "console", "globalThis", "undefined", "NaN", "Infinity",
        "isNaN", "isFinite", "parseInt", "parseFloat", "as3", "$private", "$this", "args", "self",
    };
    if (!isIdentifier(alias) || isReserved(alias) || kept.count(alias) || bindings->taken.count(alias)) {
        std::string made;
        for (char c : qualified) made += (c == '.' || c == ':' || c == '-') ? '_' : c;
        alias = made;
        while (bindings->taken.count(alias)) alias += "_";
    }
    bindings->taken.insert(alias);
    bindings->byName[key] = alias;
    const std::string how = outside ? "$private.use(" + quote(shortOf(qualified)) + ")" : "as3.use(" + quote(qualified) + ")";
    bindings->order.emplace_back(alias, how);
    return alias;
}

std::string Translator::classRef(ClassModel* cls) {
    if (cls == currentClass && currentClass) return cls->name;
    return bind(cls->qualified(), cls);
}

std::string Translator::bindingLines(const std::string& assign) const {
    // "let A, B;" or the assignments, a line each.
    std::string out;
    if (assign.empty()) {
        for (size_t i = 0; i < bindings->order.size(); ++i) {
            out += (i ? ", " : "let ") + bindings->order[i].first;
        }
        if (!out.empty()) out += ";";
        return out;
    }
    for (const auto& binding : bindings->order) {
        if (!out.empty()) out += "\n";
        out += binding.first + " = " + binding.second + ";";
    }
    return out;
}

std::vector<std::string> Translator::activeWiths() const {
    std::vector<std::string> all;
    for (Fn* f = fn; f; f = f->parent) {
        for (size_t i = f->withs.size(); i-- > 0;) all.push_back(f->withs[i]);
    }
    return all;
}

std::string Translator::withRead(const std::string& name, const std::string& otherwise) const {
    std::string out = otherwise;
    const std::vector<std::string> withs = activeWiths();
    for (size_t i = withs.size(); i-- > 0;) {
        out = "as3.has(" + withs[i] + ", " + quote(name) + ") ? " + dotted(withs[i], name) + " : " + out;
    }
    return out;
}

Translator::Ref Translator::resolve(const std::string& name) {
    Ref ref;
    // Locals, here and in the functions around.
    for (Fn* f = fn; f; f = f->parent) {
        auto found = f->locals.find(name);
        if (found != f->locals.end()) {
            ref.kind = Ref::Local;
            ref.text = found->second.js;
            ref.type = found->second.type;
            return ref;
        }
        if (!f->nested()) break;
    }
    ClassModel* cls = contextClass();
    if (cls) {
        ClassModel* owner = nullptr;
        if (!staticContext()) {
            if (const Member* member = findInstance(cls, name, &owner, true)) {
                ref.kind = Ref::Member;
                ref.object = thisRef();
                ref.property = memberName(owner, name, *member);
                ref.text = dotted(ref.object, ref.property);
                ref.type = memberType(owner, name, *member);
                ref.isMethod = member->kind == Member::Method;
                return ref;
            }
        }
        if (const Member* member = findStatic(cls, name, &owner, true)) {
            ref.kind = Ref::Static;
            ref.object = classRef(owner);
            ref.property = memberName(owner, name, *member);
            ref.text = dotted(ref.object, ref.property);
            ref.type = memberType(owner, name, *member);
            return ref;
        }
    }
    if (unit) {
        std::string package;
        if (const Definition* definition = model.findDefinition(unit, name, &package)) {
            switch (definition->kind) {
                case Definition::Class:
                    ref.kind = Ref::Class;
                    ref.text = classRef(definition->cls);
                    ref.type = Ty::classValue(definition->cls->qualified(), definition->cls);
                    return ref;
                case Definition::Function:
                    ref.kind = Ref::Function;
                    ref.type = Ty::of(Ty::Function);
                    if (definition->outside) {
                        ref.text = localName(name);
                    } else {
                        ref.text = bind(definition->package.empty() ? name : definition->package + "." + name, nullptr,
                                        definition);
                    }
                    return ref;
                case Definition::Variable:
                    ref.kind = Ref::Variable;
                    ref.type = typeOf(definition->type, unit);
                    if (definition->outside) {
                        ref.text = localName(name);
                    } else {
                        ref.text = dotted("as3.package(" + quote(definition->package) + ")", name);
                    }
                    return ref;
            }
        }
        auto imported = unit->imports.find(name);
        if (imported != unit->imports.end()) {
            ref.kind = Ref::Class;
            ref.text = bind(imported->second);
            ref.type = Ty::classValue(imported->second);
            if (imported->second == "flash.utils.Dictionary") ref.type = Ty::classValue("flash.utils.Dictionary");
            const std::string& q = imported->second;
            if (!abc::findPlayerClass(packageOf(q), shortOf(q))) {
                if (playerFunctionPackage(shortOf(q))) {
                    ref.kind = Ref::Function;
                    ref.type = Ty::of(Ty::Function);
                }
            }
            return ref;
        }
    }
    if (abc::isTopLevelName(name)) {
        ref.kind = Ref::Global;
        ref.text = topLevelName(name);
        static const std::map<std::string, Ty::Kind> values = {
            {"NaN", Ty::Number}, {"Infinity", Ty::Number}, {"undefined", Ty::Any},
        };
        auto value = values.find(name);
        if (value != values.end()) {
            ref.type = Ty::of(value->second);
        } else if (abc::findPlayerClass("", name)) {
            ref.type = Ty::classValue(name);
        } else {
            ref.type = Ty::of(Ty::Function);
        }
        return ref;
    }
    if (unit) {
        for (const std::string& wildcard : unit->wildcards) {
            if (abc::findPlayerClass(wildcard, name)) {
                ref.kind = Ref::Class;
                ref.text = bind(wildcard + "." + name);
                ref.type = Ty::classValue(wildcard + "." + name);
                return ref;
            }
        }
        if (const char* package = playerFunctionPackage(name)) {
            if (std::find(unit->wildcards.begin(), unit->wildcards.end(), std::string(package)) != unit->wildcards.end()) {
                ref.kind = Ref::Function;
                ref.text = bind(std::string(package) + "." + name);
                ref.type = Ty::of(Ty::Function);
                return ref;
            }
        }
    }
    // Not known here: an inherited member of the player's class, or a
    // property found when it runs.
    if (cls && !staticContext() && (hasRuntimeBase(cls) || cls->isDynamic)) {
        ref.kind = Ref::Member;
        ref.object = thisRef();
        ref.property = name;
        ref.text = dotted(ref.object, name);
        return ref;
    }
    ref.kind = Ref::Unknown;
    ref.text = localName(name);
    if (activeWiths().empty() && name != "$item") {
        warning("'" + name + "' is not known here; translated as a global name");
    }
    return ref;
}


// Functions

namespace {

std::vector<Statement*> topsOf(Statement* body);

// A variable's declaration in a function, outside its nested functions.
struct Declaration {
    std::string name;
    std::string type;
    bool init = false;
    // The function body's statement it is in, and whether it is that
    // statement (a var at the top) or in a loop's head there.
    size_t top = 0;
    bool atTop = false;
    bool inLoopHead = false;
    bool isConst = false;
};

void collectDeclarations(Statement* stmt, size_t top, bool atTop, std::vector<Declaration>& out,
                         std::vector<FunctionDefinition*>& functions) {
    if (!stmt) return;
    if (auto* variables = dynamic_cast<VariableDefinition*>(stmt)) {
        for (auto& decl : variables->declarations) {
            Declaration d;
            d.name = decl->id->name;
            d.type = decl->type ? decl->type->name : std::string();
            d.init = decl->init != nullptr;
            d.top = top;
            d.atTop = atTop;
            d.isConst = variables->isConst;
            out.push_back(d);
        }
    } else if (auto* function = dynamic_cast<FunctionDefinition*>(stmt)) {
        functions.push_back(function);
    } else if (auto* block = dynamic_cast<BlockStatement*>(stmt)) {
        for (auto& s : block->statements) collectDeclarations(s.get(), top, false, out, functions);
    } else if (auto* branch = dynamic_cast<IfStatement*>(stmt)) {
        collectDeclarations(branch->consequent.get(), top, false, out, functions);
        collectDeclarations(branch->alternate.get(), top, false, out, functions);
    } else if (auto* loop = dynamic_cast<WhileStatement*>(stmt)) {
        collectDeclarations(loop->body.get(), top, false, out, functions);
    } else if (auto* doLoop = dynamic_cast<DoWhileStatement*>(stmt)) {
        collectDeclarations(doLoop->body.get(), top, false, out, functions);
    } else if (auto* forLoop = dynamic_cast<ForStatement*>(stmt)) {
        const size_t before = out.size();
        collectDeclarations(forLoop->init.get(), top, false, out, functions);
        for (size_t i = before; i < out.size(); ++i) out[i].inLoopHead = atTop;
        collectDeclarations(forLoop->body.get(), top, false, out, functions);
    } else if (auto* forIn = dynamic_cast<ForInStatement*>(stmt)) {
        const size_t before = out.size();
        collectDeclarations(forIn->left.get(), top, false, out, functions);
        for (size_t i = before; i < out.size(); ++i) out[i].inLoopHead = atTop;
        collectDeclarations(forIn->body.get(), top, false, out, functions);
    } else if (auto* choice = dynamic_cast<SwitchStatement*>(stmt)) {
        for (auto& clause : choice->cases) {
            for (auto& s : clause->consequent) collectDeclarations(s.get(), top, false, out, functions);
        }
    } else if (auto* labeled = dynamic_cast<LabeledStatement*>(stmt)) {
        collectDeclarations(labeled->body.get(), top, atTop, out, functions);
    } else if (auto* attempt = dynamic_cast<TryStatement*>(stmt)) {
        collectDeclarations(attempt->block.get(), top, false, out, functions);
        for (auto& clause : attempt->catches) collectDeclarations(clause.body.get(), top, false, out, functions);
        collectDeclarations(attempt->finallyBody.get(), top, false, out, functions);
    } else if (auto* with = dynamic_cast<WithStatement*>(stmt)) {
        collectDeclarations(with->body.get(), top, false, out, functions);
    }
}

// Whether code uses this or arguments of its own (not its nested
// functions').
bool usesOwnThis(Expression* e);
bool usesOwnThis(Statement* s);

bool usesOwnThis(Expression* e) {
    if (!e) return false;
    switch (e->kind) {
        case EK::This: return true;
        case EK::Identifier: return static_cast<Identifier*>(e)->name == "arguments";
        case EK::Function: return false;
        case EK::Binary: {
            Expression* at = e;
            while (at && at->kind == EK::Binary) {
                auto* binary = static_cast<BinaryExpression*>(at);
                if (usesOwnThis(binary->right.get())) return true;
                at = binary->left.get();
            }
            return usesOwnThis(at);
        }
        case EK::Unary: return usesOwnThis(static_cast<UnaryExpression*>(e)->operand.get());
        case EK::Conditional: {
            auto* c = static_cast<ConditionalExpression*>(e);
            return usesOwnThis(c->test.get()) || usesOwnThis(c->consequent.get()) || usesOwnThis(c->alternate.get());
        }
        case EK::Assignment: {
            auto* a = static_cast<AssignmentExpression*>(e);
            return usesOwnThis(a->left.get()) || usesOwnThis(a->right.get());
        }
        case EK::Call: {
            auto* c = static_cast<CallExpression*>(e);
            if (Expression* predicate = abc::filterPredicate(c)) {
                return usesOwnThis(c->arguments[0].get()) || usesOwnThis(predicate);
            }
            if (usesOwnThis(c->callee.get())) return true;
            for (auto& arg : c->arguments) {
                if (usesOwnThis(arg.get())) return true;
            }
            return false;
        }
        case EK::New: {
            auto* n = static_cast<NewExpression*>(e);
            if (usesOwnThis(n->callee.get())) return true;
            for (auto& arg : n->arguments) {
                if (usesOwnThis(arg.get())) return true;
            }
            return false;
        }
        case EK::Member: {
            auto* m = static_cast<MemberExpression*>(e);
            return usesOwnThis(m->object.get()) || (m->computed && usesOwnThis(m->computedProperty.get()));
        }
        case EK::Array:
            for (auto& element : static_cast<ArrayExpression*>(e)->elements) {
                if (usesOwnThis(element.get())) return true;
            }
            return false;
        case EK::Object:
            for (auto& property : static_cast<ObjectExpression*>(e)->properties) {
                if (usesOwnThis(property.first.get()) || usesOwnThis(property.second.get())) return true;
            }
            return false;
        case EK::Sequence:
            for (auto& part : static_cast<SequenceExpression*>(e)->expressions) {
                if (usesOwnThis(part.get())) return true;
            }
            return false;
        default:
            return false;
    }
}

bool usesOwnThis(Statement* stmt) {
    if (!stmt) return false;
    if (auto* block = dynamic_cast<BlockStatement*>(stmt)) {
        for (auto& s : block->statements) {
            if (usesOwnThis(s.get())) return true;
        }
    } else if (auto* expression = dynamic_cast<ExpressionStatement*>(stmt)) {
        return usesOwnThis(expression->expression.get());
    } else if (auto* variables = dynamic_cast<VariableDefinition*>(stmt)) {
        for (auto& decl : variables->declarations) {
            if (usesOwnThis(decl->init.get())) return true;
        }
    } else if (auto* branch = dynamic_cast<IfStatement*>(stmt)) {
        return usesOwnThis(branch->test.get()) || usesOwnThis(branch->consequent.get()) ||
               usesOwnThis(branch->alternate.get());
    } else if (auto* loop = dynamic_cast<WhileStatement*>(stmt)) {
        return usesOwnThis(loop->test.get()) || usesOwnThis(loop->body.get());
    } else if (auto* doLoop = dynamic_cast<DoWhileStatement*>(stmt)) {
        return usesOwnThis(doLoop->test.get()) || usesOwnThis(doLoop->body.get());
    } else if (auto* forLoop = dynamic_cast<ForStatement*>(stmt)) {
        return usesOwnThis(forLoop->init.get()) || usesOwnThis(forLoop->test.get()) ||
               usesOwnThis(forLoop->update.get()) || usesOwnThis(forLoop->body.get());
    } else if (auto* forIn = dynamic_cast<ForInStatement*>(stmt)) {
        return usesOwnThis(forIn->left.get()) || usesOwnThis(forIn->right.get()) || usesOwnThis(forIn->body.get());
    } else if (auto* choice = dynamic_cast<SwitchStatement*>(stmt)) {
        if (usesOwnThis(choice->discriminant.get())) return true;
        for (auto& clause : choice->cases) {
            if (usesOwnThis(clause->test.get())) return true;
            for (auto& s : clause->consequent) {
                if (usesOwnThis(s.get())) return true;
            }
        }
    } else if (auto* labeled = dynamic_cast<LabeledStatement*>(stmt)) {
        return usesOwnThis(labeled->body.get());
    } else if (auto* attempt = dynamic_cast<TryStatement*>(stmt)) {
        if (usesOwnThis(attempt->block.get()) || usesOwnThis(attempt->finallyBody.get())) return true;
        for (auto& clause : attempt->catches) {
            if (usesOwnThis(clause.body.get())) return true;
        }
    } else if (auto* result = dynamic_cast<ReturnStatement*>(stmt)) {
        return usesOwnThis(result->argument.get());
    } else if (auto* thrown = dynamic_cast<ThrowStatement*>(stmt)) {
        return usesOwnThis(thrown->argument.get());
    } else if (auto* with = dynamic_cast<WithStatement*>(stmt)) {
        return usesOwnThis(with->object.get()) || usesOwnThis(with->body.get());
    }
    return false;
}

} // namespace

bool Translator::usesThisOrArguments(Statement* body, const std::vector<std::unique_ptr<Parameter>>& params) {
    for (const auto& param : params) {
        if (usesOwnThis(param->defaultValue.get())) return true;
    }
    return usesOwnThis(body);
}

std::string Translator::defaultValue(const Ty& type) const {
    switch (type.kind) {
        case Ty::Int:
        case Ty::Uint: return "0";
        case Ty::Number: return "NaN";
        case Ty::Boolean: return "false";
        case Ty::Any: return "undefined";
        default: return "null";
    }
}

std::string Translator::literalValue(Expression* e) const {
    // A literal's JavaScript, for a value that needs no code to make.
    if (!e) return std::string();
    if (e->kind == EK::Literal) {
        auto* lit = static_cast<Literal*>(e);
        switch (lit->kind) {
            case Literal::LiteralKind::Null: return "null";
            case Literal::LiteralKind::Undefined: return "undefined";
            case Literal::LiteralKind::Boolean: return lit->boolValue ? "true" : "false";
            case Literal::LiteralKind::Numeric: return numberLiteral(lit->numberValue, e->line);
            case Literal::LiteralKind::String: return quote(lit->stringValue);
            default: return std::string();
        }
    }
    if (e->kind == EK::Unary) {
        auto* u = static_cast<UnaryExpression*>(e);
        if (u->op == OT::Negative && u->operand && u->operand->kind == EK::Literal &&
            static_cast<Literal*>(u->operand.get())->kind == Literal::LiteralKind::Numeric) {
            return "-" + literalValue(u->operand.get());
        }
    }
    if (e->kind == EK::Identifier) {
        const std::string& name = static_cast<Identifier*>(e)->name;
        if (name == "NaN" || name == "Infinity" || name == "undefined") return name;
    }
    return std::string();
}

std::string Translator::numberLiteral(double value, int line) const {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value < 0 ? "-Infinity" : "Infinity";
    // Written in hexadecimal on its line (a color): kept so.
    if (value >= 10 && value == std::floor(value) && value <= 4294967295.0 && line > 0 &&
        static_cast<size_t>(line) <= sourceLines.size()) {
        const std::string& text = sourceLines[static_cast<size_t>(line) - 1];
        for (size_t at = text.find("0x"); at != std::string::npos; at = text.find("0x", at + 2)) {
            size_t end = at + 2;
            while (end < text.size() && std::isxdigit(static_cast<unsigned char>(text[end]))) ++end;
            if (end > at + 2 && std::strtoull(text.substr(at + 2, end - at - 2).c_str(), nullptr, 16) ==
                                    static_cast<unsigned long long>(value)) {
                return "0x" + text.substr(at + 2, end - at - 2);
            }
        }
        for (size_t at = text.find("0X"); at != std::string::npos; at = text.find("0X", at + 2)) {
            size_t end = at + 2;
            while (end < text.size() && std::isxdigit(static_cast<unsigned char>(text[end]))) ++end;
            if (end > at + 2 && std::strtoull(text.substr(at + 2, end - at - 2).c_str(), nullptr, 16) ==
                                    static_cast<unsigned long long>(value)) {
                return "0x" + text.substr(at + 2, end - at - 2);
            }
        }
    }
    return abc::numberText(value);
}

void Translator::prepareLocals(Fn& f, const std::vector<std::unique_ptr<Parameter>>* params, Statement* body) {
    if (params) {
        for (const auto& param : *params) {
            Local local;
            local.js = localName(param->name->name);
            local.type = param->rest ? Ty::instance("Array") : typeOf(param->type ? param->type->name : std::string());
            f.locals[param->name->name] = local;
            f.declaration[param->name->name] = 'a';
        }
    }
    if (!f.selfName.empty() && !f.locals.count(f.selfName)) {
        f.locals[f.selfName] = Local{localName(f.selfName), Ty::of(Ty::Function)};
        f.declaration[f.selfName] = 'a';
    }
    prepareTops(f, topsOf(body));
}

void Translator::prepareTops(Fn& f, const std::vector<Statement*>& tops) {
    std::vector<Declaration> decls;
    std::vector<FunctionDefinition*> functions;
    for (size_t i = 0; i < tops.size(); ++i) collectDeclarations(tops[i], i, true, decls, functions);
    for (FunctionDefinition* function : functions) {
        const std::string& name = function->name->name;
        if (f.kind == Fn::Frame && f.cls && f.cls->instance.count(name)) continue;
        f.locals[name] = Local{localName(name), Ty::of(Ty::Function)};
        f.declaration[name] = 'f';
    }
    // What nested functions use, and what each statement at the top uses.
    std::set<std::string> captured;
    std::vector<std::set<std::string>> topRefs;
    for (Statement* s : tops) {
        Scan scan;
        scanStatement(s, scan);
        captured.insert(scan.innerFree.begin(), scan.innerFree.end());
        std::set<std::string> refs = scan.refs;
        refs.insert(scan.innerFree.begin(), scan.innerFree.end());
        topRefs.push_back(refs);
    }
    std::map<std::string, std::vector<const Declaration*>> byName;
    std::vector<std::string> order;
    for (const Declaration& d : decls) {
        if (!byName.count(d.name)) order.push_back(d.name);
        byName[d.name].push_back(&d);
    }
    std::string hoisted;
    for (const std::string& name : order) {
        const auto& list = byName[name];
        const Declaration& first = *list.front();
        if (f.kind == Fn::Frame && f.cls && f.cls->instance.count(name)) continue;
        // Two declarations of different types are untyped.
        std::string type = first.type;
        for (const Declaration* d : list) {
            if (d->type != type) type = "*";
        }
        const Ty ty = typeOf(type);
        if (f.declaration.count(name)) {
            // A parameter or function declared again: assignments.
            f.locals[name].type = f.locals[name].type.kind == Ty::Any ? ty : f.locals[name].type;
            continue;
        }
        f.locals[name] = Local{localName(name), ty};
        if (f.kind == Fn::Script) {
            f.declaration[name] = 'h';
            continue;
        }
        bool usedBefore = false;
        for (size_t i = 0; i < first.top && i < topRefs.size(); ++i) {
            if (topRefs[i].count(name)) usedBefore = true;
        }
        bool usedAfter = false;
        for (size_t i = first.top + 1; i < topRefs.size(); ++i) {
            if (topRefs[i].count(name)) usedAfter = true;
        }
        if (list.size() == 1 && !usedBefore && !captured.count(name) &&
            (first.atTop || (first.inLoopHead && !usedAfter))) {
            f.declaration[name] = 'l';
        } else if (first.init || ty.kind == Ty::Any) {
            f.declaration[name] = 'v';
        } else {
            f.declaration[name] = 'h';
            hoisted += (hoisted.empty() ? "var " : ", ") + localName(name) + " = " + defaultValue(ty);
        }
    }
    if (!hoisted.empty()) f.hoistedLines.push_back(hoisted + ";");
}

std::string Translator::parameterList(const std::vector<std::unique_ptr<Parameter>>& params, Fn& f) {
    std::string out;
    for (const auto& param : params) {
        if (!out.empty()) out += ", ";
        const std::string& js = f.locals[param->name->name].js;
        if (param->rest) {
            out += "..." + js;
        } else if (param->defaultValue) {
            const Ty type = f.locals[param->name->name].type;
            out += js + " = " + coerce(expr(param->defaultValue.get()), type).s;
        } else {
            out += js;
        }
    }
    return out;
}

void Translator::parameterCoercions(const std::vector<std::unique_ptr<Parameter>>& params, Fn& f, Lines& out) {
    std::string line;
    for (const auto& param : params) {
        if (param->rest) continue;
        const Local& local = f.locals[param->name->name];
        std::string made;
        switch (local.type.kind) {
            case Ty::Int: made = local.js + " |= 0;"; break;
            case Ty::Uint: made = local.js + " >>>= 0;"; break;
            case Ty::Number:
                if (options.coerceAllParameters) made = local.js + " = +" + local.js + ";";
                break;
            case Ty::Boolean:
                if (options.coerceAllParameters) made = local.js + " = !!" + local.js + ";";
                break;
            case Ty::String:
                if (options.coerceAllParameters) made = local.js + " = as3.str(" + local.js + ");";
                break;
            default: break;
        }
        if (!made.empty()) line += (line.empty() ? "" : " ") + made;
    }
    if (!line.empty()) out.add(line);
}

std::string Translator::functionText(const std::vector<std::unique_ptr<Parameter>>& params, Statement* body, Fn& f,
                                     const std::string& head, bool arrow) {
    Fn* saved = fn;
    f.parent = saved;
    fn = &f;
    prepareLocals(f, &params, body);
    const std::string list = parameterList(params, f);
    Lines inner;
    inner.indent();
    parameterCoercions(params, f, inner);
    const bool plainStart = inner.empty();
    for (const std::string& line : f.hoistedLines) inner.add(line);
    if (prologueHook) {
        const auto hook = prologueHook;
        prologueHook = nullptr;
        hook(inner);
    }
    // Nested functions first, as they are made before the code runs.
    std::vector<Declaration> decls;
    std::vector<FunctionDefinition*> functions;
    collectDeclarations(body, 0, true, decls, functions);
    for (FunctionDefinition* nested : functions) {
        if (f.kind == Fn::Frame && f.cls && f.cls->instance.count(nested->name->name)) continue;
        flushComments(inner, nested->line);
        Fn g;
        g.cls = f.cls;
        g.isStatic = f.isStatic;
        const bool plain = usesThisOrArguments(nested->body.get(), nested->parameters);
        g.kind = plain ? Fn::Plain : Fn::Arrow;
        g.returnType = typeOf(nested->returnType ? nested->returnType->name : std::string());
        g.returnsValue = nested->returnType && g.returnType.kind != Ty::Void;
        const std::string name = f.locals[nested->name->name].js;
        if (plain) {
            inner.add(functionText(nested->parameters, nested->body.get(), g, "function " + name, false), nested->line);
        } else {
            inner.add("const " + name + " = " + functionText(nested->parameters, nested->body.get(), g, "", true) + ";",
                      nested->line);
        }
    }
    // A concise arrow function: one return.
    auto* blockBody = dynamic_cast<BlockStatement*>(body);
    if (arrow && plainStart && f.hoistedLines.empty() && functions.empty() && blockBody &&
        blockBody->statements.size() == 1) {
        auto* result = dynamic_cast<ReturnStatement*>(blockBody->statements[0].get());
        if (result && result->argument) {
            E value = coerce(expr(result->argument.get()), f.returnType);
            fn = saved;
            std::string text = paren(value, P_Assign);
            if (!text.empty() && text[0] == '{') text = "(" + text + ")";
            return "(" + list + ") => " + text;
        }
    }
    if (blockBody) {
        statements(blockBody->statements, inner);
    } else if (body) {
        statement(body, inner);
    }
    if (f.needsThis && !f.nested()) {
        Lines withThis;
        withThis.indent();
        withThis.add("const $this = this;");
        withThis.dedent();
        withThis.append(inner);
        inner = withThis;
    }
    fn = saved;
    const std::string open = arrow ? "(" + list + ") => {" : head + "(" + list + ") {";
    if (inner.empty()) return open + "}";
    return open + "\n" + inner.text() + "\n}";
}

// Statements

void Translator::flushComments(Lines& out, int line) {
    if (!options.comments || line <= 0) return;
    while (commentAt < comments.size() && comments[commentAt].line < line) {
        for (const std::string& text : comments[commentAt].text) out.add(text);
        ++commentAt;
    }
}

std::string Translator::blockText(Statement* stmt) {
    Lines inner;
    inner.indent();
    if (auto* body = dynamic_cast<BlockStatement*>(stmt)) {
        statements(body->statements, inner);
    } else {
        statement(stmt, inner);
    }
    if (inner.empty()) return "{}";
    return "{\n" + inner.text() + "\n}";
}

void Translator::block(Statement* stmt, Lines& out) {
    out.add(blockText(stmt));
}

std::string Translator::discardText(Expression* e) {
    E made;
    if (e->kind == EK::Assignment) made = assignment(static_cast<AssignmentExpression*>(e), true);
    else if (e->kind == EK::Unary) made = unary(static_cast<UnaryExpression*>(e), true);
    else made = expr(e);
    std::string text = made.s;
    if (text.compare(0, 1, "{") == 0 || text.compare(0, 8, "function") == 0 || text.compare(0, 5, "class") == 0 ||
        text.compare(0, 4, "let ") == 0) {
        text = "(" + text + ")";
    }
    return text;
}

void Translator::statement(Statement* stmt, Lines& out) {
    if (!stmt) return;
    if (stmt->line > 0) {
        model.currentLine = stmt->line;
        flushComments(out, stmt->line);
    }
    const int line = stmt->line;
    if (auto* body = dynamic_cast<BlockStatement*>(stmt)) {
        if (body->statements.empty()) return;
        out.add("{", line);
        out.indent();
        statements(body->statements, out);
        out.dedent();
        out.add("}");
    } else if (auto* expression = dynamic_cast<ExpressionStatement*>(stmt)) {
        out.add(discardText(expression->expression.get()) + ";", line);
    } else if (auto* branch = dynamic_cast<IfStatement*>(stmt)) {
        std::string text = "if (" + exprAs(branch->test.get(), Ty::of(Ty::Any)).s + ") " + blockText(branch->consequent.get());
        Statement* alternate = branch->alternate.get();
        while (alternate) {
            if (auto* next = dynamic_cast<IfStatement*>(alternate)) {
                text += " else if (" + expr(next->test.get()).s + ") " + blockText(next->consequent.get());
                alternate = next->alternate.get();
            } else {
                text += " else " + blockText(alternate);
                alternate = nullptr;
            }
        }
        out.add(text, line);
    } else if (auto* loop = dynamic_cast<WhileStatement*>(stmt)) {
        out.add("while (" + expr(loop->test.get()).s + ") " + blockText(loop->body.get()), line);
    } else if (auto* doLoop = dynamic_cast<DoWhileStatement*>(stmt)) {
        out.add("do " + blockText(doLoop->body.get()) + " while (" + expr(doLoop->test.get()).s + ");", line);
    } else if (auto* forLoop = dynamic_cast<ForStatement*>(stmt)) {
        forStatement(forLoop, out, std::string());
    } else if (auto* forIn = dynamic_cast<ForInStatement*>(stmt)) {
        forInStatement(forIn, out, std::string());
    } else if (auto* choice = dynamic_cast<SwitchStatement*>(stmt)) {
        switchStatement(choice, out, std::string());
    } else if (auto* labeled = dynamic_cast<LabeledStatement*>(stmt)) {
        out.add(localName(labeled->label->name) + ":", line);
        statement(labeled->body.get(), out);
    } else if (auto* result = dynamic_cast<ReturnStatement*>(stmt)) {
        Fn* f = fn;
        if (!result->argument) {
            out.add("return;", line);
        } else if (f && f->isConstructor) {
            out.add(discardText(result->argument.get()) + ";", line);
            out.add("return;");
        } else {
            out.add("return " + coerce(expr(result->argument.get()), f ? f->returnType : Ty()).s + ";", line);
        }
    } else if (auto* variables = dynamic_cast<VariableDefinition*>(stmt)) {
        variableDefinition(variables, out);
    } else if (dynamic_cast<FunctionDefinition*>(stmt)) {
        // Made at the function's start.
    } else if (dynamic_cast<ClassDefinition*>(stmt)) {
        error("a class cannot be defined here", line);
    } else if (auto* breakStmt = dynamic_cast<BreakStatement*>(stmt)) {
        out.add(breakStmt->label ? "break " + localName(breakStmt->label->name) + ";" : "break;", line);
    } else if (auto* continueStmt = dynamic_cast<ContinueStatement*>(stmt)) {
        out.add(continueStmt->label ? "continue " + localName(continueStmt->label->name) + ";" : "continue;", line);
    } else if (auto* thrown = dynamic_cast<ThrowStatement*>(stmt)) {
        out.add("throw " + expr(thrown->argument.get()).s + ";", line);
    } else if (auto* attempt = dynamic_cast<TryStatement*>(stmt)) {
        tryStatement(attempt, out);
    } else if (auto* with = dynamic_cast<WithStatement*>(stmt)) {
        withStatement(with, out);
    } else if (dynamic_cast<ForOfStatement*>(stmt)) {
        error("for-of is not ActionScript 3", line);
    }
    // Empty statements and namespace definitions make nothing.
}

void Translator::variableDefinition(VariableDefinition* stmt, Lines& out) {
    for (auto& decl : stmt->declarations) {
        const std::string& name = decl->id->name;
        Fn* f = fn;
        auto local = f ? f->locals.find(name) : decltype(f->locals.end())();
        if (!f || local == f->locals.end()) {
            // A frame's variable: the timeline's.
            if (decl->init) {
                Place target = place(decl->id.get());
                out.add(target.set(coerce(expr(decl->init.get()), target.type).s) + ";", stmt->line);
            }
            continue;
        }
        const char how = f->declaration.count(name) ? f->declaration[name] : 'v';
        const Ty& type = local->second.type;
        const std::string value = decl->init ? coerce(expr(decl->init.get()), type).s : std::string();
        const bool first = f->declared.insert(name).second;
        if (how == 'l') {
            const char* keyword = stmt->isConst && decl->init ? "const " : "let ";
            out.add(keyword + local->second.js + " = " + (decl->init ? value : defaultValue(type)) + ";", stmt->line);
        } else if (how == 'v' && first) {
            out.add("var " + local->second.js + (decl->init ? " = " + value : type.kind == Ty::Any ? std::string() : " = " + defaultValue(type)) + ";",
                    stmt->line);
        } else if (decl->init) {
            out.add(local->second.js + " = " + value + ";", stmt->line);
        }
    }
}

void Translator::forStatement(ForStatement* stmt, Lines& out, const std::string& label) {
    std::string init;
    if (auto* variables = dynamic_cast<VariableDefinition*>(stmt->init.get())) {
        // var i = 0, n = 2: declared as each is.
        Fn* f = fn;
        char how = 0;
        std::vector<std::string> parts;
        for (auto& decl : variables->declarations) {
            const std::string& name = decl->id->name;
            auto local = f ? f->locals.find(name) : decltype(f->locals.end())();
            if (!f || local == f->locals.end()) {
                if (decl->init) {
                    Place target = place(decl->id.get());
                    parts.push_back(target.set(coerce(expr(decl->init.get()), target.type).s));
                }
                how = 'a';
                continue;
            }
            char mine = f->declaration.count(name) ? f->declaration[name] : 'v';
            const bool first = f->declared.insert(name).second;
            if (mine == 'v' && !first) mine = 'a';
            if (mine == 'h') mine = 'a';
            if (how && how != mine) how = 'a';
            else if (!how) how = mine;
            const Ty& type = local->second.type;
            if (decl->init) {
                parts.push_back(local->second.js + " = " + coerce(expr(decl->init.get()), type).s);
            } else if (mine != 'a') {
                parts.push_back(local->second.js + (type.kind == Ty::Any ? std::string() : " = " + defaultValue(type)));
            }
        }
        if (how == 'a') {
            // Declared elsewhere: the assignments alone.
            for (size_t i = 0; i < parts.size(); ++i) init += (i ? ", " : "") + parts[i];
        } else {
            init = how == 'l' ? "let " : "var ";
            for (size_t i = 0; i < parts.size(); ++i) init += (i ? ", " : "") + parts[i];
        }
    } else if (auto* expression = dynamic_cast<ExpressionStatement*>(stmt->init.get())) {
        init = discardText(expression->expression.get());
    }
    const std::string test = stmt->test ? expr(stmt->test.get()).s : std::string();
    std::string update;
    if (stmt->update) {
        // i++ on an int stays i++ here.
        Expression* u = stmt->update.get();
        if (u->kind == EK::Unary) {
            auto* step = static_cast<UnaryExpression*>(u);
            if ((step->op == OT::PostIncrement || step->op == OT::PreIncrement || step->op == OT::PostDecrement ||
                 step->op == OT::PreDecrement) &&
                step->operand->kind == EK::Identifier) {
                Ref ref = resolve(static_cast<Identifier*>(step->operand.get())->name);
                if (ref.kind == Ref::Local && ref.type.kind == Ty::Int) {
                    const bool increment = step->op == OT::PostIncrement || step->op == OT::PreIncrement;
                    update = ref.text + (increment ? "++" : "--");
                }
            }
        }
        if (update.empty()) update = discardText(u);
    }
    (void)label;
    out.add("for (" + init + "; " + test + "; " + update + ") " + blockText(stmt->body.get()), stmt->line);
}

std::string Translator::forInTarget(Statement* left, int line) {
    if (auto* variables = dynamic_cast<VariableDefinition*>(left)) {
        if (variables->declarations.size() != 1) {
            error("for-in takes one variable", line);
            return "$ignored";
        }
        const std::string& name = variables->declarations[0]->id->name;
        Fn* f = fn;
        auto local = f ? f->locals.find(name) : decltype(f->locals.end())();
        if (!f || local == f->locals.end()) {
            Identifier id(name);
            return place(&id).get;
        }
        char how = f->declaration.count(name) ? f->declaration[name] : 'v';
        const bool first = f->declared.insert(name).second;
        if (how == 'l') return "const " + local->second.js;
        if (how == 'v' && first) return "var " + local->second.js;
        return local->second.js;
    }
    if (auto* expression = dynamic_cast<ExpressionStatement*>(left)) {
        Place target = place(expression->expression.get());
        return target.get;
    }
    error("for-in needs a variable", line);
    return "$ignored";
}

void Translator::forInStatement(ForInStatement* stmt, Lines& out, const std::string& label) {
    (void)label;
    const std::string target = forInTarget(stmt->left.get(), stmt->line);
    E object = expr(stmt->right.get());
    std::string source;
    if (stmt->each && object.t.isVector()) {
        source = object.s;
    } else {
        source = std::string(stmt->each ? "as3.values(" : "as3.keys(") + object.s + ")";
    }
    out.add("for (" + target + " of " + source + ") " + blockText(stmt->body.get()), stmt->line);
}

void Translator::switchStatement(SwitchStatement* stmt, Lines& out, const std::string& label) {
    (void)label;
    out.add("switch (" + expr(stmt->discriminant.get()).s + ") {", stmt->line);
    out.indent();
    for (auto& clause : stmt->cases) {
        if (clause->test) out.add("case " + expr(clause->test.get()).s + ":", clause->line);
        else out.add("default:", clause->line);
        out.indent();
        statements(clause->consequent, out);
        out.dedent();
    }
    out.dedent();
    out.add("}");
}

void Translator::tryStatement(TryStatement* stmt, Lines& out) {
    std::string text = "try " + blockText(stmt->block.get());
    if (!stmt->catches.empty()) {
        // One catch for all: the error as ActionScript has it, then each
        // clause by its type.
        const bool single = stmt->catches.size() == 1;
        const std::string caught = single ? localName(stmt->catches[0].variable->name) : temp("error");
        Lines inner;
        inner.add(caught + " = as3.error(" + caught + ");");
        std::string chain;
        bool catchesAll = false;
        for (size_t i = 0; i < stmt->catches.size() && !catchesAll; ++i) {
            CatchClause& clause = stmt->catches[i];
            const std::string name = localName(clause.variable->name);
            // The clause's variable, a local of its block.
            Fn* f = fn;
            Local saved;
            const bool had = f && f->locals.count(clause.variable->name);
            if (had) saved = f->locals[clause.variable->name];
            if (f) f->locals[clause.variable->name] = Local{name, typeOf(clause.type)};
            std::string test;
            if (!clause.type.empty() && clause.type != "*" && clause.type != "Object") {
                test = "as3.is(" + caught + ", " + typeValue(clause.type).s + ")";
            } else {
                catchesAll = true;
            }
            Lines body;
            if (!single && name != caught) body.add("const " + name + " = " + caught + ";");
            if (auto* bodyBlock = dynamic_cast<BlockStatement*>(clause.body.get())) {
                statements(bodyBlock->statements, body);
            } else {
                statement(clause.body.get(), body);
            }
            if (f) {
                if (had) f->locals[clause.variable->name] = saved;
                else f->locals.erase(clause.variable->name);
            }
            if (single) {
                if (!test.empty()) inner.add("if (!" + test + ") throw " + caught + ";");
                inner.append(body);
                break;
            }
            const std::string braced = body.empty() ? "{}" : "{\n" + indented(body.text(), 1) + "\n}";
            chain += (chain.empty() ? "" : " else ") + (test.empty() ? std::string() : "if (" + test + ") ") + braced;
        }
        if (!single) {
            if (!catchesAll) chain += " else {\n    throw " + caught + ";\n}";
            inner.add(chain);
        }
        text += " catch (" + caught + ") {\n" + indented(inner.text(), 1) + "\n}";
    }
    if (stmt->finallyBody) text += " finally " + blockText(stmt->finallyBody.get());
    out.add(text, stmt->line);
}

void Translator::withStatement(WithStatement* stmt, Lines& out) {
    const std::string object = temp("with");
    out.add("{", stmt->line);
    out.indent();
    out.add("const " + object + " = " + expr(stmt->object.get()).s + ";");
    fn->withs.push_back(object);
    if (auto* body = dynamic_cast<BlockStatement*>(stmt->body.get())) {
        statements(body->statements, out);
    } else {
        statement(stmt->body.get(), out);
    }
    fn->withs.pop_back();
    out.dedent();
    out.add("}");
}

// Expressions

namespace {

bool isSimple(Expression* e) {
    if (!e) return true;
    switch (e->kind) {
        case EK::Identifier:
        case EK::This:
        case EK::Literal:
        case EK::Super:
            return true;
        case EK::Member: {
            auto* m = static_cast<MemberExpression*>(e);
            return isSimple(m->object.get()) && (!m->computed || isSimple(m->computedProperty.get()));
        }
        default:
            return false;
    }
}

const char* compoundOperator(OT op) {
    switch (op) {
        case OT::Add: return "+";
        case OT::Subtract: return "-";
        case OT::Multiply: return "*";
        case OT::Divide: return "/";
        case OT::Remainder: return "%";
        case OT::ShiftLeft: return "<<";
        case OT::ShiftRight: return ">>";
        case OT::ShiftRightUnsigned: return ">>>";
        case OT::BitwiseAnd: return "&";
        case OT::BitwiseOr: return "|";
        case OT::BitwiseXor: return "^";
        case OT::Power: return "**";
        case OT::LogicalAnd: return "&&";
        case OT::LogicalOr: return "||";
        case OT::NullCoalescing: return "??";
        default: return nullptr;
    }
}

int operatorPrecedence(OT op) {
    switch (op) {
        case OT::Multiply: case OT::Divide: case OT::Remainder: return P_Mul;
        case OT::Add: case OT::Subtract: return P_Add;
        case OT::ShiftLeft: case OT::ShiftRight: case OT::ShiftRightUnsigned: return P_Shift;
        case OT::Lt: case OT::Gt: case OT::Le: case OT::Ge: case OT::Instanceof: case OT::In: return P_Rel;
        case OT::Equals: case OT::NotEquals: case OT::StrictEquals: case OT::StrictNotEquals: return P_Eq;
        case OT::BitwiseAnd: return P_BitAnd;
        case OT::BitwiseXor: return P_BitXor;
        case OT::BitwiseOr: return P_BitOr;
        case OT::LogicalAnd: return P_And;
        case OT::LogicalOr: return P_Or;
        case OT::NullCoalescing: return P_Coalesce;
        case OT::Power: return P_Exp;
        default: return P_Primary;
    }
}

const char* binaryText(OT op) {
    switch (op) {
        case OT::Lt: return "<";
        case OT::Gt: return ">";
        case OT::Le: return "<=";
        case OT::Ge: return ">=";
        case OT::Equals: return "==";
        case OT::NotEquals: return "!=";
        case OT::StrictEquals: return "===";
        case OT::StrictNotEquals: return "!==";
        case OT::Instanceof: return "instanceof";
        case OT::In: return "in";
        default: return compoundOperator(op);
    }
}

// The type a builtin class's static member has, or a builtin member of a
// value.
Ty builtinStaticType(const std::string& cls, const std::string& name) {
    if (cls == "Math") return Ty::of(name == "PI" || name == "E" || name.find("LN") == 0 || name.find("LOG") == 0 ||
                                             name.find("SQRT") == 0
                                         ? Ty::Number
                                         : Ty::Function);
    if (cls == "int" && (name == "MAX_VALUE" || name == "MIN_VALUE")) return Ty::of(Ty::Int);
    if (cls == "uint" && (name == "MAX_VALUE" || name == "MIN_VALUE")) return Ty::of(Ty::Uint);
    if (cls == "Number") return Ty::of(Ty::Number);
    if (cls == "Array") return Ty::of(Ty::Uint);
    return Ty();
}

Ty builtinMemberType(const Ty& object, const std::string& name) {
    if (name == "length" && (object.kind == Ty::String || object.isArrayLike())) {
        return Ty::of(object.kind == Ty::String ? Ty::Int : Ty::Uint);
    }
    if (object.isXml()) return Ty::instance("XMLList");
    return Ty();
}

Ty builtinCallType(const Ty& object, const std::string& name) {
    if (object.kind == Ty::ClassValue && object.name == "Math") return Ty::of(Ty::Number);
    if (object.kind == Ty::String) {
        static const std::set<std::string> strings = {
            "charAt", "concat", "replace", "slice", "substr", "substring", "toLowerCase", "toUpperCase",
            "toLocaleLowerCase", "toLocaleUpperCase", "toString", "valueOf", "trim",
        };
        if (strings.count(name)) return Ty::of(Ty::String);
        if (name == "charCodeAt") return Ty::of(Ty::Number);
        if (name == "indexOf" || name == "lastIndexOf" || name == "search" || name == "localeCompare") return Ty::of(Ty::Int);
        if (name == "split") return Ty::instance("Array");
    }
    if (object.numeric() && (name == "toString" || name == "toFixed" || name == "toPrecision" || name == "toExponential")) {
        return Ty::of(Ty::String);
    }
    if (object.isArrayLike()) {
        if (name == "join" || name == "toString") return Ty::of(Ty::String);
        if (name == "indexOf" || name == "lastIndexOf") return Ty::of(Ty::Int);
        if (name == "push" || name == "unshift") return Ty::of(Ty::Uint);
        if (name == "slice" || name == "concat" || name == "filter" || name == "map" || name == "splice" ||
            name == "reverse") {
            return object;
        }
        if ((name == "pop" || name == "shift") && object.element) return *object.element;
    }
    if (object.isXml()) {
        if (name == "toString" || name == "toXMLString" || name == "name" || name == "localName" || name == "nodeKind") {
            return Ty::of(Ty::String);
        }
        if (name == "length" || name == "childIndex") return Ty::of(Ty::Int);
        if (name == "hasSimpleContent" || name == "hasComplexContent" || name == "contains") return Ty::of(Ty::Boolean);
        if (name == "parent" || name == "copy" || name == "appendChild" || name == "prependChild") return Ty::instance("XML");
        return Ty::instance("XMLList");
    }
    if (name == "toString") return Ty::of(Ty::String);
    return Ty();
}

} // namespace

E Translator::coerce(E value, const Ty& target) {
    const Ty& from = value.t;
    const auto literalNumber = [&](double& out) {
        char* end = nullptr;
        const std::string& s = value.s;
        if (s.empty()) return false;
        out = std::strtod(s.c_str(), &end);
        if (s.compare(0, 2, "0x") == 0) out = static_cast<double>(std::strtoull(s.c_str() + 2, &end, 16));
        return end && *end == '\0';
    };
    switch (target.kind) {
        case Ty::Int: {
            if (from.kind == Ty::Int) return value;
            double n;
            if (literalNumber(n) && n == std::floor(n) && n >= -2147483648.0 && n <= 2147483647.0) return value;
            E made = make(paren(value, P_Unary) + " | 0", P_BitOr, Ty::of(Ty::Int));
            return made;
        }
        case Ty::Uint: {
            if (from.kind == Ty::Uint) return value;
            double n;
            if (literalNumber(n) && n == std::floor(n) && n >= 0 && n <= 4294967295.0) {
                value.t = Ty::of(Ty::Uint);
                return value;
            }
            if (from.kind == Ty::Int && value.s.find_first_not_of("0123456789") == std::string::npos) return value;
            return make(paren(value, P_Unary) + " >>> 0", P_Shift, Ty::of(Ty::Uint));
        }
        case Ty::Number:
            if (from.numeric()) return value;
            if (from.kind == Ty::String || from.kind == Ty::Boolean) return make("Number(" + value.s + ")", P_Call, target);
            return make("+" + paren(value, P_Unary), P_Unary, target);
        case Ty::Boolean:
            if (from.kind == Ty::Boolean) return value;
            return make("!!" + paren(value, P_Unary), P_Unary, target);
        case Ty::String:
            if (from.kind == Ty::String || from.kind == Ty::Null) return value;
            if (from.numeric() || from.kind == Ty::Boolean) return make("String(" + value.s + ")", P_Call, target);
            return make("as3.str(" + value.s + ")", P_Call, target);
        case Ty::Object:
        case Ty::Instance:
            if (from.kind == Ty::Any && value.s != "undefined" && value.s.compare(0, 4, "new ") != 0) {
                E made = make(paren(value, P_BitOr) + " ?? null", P_Coalesce, target);
                return made;
            }
            if (value.s == "undefined") return make("null", P_Primary, target);
            return value;
        default:
            return value;
    }
}

E Translator::exprAs(Expression* e, const Ty& target) {
    return coerce(expr(e), target);
}

E Translator::expr(Expression* e) {
    if (!e) return make("undefined", P_Primary);
    if (e->line > 0) model.currentLine = e->line;
    switch (e->kind) {
        case EK::Identifier: return identifier(static_cast<Identifier*>(e));
        case EK::Literal: return literal(static_cast<Literal*>(e));
        case EK::Binary: return binary(static_cast<BinaryExpression*>(e));
        case EK::Unary: return unary(static_cast<UnaryExpression*>(e), false);
        case EK::Conditional: {
            auto* c = static_cast<ConditionalExpression*>(e);
            E test = expr(c->test.get());
            E yes = expr(c->consequent.get());
            E no = expr(c->alternate.get());
            Ty type = yes.t.kind == no.t.kind && yes.t.name == no.t.name ? yes.t : Ty();
            if (yes.t.numeric() && no.t.numeric()) type = yes.t.kind == no.t.kind ? yes.t : Ty::of(Ty::Number);
            return make(paren(test, P_Cond + 1) + " ? " + paren(yes, P_Assign) + " : " + paren(no, P_Assign), P_Cond, type);
        }
        case EK::Assignment: return assignment(static_cast<AssignmentExpression*>(e), false);
        case EK::Call: return call(static_cast<CallExpression*>(e));
        case EK::New: return construct(static_cast<NewExpression*>(e));
        case EK::Member: return member(static_cast<MemberExpression*>(e), false);
        case EK::Function: return functionExpression(static_cast<FunctionExpression*>(e));
        case EK::Array: return arrayLiteral(static_cast<ArrayExpression*>(e));
        case EK::Object: return objectLiteral(static_cast<ObjectExpression*>(e));
        case EK::This: {
            ClassModel* cls = contextClass();
            if (cls && !staticContext()) return make(thisRef(), P_Primary, Ty::instance(cls->qualified(), cls));
            if (cls) return make(thisRef(), P_Primary, Ty::classValue(cls->qualified(), cls));
            return make("this", P_Primary);
        }
        case EK::Sequence: {
            auto& parts = static_cast<SequenceExpression*>(e)->expressions;
            std::string text;
            Ty last;
            for (size_t i = 0; i < parts.size(); ++i) {
                E part = i + 1 < parts.size() ? make(discardText(parts[i].get()), P_Assign) : expr(parts[i].get());
                text += (i ? ", " : "") + paren(part, P_Assign);
                last = part.t;
            }
            return make(text, P_Comma, last);
        }
        case EK::Super:
            error("super must be followed by a call or a property");
            return make("undefined", P_Primary);
        case EK::TypeApplication: {
            auto* application = static_cast<TypeApplication*>(e);
            std::string written;
            if (application->base->kind == EK::Identifier) written = static_cast<Identifier*>(application->base.get())->name;
            if (written != "Vector" || application->typeArguments.size() != 1) {
                unsupported("a type applied to " + (written.empty() ? std::string("this") : written));
                return make("undefined", P_Primary);
            }
            return typeValue("Vector.<" + application->typeArguments[0] + ">");
        }
    }
    return make("undefined", P_Primary);
}

E Translator::identifier(Identifier* id) {
    const std::string& name = id->name;
    if (name.size() > 1 && name[0] == '@') {
        // An attribute, in an E4X filter: of the item.
        const std::vector<std::string> withs = activeWiths();
        if (withs.empty()) {
            unsupported("an attribute name outside an E4X filter");
            return make("undefined", P_Primary);
        }
        if (name == "@*") return make(withs.front() + ".attributes()", P_Call, Ty::instance("XMLList"));
        return make(withs.front() + "[" + quote(name) + "]", P_Call, Ty::instance("XMLList"));
    }
    if (name == "arguments") {
        Ref ref = resolve(name);
        if (ref.kind == Ref::Local) return make(ref.text, P_Primary, ref.type);
        return make("arguments", P_Primary, Ty::instance("Array"));
    }
    Ref ref = resolve(name);
    if (ref.kind == Ref::Local) return make(ref.text, P_Primary, ref.type);
    std::string text = ref.text;
    if (ref.kind == Ref::Member && ref.isMethod) text = "as3.bind(" + ref.object + ", " + quote(ref.property) + ")";
    const std::vector<std::string> withs = activeWiths();
    if (!withs.empty()) {
        if (ref.kind == Ref::Unknown) return make(dotted(withs.front(), name), P_Primary, ref.type);
        return make(withRead(name, text), P_Cond, Ty());
    }
    return make(text, ref.kind == Ref::Member || ref.kind == Ref::Static ? P_Primary : P_Primary, ref.type);
}

E Translator::literal(Literal* lit) {
    switch (lit->kind) {
        case Literal::LiteralKind::Null: return make("null", P_Primary, Ty::of(Ty::Null));
        case Literal::LiteralKind::Undefined: return make("undefined", P_Primary);
        case Literal::LiteralKind::Boolean: return make(lit->boolValue ? "true" : "false", P_Primary, Ty::of(Ty::Boolean));
        case Literal::LiteralKind::Numeric: {
            const double n = lit->numberValue;
            Ty type = Ty::of(Ty::Number);
            if (n == std::floor(n) && !std::isinf(n)) {
                if (n >= -2147483648.0 && n <= 2147483647.0) type = Ty::of(Ty::Int);
                else if (n >= 0 && n <= 4294967295.0) type = Ty::of(Ty::Uint);
            }
            const std::string text = numberLiteral(n, lit->line);
            return make(text, text[0] == '-' ? P_Unary : P_Primary, type);
        }
        case Literal::LiteralKind::String: return make(quote(lit->stringValue), P_Primary, Ty::of(Ty::String));
        case Literal::LiteralKind::RegExp: {
            std::string pattern = lit->stringValue;
            std::string flags;
            bool extended = false;
            for (char c : lit->flags) {
                if (c == 'x') extended = true;
                else flags += c;
            }
            if (extended) {
                // x: whitespace and # comments outside classes left out.
                std::string kept;
                bool inClass = false;
                for (size_t i = 0; i < pattern.size(); ++i) {
                    const char c = pattern[i];
                    if (c == '\\' && i + 1 < pattern.size()) {
                        kept += c;
                        kept += pattern[++i];
                        continue;
                    }
                    if (c == '[') inClass = true;
                    else if (c == ']') inClass = false;
                    if (!inClass && (c == ' ' || c == '\t' || c == '\n' || c == '\r')) continue;
                    if (!inClass && c == '#') {
                        while (i < pattern.size() && pattern[i] != '\n') ++i;
                        continue;
                    }
                    kept += c;
                }
                pattern = kept;
            }
            if (pattern.empty()) pattern = "(?:)";
            std::string escaped;
            bool inClass = false;
            for (size_t i = 0; i < pattern.size(); ++i) {
                const char c = pattern[i];
                if (c == '\\' && i + 1 < pattern.size()) {
                    escaped += c;
                    escaped += pattern[++i];
                    continue;
                }
                if (c == '[') inClass = true;
                else if (c == ']') inClass = false;
                if (c == '/' && !inClass) escaped += '\\';
                if (c == '\n') {
                    escaped += "\\n";
                    continue;
                }
                escaped += c;
            }
            return make("/" + escaped + "/" + flags, P_Primary, Ty::instance("RegExp"));
        }
        default:
            unsupported("this literal", lit->line);
            return make("undefined", P_Primary);
    }
}

E Translator::typeValue(const std::string& written) {
    // A type as a value: its class, Vector.<T> applied.
    const size_t open = written.find(".<");
    if (open != std::string::npos && written.back() == '>') {
        const std::string parameter = written.substr(open + 2, written.size() - open - 3);
        const std::string element = parameter == "*" ? std::string("null") : typeValue(parameter).s;
        Ty type = Ty::classValue("__AS3__.vec.Vector");
        type.element = std::make_shared<Ty>(typeOf(parameter));
        return make("as3.Vector(" + element + ")", P_Call, type);
    }
    if (written == "*") return make("null", P_Primary);
    if (written.find('.') == std::string::npos) {
        // A local holding a class, or a name in scope.
        Ref ref = resolve(written);
        if (ref.kind == Ref::Local || ref.kind == Ref::Class || ref.kind == Ref::Global || ref.kind == Ref::Static ||
            ref.kind == Ref::Member) {
            return make(ref.text, P_Primary, ref.type.kind == Ty::ClassValue ? ref.type : Ty::classValue(written));
        }
        return make(ref.text, P_Primary, Ty::classValue(written));
    }
    if (ClassModel* cls = model.sourceClass(written)) return make(classRef(cls), P_Primary, Ty::classValue(written, cls));
    return make(bind(written), P_Primary, Ty::classValue(written));
}

E Translator::typeOperand(Expression* e) {
    if (e->kind == EK::Identifier) {
        const std::string& name = static_cast<Identifier*>(e)->name;
        return typeValue(name);
    }
    if (e->kind == EK::TypeApplication) return expr(e);
    return expr(e);
}

E Translator::binary(BinaryExpression* e) {
    const OT op = e->op;
    if (op == OT::Is || op == OT::As) {
        E value = expr(e->left.get());
        E type = typeOperand(e->right.get());
        Ty result = Ty::of(Ty::Boolean);
        if (op == OT::As) {
            result = type.t.kind == Ty::ClassValue ? Ty::instance(type.t.name, type.t.cls) : Ty();
            if (type.t.kind == Ty::ClassValue && type.t.element) result.element = type.t.element;
            static const std::map<std::string, Ty::Kind> primitives = {
                {"int", Ty::Int}, {"uint", Ty::Uint}, {"Number", Ty::Number}, {"String", Ty::String},
                {"Boolean", Ty::Boolean},
            };
            auto primitive = primitives.find(type.t.name);
            if (primitive != primitives.end()) result = Ty();
        }
        return make(std::string(op == OT::Is ? "as3.is(" : "as3.as(") + value.s + ", " + type.s + ")", P_Call, result);
    }
    if (op == OT::LogicalXor) {
        E left = expr(e->left.get());
        E right = expr(e->right.get());
        return make("!" + paren(left, P_Unary) + " !== !" + paren(right, P_Unary), P_Eq, Ty::of(Ty::Boolean));
    }
    if (op == OT::Instanceof) {
        E left = expr(e->left.get());
        E type = typeOperand(e->right.get());
        return make(paren(left, P_Rel) + " instanceof " + paren(type, P_Rel + 1), P_Rel, Ty::of(Ty::Boolean));
    }
    E left = expr(e->left.get());
    E right = expr(e->right.get());
    if (op == OT::In && (right.t.isDictionary() || right.t.isXml())) {
        return make("as3.hasItem(" + right.s + ", " + left.s + ")", P_Call, Ty::of(Ty::Boolean));
    }
    const char* text = binaryText(op);
    if (!text) {
        unsupported("this operator", e->line);
        return make("undefined", P_Primary);
    }
    const int prec = operatorPrecedence(op);
    std::string l, r;
    if (op == OT::NullCoalescing) {
        l = paren(left, P_BitOr);
        r = paren(right, P_BitOr);
    } else if (op == OT::Power) {
        l = paren(left, P_Postfix);
        r = paren(right, P_Exp);
    } else {
        l = paren(left, prec);
        r = paren(right, prec + 1);
    }
    Ty type;
    switch (op) {
        case OT::Add:
            if (left.t.kind == Ty::String || right.t.kind == Ty::String) type = Ty::of(Ty::String);
            else if (left.t.numeric() && right.t.numeric()) type = Ty::of(Ty::Number);
            break;
        case OT::Subtract: case OT::Multiply: case OT::Divide: case OT::Remainder: case OT::Power:
            type = Ty::of(Ty::Number);
            break;
        case OT::ShiftLeft: case OT::ShiftRight: case OT::BitwiseAnd: case OT::BitwiseOr: case OT::BitwiseXor:
            type = Ty::of(Ty::Int);
            break;
        case OT::ShiftRightUnsigned: type = Ty::of(Ty::Uint); break;
        case OT::Lt: case OT::Gt: case OT::Le: case OT::Ge: case OT::Equals: case OT::NotEquals:
        case OT::StrictEquals: case OT::StrictNotEquals: case OT::In:
            type = Ty::of(Ty::Boolean);
            break;
        case OT::LogicalAnd: case OT::LogicalOr: case OT::NullCoalescing:
            if (left.t.kind == right.t.kind && left.t.name == right.t.name) type = left.t;
            break;
        default: break;
    }
    return make(l + " " + text + " " + r, prec, type);
}

E Translator::unary(UnaryExpression* e, bool discard) {
    switch (e->op) {
        case OT::PostIncrement: return incDec(e->operand.get(), true, true, discard);
        case OT::PostDecrement: return incDec(e->operand.get(), false, true, discard);
        case OT::PreIncrement: return incDec(e->operand.get(), true, false, discard);
        case OT::PreDecrement: return incDec(e->operand.get(), false, false, discard);
        default: break;
    }
    if (e->op == OT::Delete) {
        Expression* target = e->operand.get();
        if (target->kind == EK::Member) {
            auto* m = static_cast<MemberExpression*>(target);
            E object = expr(m->object.get());
            if (m->computed) {
                E key = expr(m->computedProperty.get());
                if (object.t.isDictionary()) {
                    return make("as3.deleteItem(" + object.s + ", " + key.s + ")", P_Call, Ty::of(Ty::Boolean));
                }
                return make("delete " + paren(object, P_Call) + "[" + key.s + "]", P_Unary, Ty::of(Ty::Boolean));
            }
            const std::string& name = m->property->name;
            if (name[0] == '@' || name == "*" || !isIdentifier(name)) {
                return make("delete " + paren(object, P_Call) + "[" + quote(name) + "]", P_Unary, Ty::of(Ty::Boolean));
            }
            return make("delete " + paren(object, P_Call) + "." + name, P_Unary, Ty::of(Ty::Boolean));
        }
        // delete of a variable: false, as for a declared one.
        return make("false", P_Primary, Ty::of(Ty::Boolean));
    }
    E operand = expr(e->operand.get());
    switch (e->op) {
        case OT::Positive: {
            std::string text = paren(operand, P_Unary);
            if (!text.empty() && text[0] == '+') text = "(" + text + ")";
            return make("+" + text, P_Unary, Ty::of(Ty::Number));
        }
        case OT::Negative: {
            std::string text = paren(operand, P_Unary);
            if (!text.empty() && text[0] == '-') text = "(" + text + ")";
            Ty type = operand.t.kind == Ty::Int || operand.t.kind == Ty::Uint ? operand.t : Ty::of(Ty::Number);
            if (operand.t.kind == Ty::Uint && operand.p == P_Primary && text.find_first_not_of("0123456789") == std::string::npos) {
                const double n = std::strtod(text.c_str(), nullptr);
                type = Ty::of(n <= 2147483648.0 ? Ty::Int : Ty::Number);
            } else if (operand.t.kind == Ty::Uint) {
                type = Ty::of(Ty::Number);
            }
            return make("-" + text, P_Unary, type);
        }
        case OT::BitwiseNot: return make("~" + paren(operand, P_Unary), P_Unary, Ty::of(Ty::Int));
        case OT::LogicalNot: return make("!" + paren(operand, P_Unary), P_Unary, Ty::of(Ty::Boolean));
        case OT::Void: return make("void " + paren(operand, P_Unary), P_Unary);
        case OT::Typeof: return make("as3.typeOf(" + operand.s + ")", P_Call, Ty::of(Ty::String));
        case OT::NonNull: return operand;
        default:
            unsupported("this operator", e->line);
            return make("undefined", P_Primary);
    }
}

Translator::Place Translator::place(Expression* e) {
    Place out;
    if (e->kind == EK::Identifier) {
        const std::string& name = static_cast<Identifier*>(e)->name;
        if (name.size() > 1 && name[0] == '@') {
            const std::vector<std::string> withs = activeWiths();
            const std::string text = (withs.empty() ? std::string("undefined") : withs.front()) + "[" + quote(name) + "]";
            out.get = text;
            out.set = [text](const std::string& v) { return text + " = " + v; };
            return out;
        }
        Ref ref = resolve(name);
        out.type = ref.type;
        if (ref.kind == Ref::Class || ref.kind == Ref::Function || ref.kind == Ref::Global) {
            error("'" + name + "' cannot be assigned");
            out.error = true;
        }
        const std::vector<std::string> withs = ref.kind == Ref::Local ? std::vector<std::string>() : activeWiths();
        const std::string text = ref.text;
        if (withs.empty()) {
            out.get = text;
            out.set = [text](const std::string& v) { return text + " = " + v; };
            return out;
        }
        out.simple = false;
        out.get = withRead(name, text);
        out.set = [withs, name, text](const std::string& v) {
            std::string made = text + " = " + v;
            for (size_t i = withs.size(); i-- > 0;) {
                made = "as3.has(" + withs[i] + ", " + quote(name) + ") ? (" + dotted(withs[i], name) + " = " + v + ") : (" +
                       made + ")";
            }
            return made;
        };
        return out;
    }
    if (e->kind == EK::Member) {
        auto* m = static_cast<MemberExpression*>(e);
        out.simple = isSimple(m);
        if (m->computed) {
            E object = expr(m->object.get());
            E key = expr(m->computedProperty.get());
            if (object.t.isDictionary()) {
                const std::string o = object.s;
                const std::string k = key.s;
                out.get = "as3.getItem(" + o + ", " + k + ")";
                out.set = [o, k](const std::string& v) { return "as3.setItem(" + o + ", " + k + ", " + v + ")"; };
                return out;
            }
            if (object.t.isVector() && object.t.element) out.type = *object.t.element;
            const std::string text = paren(object, P_Call) + "[" + key.s + "]";
            out.get = text;
            out.set = [text](const std::string& v) { return text + " = " + v; };
            return out;
        }
        const std::string& name = m->property->name;
        std::string text;
        if (m->object->kind == EK::Super) {
            text = "super." + name;
            ClassModel* cls = contextClass();
            ClassModel* owner = nullptr;
            if (cls && cls->superClass) {
                if (const Member* member = findInstance(cls->superClass, name, &owner, true)) {
                    text = dotted("super", memberName(owner, name, *member));
                    out.type = memberType(owner, name, *member);
                }
            }
        } else if (name[0] == '@' || name == "*") {
            E object = expr(m->object.get());
            text = paren(object, P_Call) + "[" + quote(name) + "]";
        } else {
            std::string qualified;
            if (qualifiedPath(m, qualified)) {
                E value = member(m, false);
                text = value.s;
                out.type = value.t;
            } else {
                E object = expr(m->object.get());
                std::string property = name;
                if (object.t.kind == Ty::ClassValue && object.t.cls) {
                    ClassModel* owner = nullptr;
                    if (const Member* found = findStatic(object.t.cls, name, &owner, false)) {
                        property = memberName(owner, name, *found);
                        out.type = memberType(owner, name, *found);
                    }
                } else if (object.t.kind == Ty::Instance && object.t.cls) {
                    ClassModel* owner = nullptr;
                    if (const Member* found = findInstance(object.t.cls, name, &owner, false)) {
                        property = memberName(owner, name, *found);
                        out.type = memberType(owner, name, *found);
                    }
                }
                std::string base = paren(object, P_Call);
                if (object.p == P_Primary && !base.empty() && std::isdigit(static_cast<unsigned char>(base[0]))) base = "(" + base + ")";
                text = dotted(base, property);
            }
        }
        out.get = text;
        out.set = [text](const std::string& v) { return text + " = " + v; };
        return out;
    }
    error("this cannot be assigned");
    out.error = true;
    out.get = "undefined";
    out.set = [](const std::string& v) { return v; };
    return out;
}

E Translator::assignment(AssignmentExpression* e, bool discard) {
    (void)discard;
    Place target = place(e->left.get());
    if (e->op == OT::Assign) {
        E value = coerce(expr(e->right.get()), target.type);
        return make(target.set(paren(value, P_Assign)), P_Assign, target.type);
    }
    const char* op = compoundOperator(e->op);
    if (!op) {
        unsupported("this assignment", e->line);
        return make("undefined", P_Primary);
    }
    E value = expr(e->right.get());
    const int prec = operatorPrecedence(e->op);
    const bool wraps = target.type.kind == Ty::Int || target.type.kind == Ty::Uint;
    const bool logical = e->op == OT::LogicalAnd || e->op == OT::LogicalOr || e->op == OT::NullCoalescing;
    const bool plainTarget = target.get.compare(0, 11, "as3.getItem") != 0 && target.get.find(" ? ") == std::string::npos;
    if ((wraps || logical || !plainTarget) && (target.simple || !plainTarget)) {
        E combined = make(target.get + " " + op + " " + paren(value, prec + 1), prec, Ty());
        if (e->op == OT::Add && (value.t.kind == Ty::String || target.type.kind == Ty::String)) combined.t = Ty::of(Ty::String);
        else if (!logical) combined.t = Ty::of(Ty::Number);
        if (e->op == OT::ShiftLeft || e->op == OT::ShiftRight || e->op == OT::BitwiseAnd || e->op == OT::BitwiseOr ||
            e->op == OT::BitwiseXor) {
            combined.t = Ty::of(Ty::Int);
        }
        if (e->op == OT::ShiftRightUnsigned) combined.t = Ty::of(Ty::Uint);
        E stored = coerce(combined, target.type);
        return make(target.set(paren(stored, P_Assign)), P_Assign, target.type);
    }
    return make(target.get + " " + op + "= " + paren(value, P_Assign), P_Assign, target.type);
}

E Translator::incDec(Expression* operand, bool increment, bool postfix, bool discard) {
    Place target = place(operand);
    const bool wraps = target.type.kind == Ty::Int || target.type.kind == Ty::Uint;
    const bool plainTarget = target.get.compare(0, 11, "as3.getItem") != 0 && target.get.find(" ? ") == std::string::npos;
    if ((wraps && (discard || !postfix) && target.simple) || !plainTarget) {
        E step = make(target.get + (increment ? " + 1" : " - 1"), P_Add, Ty::of(Ty::Number));
        if (!plainTarget && !wraps) step.s = "+" + paren(make(target.get, P_Primary), P_Unary) + (increment ? " + 1" : " - 1");
        E stored = coerce(step, target.type);
        return make(target.set(paren(stored, P_Assign)), P_Assign, target.type.kind == Ty::Any ? Ty::of(Ty::Number) : target.type);
    }
    const char* op = increment ? "++" : "--";
    Ty type = target.type.numeric() ? target.type : Ty::of(Ty::Number);
    return make(postfix ? target.get + op : op + target.get, postfix ? P_Postfix : P_Unary, type);
}

std::string Translator::arguments(const ExpressionList& args, const std::vector<Ty>* types) {
    std::string out;
    for (size_t i = 0; i < args.size(); ++i) {
        E value = expr(args[i].get());
        if (types && i < types->size()) value = coerce(value, (*types)[i]);
        out += (i ? ", " : "") + paren(value, P_Assign);
    }
    return out;
}

bool Translator::qualifiedPath(Expression* e, std::string& qualified) {
    // a.b.C, where a is not a variable or member but a package's start.
    std::vector<std::string> parts;
    Expression* at = e;
    while (at && at->kind == EK::Member) {
        auto* m = static_cast<MemberExpression*>(at);
        if (m->computed || !m->property) return false;
        parts.push_back(m->property->name);
        at = m->object.get();
    }
    if (!at || at->kind != EK::Identifier || parts.empty() || !unit) return false;
    const std::string& rootName = static_cast<Identifier*>(at)->name;
    for (Fn* f = fn; f; f = f->parent) {
        if (f->locals.count(rootName)) return false;
    }
    if (model.findDefinition(unit, rootName)) return false;
    ClassModel* cls = contextClass();
    if (cls && (findInstance(cls, rootName, nullptr, true) || findStatic(cls, rootName, nullptr, true))) return false;
    parts.push_back(rootName);
    std::reverse(parts.begin(), parts.end());
    std::string package;
    for (size_t i = 0; i + 1 < parts.size(); ++i) package += (i ? "." : "") + parts[i];
    if (!model.isPackage(unit, package)) return false;
    qualified = package + "." + parts.back();
    return true;
}

E Translator::call(CallExpression* e) {
    Expression* callee = e->callee.get();
    if (callee->kind == EK::Super) {
        ClassModel* cls = contextClass();
        Fn* method = methodFn();
        if (!method || !method->isConstructor) error("super() outside a constructor");
        if (!cls || cls->superName.empty() || cls->superName == "Object") {
            // Object's constructor does nothing.
            return make(std::string(), P_Primary);
        }
        return make("super.$construct(" + arguments(e->arguments) + ")", P_Call);
    }
    if (Expression* predicate = abc::filterPredicate(e)) return xmlFilter(e, predicate);
    if (callee->kind == EK::Identifier) {
        const std::string& name = static_cast<Identifier*>(callee)->name;
        if (name == "$xmlPart" && e->arguments.size() == 2) {
            E value = expr(e->arguments[0].get());
            const std::string kind = literalValue(e->arguments[1].get());
            if (kind == "1") return make("as3.xmlText(" + value.s + ")", P_Call, Ty::of(Ty::String));
            if (kind == "2") return make("as3.xmlAttribute(" + value.s + ")", P_Call, Ty::of(Ty::String));
            return make("String(" + value.s + ")", P_Call, Ty::of(Ty::String));
        }
        Ref ref = resolve(name);
        if (ref.kind == Ref::Global || (ref.kind == Ref::Class && ref.type.name == name)) {
            // Conversions.
            const auto one = [&]() { return e->arguments.empty() ? make("undefined", P_Primary) : expr(e->arguments[0].get()); };
            if (name == "int" && e->arguments.size() == 1) return coerce(make(paren(one(), P_Unary), P_Unary), Ty::of(Ty::Int));
            if (name == "uint" && e->arguments.size() == 1) {
                E value = one();
                return make(paren(value, P_Unary) + " >>> 0", P_Shift, Ty::of(Ty::Uint));
            }
            if (name == "Number") return make("Number(" + arguments(e->arguments) + ")", P_Call, Ty::of(Ty::Number));
            if (name == "String") return make("String(" + arguments(e->arguments) + ")", P_Call, Ty::of(Ty::String));
            if (name == "Boolean") return make("Boolean(" + arguments(e->arguments) + ")", P_Call, Ty::of(Ty::Boolean));
            if (name == "XML") return make("as3.toXML(" + arguments(e->arguments) + ")", P_Call, Ty::instance("XML"));
            if (name == "XMLList") return make("as3.toXMLList(" + arguments(e->arguments) + ")", P_Call, Ty::instance("XMLList"));
            if (name == "Array") return make("Array(" + arguments(e->arguments) + ")", P_Call, Ty::instance("Array"));
            if (name == "Object") return make("Object(" + arguments(e->arguments) + ")", P_Call, Ty::of(Ty::Object));
            if (ref.kind == Ref::Class && ref.text.compare(0, 4, "as3.") == 0 && name.find("Error") != std::string::npos) {
                return make("new " + ref.text + "(" + arguments(e->arguments) + ")", P_Call, Ty::instance(name));
            }
            if (ref.kind == Ref::Global && ref.type.kind == Ty::Function) {
                return make(ref.text + "(" + arguments(e->arguments) + ")", P_Call,
                            name == "isNaN" || name == "isFinite" || name == "isXMLName" ? Ty::of(Ty::Boolean)
                            : name == "parseInt" || name == "parseFloat" ? Ty::of(Ty::Number)
                            : name == "trace" ? Ty::of(Ty::Void)
                                              : Ty::of(Ty::String));
            }
        }
        const std::vector<std::string> withs = ref.kind == Ref::Local ? std::vector<std::string>() : activeWiths();
        const std::string args = arguments(e->arguments);
        Ty result;
        if (ref.kind == Ref::Member || ref.kind == Ref::Static) {
            ClassModel* cls = contextClass();
            ClassModel* owner = nullptr;
            const Member* member = ref.kind == Ref::Member ? findInstance(cls, name, &owner, true)
                                                           : findStatic(cls, name, &owner, true);
            if (member && member->kind == Member::Method) {
                if (FunctionDefinition* definition = methodDefinition(owner, name, ref.kind == Ref::Static)) {
                    if (definition->returnType) result = typeOf(definition->returnType->name, owner->unit);
                }
            }
        }
        if (ref.kind == Ref::Class) {
            // A class called: a cast.
            if (e->arguments.size() != 1) error("a cast takes one argument");
            Ty type = ref.type.kind == Ty::ClassValue ? Ty::instance(ref.type.name, ref.type.cls) : Ty();
            return make("as3.cast(" + args + ", " + ref.text + ")", P_Call, type);
        }
        if (!withs.empty()) {
            if (ref.kind == Ref::Unknown) return make(dotted(withs.front(), name) + "(" + args + ")", P_Call);
            std::string made = (ref.kind == Ref::Member ? ref.object + "." + ref.property : ref.text) + "(" + args + ")";
            for (size_t i = withs.size(); i-- > 0;) {
                made = "as3.has(" + withs[i] + ", " + quote(name) + ") ? " + dotted(withs[i], name) + "(" + args + ") : " + made;
            }
            return make(made, P_Cond);
        }
        return make(ref.text + "(" + args + ")", P_Call, result);
    }
    if (callee->kind == EK::Member) {
        auto* m = static_cast<MemberExpression*>(callee);
        std::string qualified;
        if (qualifiedPath(m, qualified)) {
            E target = member(m, true);
            if (target.t.kind == Ty::ClassValue) {
                return make("as3.cast(" + arguments(e->arguments) + ", " + target.s + ")", P_Call,
                            Ty::instance(target.t.name, target.t.cls));
            }
            return make(target.s + "(" + arguments(e->arguments) + ")", P_Call);
        }
        if (!m->computed && m->object->kind != EK::Super) {
            const std::string& name = m->property->name;
            if (name == "sort" || name == "sortOn" || name == "insertAt" || name == "removeAt") {
                E object = expr(m->object.get());
                if (object.t.kind == Ty::Any || object.t.kind == Ty::Object || object.t.isArrayLike()) {
                    const std::string args = arguments(e->arguments);
                    return make("as3." + name + "(" + object.s + (args.empty() ? "" : ", " + args) + ")", P_Call,
                                name == "removeAt" ? Ty() : object.t);
                }
            }
        }
        E target = member(m, true);
        if (target.t.kind == Ty::Function && target.t.name == "vector-push" && target.t.element) {
            // A typed Vector's push: its elements converted.
            const std::vector<Ty> types(e->arguments.size(), *target.t.element);
            return make(target.s + "(" + arguments(e->arguments, &types) + ")", P_Call, Ty::of(Ty::Uint));
        }
        Ty result;
        if (target.t.kind == Ty::Function && target.t.element) result = *target.t.element;
        return make(target.s + "(" + arguments(e->arguments) + ")", P_Call, result);
    }
    if (callee->kind == EK::TypeApplication) {
        E type = expr(callee);
        Ty made = Ty::instance("__AS3__.vec.Vector");
        made.element = type.t.element;
        return make("as3.toVector(" + type.s.substr(11, type.s.size() - 12) + ", " + arguments(e->arguments) + ")", P_Call, made);
    }
    E target = expr(callee);
    std::string text = paren(target, P_Call);
    if (callee->kind == EK::Function) text = "(" + target.s + ")";
    return make(text + "(" + arguments(e->arguments) + ")", P_Call);
}

E Translator::construct(NewExpression* e) {
    Expression* callee = e->callee.get();
    if (callee->kind == EK::TypeApplication) {
        E type = expr(callee);
        Ty made = Ty::instance("__AS3__.vec.Vector");
        made.element = type.t.element;
        const std::string element = type.s.substr(11, type.s.size() - 12);
        const std::string args = arguments(e->arguments);
        return make("as3.vector(" + element + (args.empty() ? "" : ", " + args) + ")", P_Call, made);
    }
    E target;
    if (callee->kind == EK::Identifier) {
        const std::string& name = static_cast<Identifier*>(callee)->name;
        Ref ref = resolve(name);
        target = make(ref.text, P_Primary, ref.type);
        if (ref.kind == Ref::Member && ref.isMethod) target.s = ref.text;
    } else if (callee->kind == EK::Member) {
        target = member(static_cast<MemberExpression*>(callee), true);
    } else {
        target = expr(callee);
    }
    std::string text = target.s;
    if (target.p < P_Primary || text.find('(') != std::string::npos || callee->kind == EK::Function) text = "(" + text + ")";
    Ty type;
    if (target.t.kind == Ty::ClassValue) {
        type = Ty::instance(target.t.name, target.t.cls);
        if (target.t.name == "Object") type = Ty::of(Ty::Object);
    }
    return make("new " + text + "(" + arguments(e->arguments) + ")", P_Call, type);
}

E Translator::member(MemberExpression* e, bool callee) {
    std::string qualified;
    if (qualifiedPath(e, qualified)) {
        auto global = model.globals.find(qualified);
        if (global != model.globals.end()) {
            const Definition& definition = global->second;
            if (definition.kind == Definition::Class) {
                return make(classRef(definition.cls), P_Primary, Ty::classValue(qualified, definition.cls));
            }
            if (definition.kind == Definition::Variable) {
                return make(dotted("as3.package(" + quote(definition.package) + ")", definition.name), P_Primary,
                            typeOf(definition.type, unit));
            }
            return make(bind(qualified, nullptr, &definition), P_Primary, Ty::of(Ty::Function));
        }
        if (abc::findPlayerClass(packageOf(qualified), shortOf(qualified))) {
            return make(bind(qualified), P_Primary, Ty::classValue(qualified));
        }
        return make(bind(qualified), P_Primary, Ty::of(Ty::Function));
    }
    if (e->computed) {
        E object = expr(e->object.get());
        E key = expr(e->computedProperty.get());
        const std::string base = paren(object, P_Call);
        const bool objectKey = key.t.kind == Ty::Instance || key.t.kind == Ty::ClassValue || key.t.kind == Ty::Function;
        if (object.t.isDictionary() || ((object.t.kind == Ty::Any || object.t.kind == Ty::Object) && objectKey)) {
            return make("as3.getItem(" + object.s + ", " + key.s + ")", P_Call);
        }
        Ty type;
        if (object.t.isVector() && object.t.element) type = *object.t.element;
        if (object.t.is("XMLList")) type = Ty::instance("XML");
        if (object.t.kind == Ty::String) type = Ty::of(Ty::String);
        return make(base + (e->optional ? "?.[" : "[") + key.s + "]", P_Call, type);
    }
    const std::string& name = e->property->name;
    if (e->object->kind == EK::Super) {
        ClassModel* cls = contextClass();
        ClassModel* owner = nullptr;
        std::string property = name;
        Ty type;
        bool isMethod = false;
        if (cls && cls->superClass) {
            if (const Member* member = findInstance(cls->superClass, name, &owner, true)) {
                property = memberName(owner, name, *member);
                type = memberType(owner, name, *member);
                isMethod = member->kind == Member::Method;
                if (isMethod && callee) {
                    if (FunctionDefinition* definition = methodDefinition(owner, name, false)) {
                        if (definition->returnType) {
                            type = Ty::of(Ty::Function);
                            type.element = std::make_shared<Ty>(typeOf(definition->returnType->name, owner->unit));
                        }
                    }
                }
            }
        }
        if (fn && fn->kind == Fn::Plain) unsupported("super in a nested function");
        const std::string text = dotted("super", property);
        if (!callee && (isMethod || !cls || !cls->superClass)) {
            if (isMethod) return make(text + ".bind(" + thisRef() + ")", P_Call, Ty::of(Ty::Function));
        }
        return make(text, P_Primary, type);
    }
    E object = expr(e->object.get());
    std::string base = paren(object, P_Call);
    if (object.p == P_Primary && !base.empty() && std::isdigit(static_cast<unsigned char>(base[0]))) base = "(" + base + ")";
    const std::string dot = e->optional ? "?." : ".";
    // E4X: attributes and every child.
    if (name == "*") return make(base + dot + "children()", P_Call, Ty::instance("XMLList"));
    if (name == "@*") return make(base + dot + "attributes()", P_Call, Ty::instance("XMLList"));
    if (name[0] == '@') return make(base + (e->optional ? "?.[" : "[") + quote(name) + "]", P_Call, Ty::instance("XMLList"));
    // A class's statics.
    if (object.t.kind == Ty::ClassValue) {
        if (object.t.cls) {
            ClassModel* owner = nullptr;
            if (const Member* member = findStatic(object.t.cls, name, &owner, false)) {
                const std::string property = memberName(owner, name, *member);
                Ty type = memberType(owner, name, *member);
                if (member->kind == Member::Method && callee) {
                    if (FunctionDefinition* definition = methodDefinition(owner, name, true)) {
                        if (definition->returnType) {
                            type = Ty::of(Ty::Function);
                            type.element = std::make_shared<Ty>(typeOf(definition->returnType->name, owner->unit));
                        }
                    }
                }
                return make(base + (isIdentifier(property) ? dot + property : "[" + quote(property) + "]"), P_Primary, type);
            }
        }
        Ty type = builtinStaticType(object.t.name, name);
        if (callee && object.t.name == "Math") {
            type = Ty::of(Ty::Function);
            type.element = std::make_shared<Ty>(Ty::of(Ty::Number));
        }
        return make(e->optional ? base + "?." + name : dotted(base, name), P_Primary, type);
    }
    // A class's instance members.
    if (object.t.kind == Ty::Instance && object.t.cls) {
        ClassModel* owner = nullptr;
        if (const Member* member = findInstance(object.t.cls, name, &owner, false)) {
            const std::string property = memberName(owner, name, *member);
            if (member->kind == Member::Method && !callee) {
                return make("as3.bind(" + object.s + ", " + quote(property) + ")", P_Call, Ty::of(Ty::Function));
            }
            Ty type = memberType(owner, name, *member);
            if (member->kind == Member::Method) {
                if (FunctionDefinition* definition = methodDefinition(owner, name, false)) {
                    if (definition->returnType) {
                        type.element = std::make_shared<Ty>(typeOf(definition->returnType->name, owner->unit));
                    }
                }
            }
            return make(base + (isIdentifier(property) ? dot + property : "[" + quote(property) + "]"), P_Primary, type);
        }
    }
    if (object.t.isXml() && !callee && isXmlMethod(name)) {
        return make(base + dot + "child(" + quote(name) + ")", P_Call, Ty::instance("XMLList"));
    }
    Ty type = builtinMemberType(object.t, name);
    if (callee) {
        type = Ty::of(Ty::Function);
        const Ty result = builtinCallType(object.t, name);
        if (result.kind != Ty::Any) type.element = std::make_shared<Ty>(result);
        if (object.t.isVector() && name == "push" && object.t.element && object.t.element->primitive()) {
            type.name = "vector-push";
            type.element = object.t.element;
        }
    }
    return make(e->optional ? base + "?." + name : dotted(base, name), P_Primary, type);
}

E Translator::arrayLiteral(ArrayExpression* e) {
    std::string out;
    Ty element;
    if (!e->vectorType.empty()) element = typeOf(e->vectorType);
    for (size_t i = 0; i < e->elements.size(); ++i) {
        if (i) out += ", ";
        if (!e->elements[i]) {
            if (i + 1 == e->elements.size()) out += "undefined";
            continue;
        }
        E value = expr(e->elements[i].get());
        if (!e->vectorType.empty()) value = coerce(value, element);
        out += paren(value, P_Assign);
    }
    if (!e->vectorType.empty()) {
        E type = typeValue(e->vectorType);
        Ty made = Ty::instance("__AS3__.vec.Vector");
        made.element = std::make_shared<Ty>(element);
        return make("as3.vectorOf(" + type.s + ", [" + out + "])", P_Call, made);
    }
    return make("[" + out + "]", P_Primary, Ty::instance("Array"));
}

E Translator::objectLiteral(ObjectExpression* e) {
    if (e->properties.empty()) return make("{}", P_Primary, Ty::of(Ty::Object));
    std::string out;
    for (const auto& property : e->properties) {
        Expression* key = property.first.get();
        std::string name;
        if (key && key->kind == EK::Identifier) {
            name = static_cast<Identifier*>(key)->name;
            if (!isIdentifier(name)) name = quote(name);
        } else if (key && key->kind == EK::Literal && static_cast<Literal*>(key)->kind == Literal::LiteralKind::Numeric) {
            name = quote(abc::numberText(static_cast<Literal*>(key)->numberValue));
        } else if (key && key->kind == EK::Literal) {
            const std::string& text = static_cast<Literal*>(key)->stringValue;
            name = isIdentifier(text) ? text : quote(text);
        } else {
            name = "[" + expr(key).s + "]";
        }
        if (!out.empty()) out += ", ";
        out += name + ": " + paren(expr(property.second.get()), P_Assign);
    }
    return make("{ " + out + " }", P_Primary, Ty::of(Ty::Object));
}

E Translator::functionExpression(FunctionExpression* e) {
    const std::string name = e->name ? e->name->name : std::string();
    bool usesSelf = false;
    if (!name.empty()) {
        Scan scan;
        scanStatement(e->body.get(), scan);
        usesSelf = scan.refs.count(name) || scan.innerFree.count(name);
    }
    Fn g;
    g.cls = contextClass();
    g.isStatic = staticContext();
    const bool plain = usesSelf || usesThisOrArguments(e->body.get(), e->parameters);
    g.kind = plain ? Fn::Plain : Fn::Arrow;
    g.returnType = typeOf(e->returnType ? e->returnType->name : std::string());
    g.returnsValue = e->returnType && g.returnType.kind != Ty::Void;
    if (usesSelf) g.selfName = name;
    const std::string head = plain ? "function " + (usesSelf ? localName(name) : std::string()) : std::string();
    const std::string text = functionText(e->parameters, e->body.get(), g, plain && !usesSelf ? "function " : head, !plain);
    return make(text, plain ? P_Primary : P_Assign, Ty::of(Ty::Function));
}

E Translator::xmlFilter(CallExpression* e, Expression* predicate) {
    E list = expr(e->arguments[0].get());
    Fn g;
    g.cls = contextClass();
    g.isStatic = staticContext();
    g.kind = Fn::Arrow;
    g.parent = fn;
    g.locals["$item"] = Local{"$item", Ty::instance("XML")};
    g.withs.push_back("$item");
    Fn* saved = fn;
    fn = &g;
    E test = expr(predicate);
    fn = saved;
    return make("as3.xmlFilter(" + list.s + ", ($item) => " + paren(test, P_Assign) + ")", P_Call, Ty::instance("XMLList"));
}

// Definitions and files

std::string Translator::definitionCall(const Definition& definition) const {
    if (definition.outside) return "$private.define(" + quote(definition.name) + ", ";
    return "as3.define(" + quote(definition.package.empty() ? definition.name : definition.package + "." + definition.name) + ", ";
}

std::string Translator::fileHeader(const std::string& sourceName) const {
    return "// " + sourceName + ", translated to JavaScript by libas3.\n\"use strict\";\n";
}

namespace {

// The statements of a body, as the function's top.
std::vector<Statement*> topsOf(Statement* body) {
    std::vector<Statement*> tops;
    if (auto* block = dynamic_cast<BlockStatement*>(body)) {
        for (auto& s : block->statements) tops.push_back(s.get());
    } else if (body) {
        tops.push_back(body);
    }
    return tops;
}

std::string fieldKey(const std::string& name) {
    return isIdentifier(name) ? name : quote(name);
}

std::string methodKey(const std::string& name) {
    if (name == "constructor") return "[\"constructor\"]";
    return isIdentifier(name) ? name : "[" + quote(name) + "]";
}

} // namespace

std::string Translator::translateInterface(ClassModel& cls) {
    std::string extends;
    for (size_t i = 0; i < cls.interfaceNames.size(); ++i) extends += (i ? ", " : "") + quote(cls.interfaceNames[i]);
    Definition definition;
    definition.name = cls.name;
    definition.package = cls.package;
    definition.outside = cls.outside;
    Lines out;
    flushComments(out, cls.line);
    out.add(definitionCall(definition) + "() => ({ interface: true" + (extends.empty() ? std::string() : ", extends: [" + extends + "]") +
                " }));",
            cls.line);
    return out.text();
}

std::string Translator::translateClass(ClassModel& cls) {
    Bindings own;
    Bindings* savedBindings = bindings;
    ClassModel* savedClass = currentClass;
    Unit* savedUnit = unit;
    bindings = &own;
    currentClass = &cls;
    unit = cls.unit;
    model.currentFile = cls.unit->fileName;
    own.taken.insert(cls.name);

    Lines head;
    flushComments(head, cls.line);
    const bool root = cls.superName.empty() || cls.superName == "Object";
    std::string superAlias;
    if (!root) superAlias = cls.superClass ? classRef(cls.superClass) : bind(cls.superName);

    // The class's members, in order.
    FunctionDefinition* constructor = nullptr;
    std::vector<std::pair<VariableDeclaration*, bool>> instanceFields;   // decl, isConst
    std::string fields;
    for (auto& node : cls.ast->members) {
        auto* function = dynamic_cast<FunctionDefinition*>(node.get());
        if (function && !hasAttribute(function->attributes, AttributeType::Static) && function->name->name == cls.name &&
            !function->isGetter && !function->isSetter) {
            constructor = function;
        }
        if (auto* variables = dynamic_cast<VariableDefinition*>(node.get())) {
            if (hasAttribute(variables->attributes, AttributeType::Static)) continue;
            for (auto& decl : variables->declarations) {
                const Member& member = cls.instance[decl->id->name];
                const std::string key = memberName(&cls, decl->id->name, member);
                std::string value = literalValue(decl->init.get());
                const Ty type = typeOf(member.type);
                if (!value.empty()) {
                    // A literal, as its type has it.
                    value = coerce(expr(decl->init.get()), type).s;
                } else {
                    value = defaultValue(type);
                    if (decl->init) instanceFields.emplace_back(decl.get(), variables->isConst);
                }
                fields += (fields.empty() ? "" : ", ") + fieldKey(key) + ": " + value;
            }
        }
    }

    Lines body;
    body.indent();
    body.add(std::string("constructor(...args) { ") + (root ? "" : "super(); ") + "if (new.target === " + cls.name +
             ") this.$construct(...args); }");
    body.blank();
    {
        Fn f;
        f.kind = Fn::Method;
        f.cls = &cls;
        f.isConstructor = true;
        static const std::vector<std::unique_ptr<Parameter>> none;
        const auto& params = constructor ? constructor->parameters : none;
        Statement* code = constructor ? constructor->body.get() : nullptr;
        BlockStatement empty{std::vector<StatementPtr>()};
        if (constructor) flushComments(body, constructor->line);
        const auto prologue = [&](Lines& lines) {
            for (const auto& field : instanceFields) {
                VariableDeclaration* decl = field.first;
                const Member& member = cls.instance[decl->id->name];
                const Ty type = typeOf(member.type);
                lines.add("this." + memberName(&cls, decl->id->name, member) + " = " +
                              paren(coerce(expr(decl->init.get()), type), P_Assign) + ";",
                          decl->init->line);
            }
            if (!root && (!code || !abc::callsSuper(code))) lines.add("super.$construct();");
        };
        prologueHook = prologue;
        body.add(functionText(params, code ? code : &empty, f, "$construct", false),
                 constructor ? constructor->line : cls.line);
        prologueHook = nullptr;
    }

    // Methods, getters and setters; statics after the class; static code
    // in init.
    Lines after;
    Lines init;
    init.indent();
    std::set<std::string> accessorsDone;
    Fn staticFn;
    staticFn.kind = Fn::Static;
    staticFn.cls = &cls;
    staticFn.isStatic = true;
    for (auto& node : cls.ast->members) {
        if (auto* function = dynamic_cast<FunctionDefinition*>(node.get())) {
            if (function == constructor) continue;
            const bool isStatic = hasAttribute(function->attributes, AttributeType::Static);
            const std::string& name = function->name->name;
            const Member& member = (isStatic ? cls.statics : cls.instance)[name];
            if (!function->body) continue;
            Fn f;
            f.kind = isStatic ? Fn::Static : Fn::Method;
            f.cls = &cls;
            f.isStatic = isStatic;
            f.returnType = function->isSetter ? Ty::of(Ty::Void)
                                              : typeOf(function->returnType ? function->returnType->name : std::string());
            f.returnsValue = function->returnType && f.returnType.kind != Ty::Void;
            const std::string key = methodKey(memberName(&cls, name, member));
            std::string head = std::string(isStatic ? "static " : "") + (function->isGetter ? "get " : function->isSetter ? "set " : "") + key;
            body.blank();
            flushComments(body, function->line);
            body.add(functionText(function->parameters, function->body.get(), f, head, false), function->line);
            // An accessor overriding half of one: the other half, as the
            // superclass has it.
            if (member.kind == Member::Accessor && !isStatic && !accessorsDone.count(name) &&
                (member.hasGetter != member.hasSetter)) {
                accessorsDone.insert(name);
                bool inherited = hasAttribute(function->attributes, AttributeType::Override);
                ClassModel* owner = nullptr;
                if (cls.superClass) {
                    if (const Member* base = findInstance(cls.superClass, name, &owner, true)) {
                        inherited = base->kind == Member::Accessor && (member.hasGetter ? base->hasSetter : base->hasGetter);
                    }
                }
                if (inherited) {
                    const std::string property = memberName(&cls, name, member);
                    if (member.hasGetter) body.add("set " + key + "(value) { " + dotted("super", property) + " = value; }");
                    else body.add("get " + key + "() { return " + dotted("super", property) + "; }");
                }
            }
        } else if (auto* variables = dynamic_cast<VariableDefinition*>(node.get())) {
            if (!hasAttribute(variables->attributes, AttributeType::Static)) continue;
            for (auto& decl : variables->declarations) {
                const Member& member = cls.statics[decl->id->name];
                const std::string property = memberName(&cls, decl->id->name, member);
                const Ty type = typeOf(member.type);
                const std::string literal = literalValue(decl->init.get());
                Fn* saved = fn;
                fn = &staticFn;
                if (!literal.empty()) {
                    after.add(dotted(cls.name, property) + " = " + coerce(make(literal, literal[0] == '-' ? P_Unary : P_Primary,
                                                                              literal == "null" ? Ty::of(Ty::Null) : Ty()),
                                                                         type)
                                                                      .s +
                                  ";",
                              decl->id->line > 0 ? decl->id->line : variables->line);
                } else {
                    after.add(dotted(cls.name, property) + " = " + defaultValue(type) + ";", variables->line);
                    if (decl->init) {
                        init.add(dotted(cls.name, property) + " = " + paren(coerce(expr(decl->init.get()), type), P_Assign) + ";",
                                 variables->line);
                    }
                }
                fn = saved;
            }
        } else if (auto* stmt = dynamic_cast<Statement*>(node.get())) {
            if (dynamic_cast<EmptyStatement*>(stmt) || dynamic_cast<NamespaceDefinition*>(stmt)) continue;
            Fn* saved = fn;
            fn = &staticFn;
            statement(stmt, init);
            fn = saved;
        }
    }
    // Literal statics come out right; their typed values are as coerce made them.

    Lines out;
    out.append(head);
    Definition definition;
    definition.name = cls.name;
    definition.package = cls.package;
    definition.outside = cls.outside;
    out.add(definitionCall(definition) + "() => {", cls.line);
    out.indent();
    // The superclass now, the rest when the class is first used.
    std::string superLine;
    std::vector<std::pair<std::string, std::string>> lazy;
    for (const auto& binding : own.order) {
        if (binding.first == superAlias) superLine = "const " + binding.first + " = " + binding.second + ";";
        else lazy.push_back(binding);
    }
    if (!superLine.empty()) out.add(superLine);
    std::string letLine;
    for (size_t i = 0; i < lazy.size(); ++i) letLine += (i ? ", " : "let ") + lazy[i].first;
    if (!letLine.empty()) out.add(letLine + ";");
    if (!superLine.empty() || !letLine.empty()) out.blank();
    out.add("class " + cls.name + (root ? std::string() : " extends " + superAlias) + " {", cls.line);
    out.append(body);
    out.add("}");
    out.append(after);
    std::string interfaces;
    for (size_t i = 0; i < cls.interfaceNames.size(); ++i) interfaces += (i ? ", " : "") + quote(cls.interfaceNames[i]);
    out.add("return {");
    out.indent();
    out.add("class: " + cls.name + ",");
    if (!interfaces.empty()) out.add("interfaces: [" + interfaces + "],");
    if (cls.isDynamic) out.add("dynamic: true,");
    if (cls.isFinal) out.add("final: true,");
    if (!fields.empty()) out.add("fields: { " + fields + " },");
    if (!lazy.empty() || !init.empty()) {
        out.add("init() {");
        out.indent();
        for (const auto& binding : lazy) out.add(binding.first + " = " + binding.second + ";");
        out.dedent();
        out.append(init);
        out.add("},");
    }
    out.dedent();
    out.add("};");
    out.dedent();
    out.add("});");

    bindings = savedBindings;
    currentClass = savedClass;
    unit = savedUnit;
    return out.text();
}

std::string Translator::translateTimeline(ClassModel& cls) {
    Bindings own;
    Bindings* savedBindings = bindings;
    bindings = &own;
    currentClass = &cls;
    unit = cls.unit;
    own.taken.insert(cls.name);
    const std::string superAlias = cls.superClass ? classRef(cls.superClass) : bind(cls.superName);

    // Stage instances and frame variables: fields at their defaults.
    std::string fields;
    for (const auto& entry : cls.instance) {
        if (entry.second.kind != Member::Var) continue;
        fields += (fields.empty() ? "" : ", ") + fieldKey(entry.first) + ": " + defaultValue(typeOf(entry.second.type));
    }
    Lines body;
    body.indent();
    body.add("constructor(...args) { super(); if (new.target === " + cls.name + ") this.$construct(...args); }");
    body.blank();
    std::string frames;
    for (size_t i = 0; i < cls.unit->members.size(); ++i) {
        const int frame = cls.unit->file->frames[i];
        frames += ", " + std::to_string(frame) + ", as3.bind(this, \"frame" + std::to_string(frame + 1) + "\")";
    }
    body.add("$construct() {");
    body.indent();
    body.add("super.$construct();");
    if (!frames.empty()) body.add("this.addFrameScript(" + frames.substr(2) + ");");
    body.dedent();
    body.add("}");
    for (size_t i = 0; i < cls.unit->members.size(); ++i) {
        auto* block = static_cast<BlockStatement*>(cls.unit->members[i]);
        const int frame = cls.unit->file->frames[i];
        model.currentFile = cls.qualified() + ", frame " + std::to_string(frame + 1);
        // The frame's functions: the timeline's methods.
        Scan scan;
        scanStatement(block, scan);
        for (FunctionDefinition* function : scan.functions) {
            Fn f;
            f.kind = Fn::Method;
            f.cls = &cls;
            f.returnType = typeOf(function->returnType ? function->returnType->name : std::string());
            f.returnsValue = function->returnType && f.returnType.kind != Ty::Void;
            body.blank();
            body.add(functionText(function->parameters, function->body.get(), f, methodKey(function->name->name), false),
                     function->line);
        }
        Fn f;
        f.kind = Fn::Frame;
        f.cls = &cls;
        static const std::vector<std::unique_ptr<Parameter>> none;
        body.blank();
        body.add(functionText(none, block, f, "frame" + std::to_string(frame + 1), false));
    }

    Lines out;
    out.add("as3.define(" + quote(cls.qualified()) + ", () => {");
    out.indent();
    std::string superLine;
    std::vector<std::pair<std::string, std::string>> lazy;
    for (const auto& binding : own.order) {
        if (binding.first == superAlias) superLine = "const " + binding.first + " = " + binding.second + ";";
        else lazy.push_back(binding);
    }
    out.add(superLine);
    std::string letLine;
    for (size_t i = 0; i < lazy.size(); ++i) letLine += (i ? ", " : "let ") + lazy[i].first;
    if (!letLine.empty()) out.add(letLine + ";");
    out.blank();
    out.add("class " + cls.name + " extends " + superAlias + " {");
    out.append(body);
    out.add("}");
    out.add("return {");
    out.indent();
    out.add("class: " + cls.name + ",");
    out.add("dynamic: true,");
    if (!fields.empty()) out.add("fields: { " + fields + " },");
    if (!lazy.empty()) {
        out.add("init() {");
        out.indent();
        for (const auto& binding : lazy) out.add(binding.first + " = " + binding.second + ";");
        out.dedent();
        out.add("},");
    }
    out.dedent();
    out.add("};");
    out.dedent();
    out.add("});");
    bindings = savedBindings;
    currentClass = nullptr;
    return out.text();
}

std::string Translator::translatePackageFunction(FunctionDefinition* function, const Definition& definition) {
    Bindings own;
    Bindings* saved = bindings;
    bindings = &own;
    const std::string name = localName(function->name->name);
    own.taken.insert(name);
    Fn f;
    f.kind = Fn::PackageCode;
    f.returnType = typeOf(function->returnType ? function->returnType->name : std::string());
    f.returnsValue = function->returnType && f.returnType.kind != Ty::Void;
    Lines out;
    flushComments(out, function->line);
    const std::string text = functionText(function->parameters, function->body.get(), f, "function " + name, false);
    out.add(definitionCall(definition) + "() => {", function->line);
    out.indent();
    const std::string lets = bindingLines(std::string());
    if (!lets.empty()) out.add(lets);
    out.add(text);
    if (own.order.empty()) {
        out.add("return { value: " + name + " };");
    } else {
        out.add("return {");
        out.indent();
        out.add("value: " + name + ",");
        out.add("init() {");
        out.indent();
        out.add(bindingLines("="));
        out.dedent();
        out.add("},");
        out.dedent();
        out.add("};");
    }
    out.dedent();
    out.add("});");
    bindings = saved;
    return out.text();
}

std::string Translator::translatePackageVariable(VariableDeclaration* decl, const Definition& definition, bool isConst) {
    Bindings own;
    Bindings* saved = bindings;
    bindings = &own;
    Fn f;
    f.kind = Fn::PackageCode;
    Fn* savedFn = fn;
    fn = &f;
    const Ty type = typeOf(definition.type);
    const std::string value = decl->init ? coerce(expr(decl->init.get()), type).s : defaultValue(type);
    fn = savedFn;
    Lines out;
    flushComments(out, decl->id->line);
    out.add(definitionCall(definition) + "() => ({", decl->id->line);
    out.indent();
    if (isConst) out.add("constant: true,");
    else out.add("variable: true,");
    out.add("init() {");
    out.indent();
    if (!own.order.empty()) out.add(bindingLines(std::string()) + "\n" + bindingLines("="));
    out.add("return " + value + ";");
    out.dedent();
    out.add("},");
    out.dedent();
    out.add("}));");
    bindings = saved;
    return out.text();
}

std::string Translator::translateFile(SourceFile& source) {
    file = &source;
    model.currentFile = source.fileName;
    const std::string& text = sourceTexts[source.fileName];
    comments = options.comments ? scanComments(text) : std::vector<Comment>();
    commentAt = 0;
    sourceLines.clear();
    {
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) sourceLines.push_back(line);
    }
    Lines out;
    if (source.timeline) {
        for (ClassModel* cls : source.classes) {
            out.add(translateTimeline(*cls));
            output.classes.push_back(cls->qualified());
        }
        return out.text();
    }
    // The packages' definitions, then the file's own outside them.
    Lines definitions;
    Lines privates;
    Unit* outsideUnit = nullptr;
    std::vector<Statement*> code;
    std::vector<FunctionDefinition*> functions;
    for (auto& entry : source.units) {
        Unit* u = entry.get();
        unit = u;
        if (u->outside) outsideUnit = u;
        for (AstNode* node : u->members) {
            if (auto* definition = dynamic_cast<ClassDefinition*>(node)) {
                ClassModel* cls = nullptr;
                for (ClassModel* c : source.classes) {
                    if (c->ast == definition) cls = c;
                }
                if (!cls) continue;
                Lines& into = cls->outside ? privates : definitions;
                into.blank();
                into.add(cls->isInterface ? translateInterface(*cls) : translateClass(*cls));
                if (!cls->outside) output.classes.push_back(cls->qualified());
            } else if (auto* function = dynamic_cast<FunctionDefinition*>(node)) {
                if (u->outside) {
                    functions.push_back(function);
                    continue;
                }
                const Definition* definition = model.findDefinition(u, function->name->name);
                if (!definition) continue;
                definitions.blank();
                definitions.add(translatePackageFunction(function, *definition));
            } else if (auto* variables = dynamic_cast<VariableDefinition*>(node)) {
                if (u->outside) {
                    code.push_back(variables);
                    continue;
                }
                for (auto& decl : variables->declarations) {
                    const Definition* definition = model.findDefinition(u, decl->id->name);
                    if (!definition) continue;
                    definitions.blank();
                    definitions.add(translatePackageVariable(decl.get(), *definition, variables->isConst));
                }
            } else if (dynamic_cast<NamespaceDefinition*>(node)) {
                continue;
            } else if (auto* stmt = dynamic_cast<Statement*>(node)) {
                if (u->outside) code.push_back(stmt);
            }
        }
    }
    const bool outside = !privates.empty() || !code.empty() || !functions.empty();
    if (!outside) return definitions.text();

    // The file's code: its variables and functions the file's, its
    // statements run with as3.runScripts().
    unit = outsideUnit;
    Bindings scriptBindings;
    bindings = &scriptBindings;
    Fn script;
    script.kind = Fn::Script;
    fn = nullptr;
    prepareTops(script, code);
    for (FunctionDefinition* function : functions) {
        script.locals[function->name->name] = Local{localName(function->name->name), Ty::of(Ty::Function)};
        script.declaration[function->name->name] = 'f';
    }
    Lines functionLines;
    for (FunctionDefinition* function : functions) {
        Fn f;
        f.kind = Fn::PackageCode;
        f.returnType = typeOf(function->returnType ? function->returnType->name : std::string());
        f.returnsValue = function->returnType && f.returnType.kind != Ty::Void;
        fn = &script;
        functionLines.blank();
        flushComments(functionLines, function->line);
        functionLines.add(functionText(function->parameters, function->body.get(), f,
                                       "function " + localName(function->name->name), false),
                          function->line);
        fn = nullptr;
    }
    Lines body;
    body.indent();
    fn = &script;
    for (Statement* stmt : code) statement(stmt, body);
    fn = nullptr;
    std::string vars;
    for (const auto& local : script.locals) {
        if (script.declaration[local.first] != 'h') continue;
        vars += (vars.empty() ? "var " : ", ") + local.second.js + " = " + defaultValue(local.second.type);
    }

    out.add("(() => {");
    out.indent();
    if (!privates.empty()) {
        out.add("const $private = as3.file(" + quote(source.fileName) + ");");
        out.append(privates);
    }
    out.append(definitions);
    if (!definitions.empty() || !privates.empty()) out.blank();
    const std::string lets = bindingLines(std::string());
    if (!lets.empty()) out.add(lets);
    if (!vars.empty()) out.add(vars + ";");
    out.append(functionLines);
    out.blank();
    out.add("as3.script(" + quote(source.fileName) + ", () => {");
    out.indent();
    const std::string assigns = bindingLines("=");
    if (!assigns.empty()) out.add(assigns);
    out.dedent();
    out.append(body);
    out.add("});");
    out.dedent();
    out.add("})();");
    bindings = nullptr;
    return out.text();
}

Output Translator::run(const std::vector<Source>& sources, const std::vector<Timeline>& timelines) {
    if (sources.empty() && timelines.empty()) {
        error("nothing to translate", 0);
        return output;
    }
    for (const Source& source : sources) sourceTexts[source.fileName] = source.text;
    model.parseSources(sources);
    for (auto& source : model.files) model.collectDefinitions(*source);
    for (const Timeline& timeline : timelines) model.buildTimeline(timeline);
    model.resolveClasses();
    if (model.hasErrors()) return output;
    for (auto& cls : model.classes) {
        if (cls->superClass) subclasses[cls->superClass].push_back(cls.get());
    }

    std::vector<std::string> order;
    for (auto& source : model.files) {
        std::string path;
        if (source->timeline) {
            path = source->classes.empty() ? source->fileName : source->classes.front()->qualified();
            std::replace(path.begin(), path.end(), '.', '/');
            path += ".js";
        } else {
            for (ClassModel* cls : source->classes) {
                if (!cls->outside) {
                    path = cls->qualified();
                    std::replace(path.begin(), path.end(), '.', '/');
                    path += ".js";
                    break;
                }
            }
            if (path.empty()) {
                path = source->fileName;
                const size_t dot = path.rfind(".as");
                if (dot != std::string::npos && dot + 3 == path.size()) path = path.substr(0, dot);
                path += ".js";
            }
        }
        while (output.files.count(path)) path = path.substr(0, path.size() - 3) + "_.js";
        const std::string body = translateFile(*source);
        output.files[path] = fileHeader(source->timeline ? source->fileName : source->fileName) + body + "\n";
        order.push_back(path);
    }
    output.ok = !model.hasErrors();
    if (!output.ok) {
        output.files.clear();
        return output;
    }

    // Marks out, and the source maps.
    std::vector<std::string> bundleSources;
    std::vector<int> bundleSourceOf;
    std::vector<int> bundleLines;
    std::string bundle;
    for (size_t i = 0; i < order.size(); ++i) {
        const std::string& path = order[i];
        std::vector<int> lines;
        std::string clean = unmark(output.files[path], lines);
        std::string sourceName;
        for (auto& source : model.files) {
            std::string candidate = path;
            (void)candidate;
        }
        sourceName = model.files[i]->timeline ? model.files[i]->fileName : model.files[i]->fileName;
        if (options.sourceMaps) {
            const std::string mapName = path + ".map";
            std::string base = path;
            const size_t slash = base.find_last_of('/');
            if (slash != std::string::npos) base = base.substr(slash + 1);
            output.sourceMaps[mapName] = sourceMap(base, {sourceName}, std::vector<int>(lines.size(), 0), lines);
            clean += "//# sourceMappingURL=" + base + ".map\n";
        }
        output.files[path] = clean;
        bundleSources.push_back(sourceName);
        for (size_t j = 0; j + 1 < lines.size(); ++j) {
            bundleSourceOf.push_back(static_cast<int>(i));
            bundleLines.push_back(lines[j]);
        }
        std::vector<int> ignored;
        std::string plain = unmark(output.files[path], ignored);
        if (options.sourceMaps) {
            const size_t at = plain.rfind("//# sourceMappingURL=");
            if (at != std::string::npos) plain = plain.substr(0, at);
        }
        bundle += plain;
    }
    bundle += "as3.runScripts();\n";
    if (options.sourceMaps) {
        output.sourceMaps["bundle.js.map"] = sourceMap("bundle.js", bundleSources, bundleSourceOf, bundleLines);
        bundle += "//# sourceMappingURL=bundle.js.map\n";
    }
    output.bundle = bundle;
    return output;
}

} // namespace

Output translate(const std::vector<Source>& sources, const std::vector<Timeline>& timelines, const Options& options) {
    Translator translator(options);
    return translator.run(sources, timelines);
}

} // namespace js
} // namespace as3
