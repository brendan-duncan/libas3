#pragma once

// What the compilers know of a program before they make anything of it:
// its files parsed, the definitions of their packages, the classes with
// their members, superclasses and interfaces resolved, and the timelines'
// classes made from their frame scripts. The ABC compiler (abc.h) and the
// JavaScript translator (js/js.h) both start from this.
//
//   Model model(options.constants, options.includeHandler, diagnostics);
//   model.parseSources(sources);
//   for (auto& file : model.files) model.collectDefinitions(*file);
//   for (const Timeline& timeline : timelines) model.buildTimeline(timeline);
//   model.resolveClasses();

#include "abc.h"
#include "../ast/ast_nodes.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace as3 {
namespace abc {

enum class Access { Public, Private, Protected, Internal };

struct Member {
    std::string name;
    enum Kind { Var, Const, Method, Accessor };
    Kind kind = Var;
    bool isStatic = false;
    Access access = Access::Internal;
    // As written: "int", "flash.display.Sprite", "Vector.<String>"; empty
    // for none. A getter's is its return type.
    std::string type;
    Expression* init = nullptr;
    // An accessor's halves, as declared.
    bool hasGetter = false;
    bool hasSetter = false;
};

struct Unit;

struct ClassModel {
    std::string package;
    std::string name;
    bool isInterface = false;
    bool isFinal = false;
    bool isDynamic = false;
    bool isPublic = true;
    // Defined outside the file's package: only the file sees it.
    bool outside = false;
    ClassDefinition* ast = nullptr;
    Unit* unit = nullptr;
    const Timeline* timeline = nullptr;
    int line = 0;
    // Qualified with a dot; empty for none (an interface).
    std::string superName;
    // The superclass when it is one of the sources' (null for a player's).
    ClassModel* superClass = nullptr;
    // What it implements (or, an interface, extends): qualified, and the
    // sources' model of each (null for a player's).
    std::vector<std::string> interfaceNames;
    std::vector<ClassModel*> interfaceModels;
    std::map<std::string, Member> instance;
    std::map<std::string, Member> statics;

    std::string qualified() const { return package.empty() ? name : package + "." + name; }
};

struct SourceFile;

// A definition at a package's or file's top: a class, function or
// variable, found by name.
struct Definition {
    enum Kind { Class, Function, Variable };
    Kind kind = Class;
    ClassModel* cls = nullptr;
    std::string name;
    std::string package;
    bool isPublic = false;
    // Outside the file's package: only the file sees it.
    bool outside = false;
    SourceFile* file = nullptr;
    std::string type;
    bool isConst = false;
    Expression* init = nullptr;
};

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
};

struct SourceFile {
    std::string fileName;
    std::unique_ptr<Program> program;
    std::vector<std::unique_ptr<Unit>> units;
    // Definitions outside the packages, which only the file sees.
    std::map<std::string, Definition> privateDefinitions;
    std::vector<ClassModel*> classes;
    std::vector<FunctionDefinition*> functions;   // a package's, or outside one
    bool timeline = false;
    // A timeline's frames, one for each of its program's blocks.
    std::vector<int> frames;
};

// A type as written, resolved.
struct TypeName {
    enum Kind { Any, Void, Int, Uint, Number, Boolean, String, Object, Class, Vector };
    Kind kind = Any;
    // Class: qualified with a dot. Int to Object: the builtin's name.
    std::string qualified;
    // Class: the definition, when it is one of the sources'.
    const Definition* definition = nullptr;
    // Vector: its element type.
    std::shared_ptr<TypeName> element;
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
// Whether a constructor's body calls super(), outside nested functions.
bool callsSuper(Statement* body);

// The predicate of an E4X filter, as the parser makes it:
// $xmlFilter(list, function($item) { with ($item) return predicate; }).
Expression* filterPredicate(CallExpression* call);

Access accessOf(const std::vector<AttributePtr>& attributes, Access otherwise);
std::string packageOf(const std::string& qualified);
std::string shortOf(const std::string& qualified);
// A number as ActionScript writes it, for an object literal's numeric key.
std::string numberText(double value);

class Model {
public:
    Model(const Options& options, std::vector<Diagnostic>& diagnostics) : options(options), diagnostics(diagnostics) {}

    // Building it, in this order.
    void parseSources(const std::vector<Source>& sources);
    void collectDefinitions(SourceFile& source);
    void buildTimeline(const Timeline& timeline);
    void resolveClasses();

    std::vector<std::unique_ptr<SourceFile>> files;
    std::vector<std::unique_ptr<ClassModel>> classes;
    std::map<std::string, Definition> globals;

    // Finding names.
    ClassModel* sourceClass(const std::string& qualified) const;
    ClassModel* superOf(const ClassModel* cls) const { return cls ? cls->superClass : nullptr; }
    const Definition* findDefinition(Unit* unit, const std::string& name, std::string* package = nullptr) const;
    bool isPackage(Unit* unit, const std::string& name) const;
    // From Object down to the direct superclass, as far as known.
    std::vector<std::string> superChain(const ClassModel* cls) const;
    // A type as written ("int", "Sprite", "Vector.<Point>"); one not known
    // is Any, with a warning unless quiet.
    TypeName resolveType(const std::string& written, Unit* unit, bool quiet = false);

    // Diagnostics, for the file and line being worked on.
    std::string currentFile;
    int currentLine = 0;
    void error(const std::string& message, int line = -1) {
        diagnostics.push_back({true, currentFile, line < 0 ? currentLine : line, message});
    }
    void warning(const std::string& message, int line = -1) {
        diagnostics.push_back({false, currentFile, line < 0 ? currentLine : line, message});
    }
    bool hasErrors() const {
        for (const Diagnostic& diagnostic : diagnostics) {
            if (diagnostic.isError) return true;
        }
        return false;
    }

private:
    const Options& options;
    std::vector<Diagnostic>& diagnostics;

    void addClass(ClassDefinition* definition, Unit* unit, bool outside);
    void addMember(ClassModel& cls, const std::string& name, Member member);
};

} // namespace abc
} // namespace as3
