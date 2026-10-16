#ifndef AST_NODES_H
#define AST_NODES_H

#include <string>
#include <vector>
#include <memory>
#include <utility>

namespace as3 {

enum class AstNodeType {
    Program,
    Expression,
    Statement,
    ClassDefinition,
    FunctionDefinition,
    VariableDefinition,
    Parameter,
    ImportDirective,
    PackageDefinition,
};

class AstNode {
public:
    virtual ~AstNode() = default;
    virtual AstNodeType getType() const = 0;
};

using AstNodePtr = std::unique_ptr<AstNode>;
using NodeList = std::vector<AstNodePtr>;

class Expression;
class Statement;
class Parameter;

using ExpressionPtr = std::unique_ptr<Expression>;
using StatementPtr = std::unique_ptr<Statement>;
using ExpressionList = std::vector<ExpressionPtr>;

class Expression : public AstNode {
public:
    enum class ExpressionKind {
        Identifier, Literal, Binary, Unary, Conditional, Assignment,
        Call, New, Member, Function, Array, Object, This, Sequence,
        Super, TypeApplication
    };
    ExpressionKind kind;
    // The line it starts on, from 1; 0 when not known.
    int line = 0;

    explicit Expression(ExpressionKind k)
        : kind(k) {
    }
    AstNodeType getType() const override { return AstNodeType::Expression; }
};

class Statement : public AstNode {
public:
    AstNodeType getType() const override { return AstNodeType::Statement; }

    // The line it starts on, from 1; 0 when not known.
    int line = 0;
};

enum class OperatorType {
    Add, Subtract, Multiply, Divide, Remainder,
    ShiftLeft, ShiftRight, ShiftRightUnsigned,
    Lt, Gt, Le, Ge,
    Equals, NotEquals, StrictEquals, StrictNotEquals,
    BitwiseAnd, BitwiseXor, BitwiseOr,
    LogicalAnd, LogicalOr, LogicalXor,
    Instanceof, In, Is, As,
    NullCoalescing,
    Power,
    PostIncrement, PostDecrement,
    PreIncrement, PreDecrement,
    Positive, Negative, BitwiseNot, LogicalNot,
    NonNull,
    Delete, Void, Typeof,
    // A plain = (compound assignments carry their operator).
    Assign,
};

// A name. As a type annotation it is the type as written, qualified or
// not: "int", "*", "flash.display.Sprite", "Vector.<int>".
class Identifier : public Expression {
public:
    std::string name;
    std::string namespacePrefix;

    explicit Identifier(const std::string& n)
        : Expression(ExpressionKind::Identifier)
        , name(n) {
    }
    AstNodeType getType() const override { return AstNodeType::Expression; }
};

using IdentifierPtr = std::unique_ptr<Identifier>;

class Literal : public Expression {
public:
    enum class LiteralKind {
        Null, Boolean, Numeric, String, RegExp, Xml, Array, Object, Vector, Undefined
    };
    LiteralKind kind;
    enum class LiteralValueType { None, Bool, Number, String };
    LiteralValueType valueType;
    bool boolValue;
    double numberValue;
    // A string's text, or a regular expression's pattern.
    std::string stringValue;
    // A regular expression's flags.
    std::string flags;

    explicit Literal(LiteralKind k)
        : Expression(ExpressionKind::Literal)
        , kind(k)
        , valueType(LiteralValueType::None)
        , boolValue(false)
        , numberValue(0) {
    }
    AstNodeType getType() const override { return AstNodeType::Expression; }
};

using LiteralPtr = std::unique_ptr<Literal>;

class BinaryExpression : public Expression {
public:
    OperatorType op;
    ExpressionPtr left;
    ExpressionPtr right;

    BinaryExpression(OperatorType operation, ExpressionPtr l, ExpressionPtr r)
        : Expression(ExpressionKind::Binary)
        , op(operation)
        , left(std::move(l))
        , right(std::move(r)) {
    }
    AstNodeType getType() const override { return AstNodeType::Expression; }
};

class UnaryExpression : public Expression {
public:
    OperatorType op;
    ExpressionPtr operand;
    bool isPostfix;

    UnaryExpression(OperatorType operation, ExpressionPtr expr, bool postfix)
        : Expression(ExpressionKind::Unary)
        , op(operation)
        , operand(std::move(expr))
        , isPostfix(postfix) {
    }
    AstNodeType getType() const override { return AstNodeType::Expression; }
};

class ConditionalExpression : public Expression {
public:
    ExpressionPtr test;
    ExpressionPtr consequent;
    ExpressionPtr alternate;

    ConditionalExpression(ExpressionPtr t, ExpressionPtr c, ExpressionPtr a)
        : Expression(ExpressionKind::Conditional)
        , test(std::move(t))
        , consequent(std::move(c))
        , alternate(std::move(a)) {}
    AstNodeType getType() const override { return AstNodeType::Expression; }
};

class AssignmentExpression : public Expression {
public:
    OperatorType op;
    ExpressionPtr left;
    ExpressionPtr right;

    AssignmentExpression(OperatorType operation, ExpressionPtr l, ExpressionPtr r)
        : Expression(ExpressionKind::Assignment)
        , op(operation)
        , left(std::move(l))
        , right(std::move(r)) {
    }
    AstNodeType getType() const override { return AstNodeType::Expression; }
};

class CallExpression : public Expression {
public:
    ExpressionPtr callee;
    ExpressionList arguments;

    CallExpression(ExpressionPtr c, ExpressionList args)
        : Expression(ExpressionKind::Call)
        , callee(std::move(c))
        , arguments(std::move(args)) {
    }
    AstNodeType getType() const override { return AstNodeType::Expression; }
};

class NewExpression : public Expression {
public:
    ExpressionPtr callee;
    ExpressionList arguments;

    NewExpression(ExpressionPtr c, ExpressionList args)
        : Expression(ExpressionKind::New)
        , callee(std::move(c))
        , arguments(std::move(args)) {
    }
    AstNodeType getType() const override { return AstNodeType::Expression; }
};

class MemberExpression : public Expression {
public:
    ExpressionPtr object;
    IdentifierPtr property;
    ExpressionPtr computedProperty;  // For bracket notation
    bool computed;
    bool optional;

    MemberExpression(ExpressionPtr obj, IdentifierPtr prop, bool comp = false, bool opt = false)
        : Expression(ExpressionKind::Member)
        , object(std::move(obj))
        , property(std::move(prop))
        , computed(comp), optional(opt) {
    }
    MemberExpression(ExpressionPtr obj, ExpressionPtr compProp, bool opt = false)
        : Expression(ExpressionKind::Member)
        , object(std::move(obj))
        , computedProperty(std::move(compProp))
        , computed(true)
        , optional(opt) {
    }
    AstNodeType getType() const override { return AstNodeType::Expression; }
};

class FunctionExpression : public Expression {
public:
    IdentifierPtr name;
    std::vector<std::unique_ptr<Parameter>> parameters;
    IdentifierPtr returnType;
    StatementPtr body;

    FunctionExpression(IdentifierPtr n, std::vector<std::unique_ptr<Parameter>> params, IdentifierPtr ret, StatementPtr b)
        : Expression(ExpressionKind::Function)
        , name(std::move(n))
        , parameters(std::move(params))
        , returnType(std::move(ret))
        , body(std::move(b)) {
    }
    AstNodeType getType() const override { return AstNodeType::Expression; }
};

// [a, b, c]; with a vector type, new <int>[a, b, c]. A hole, as in [a,,b],
// is a null element.
class ArrayExpression : public Expression {
public:
    ExpressionList elements;
    std::string vectorType;

    explicit ArrayExpression(ExpressionList elems)
        : Expression(ExpressionKind::Array)
        , elements(std::move(elems)) {
    }
    AstNodeType getType() const override { return AstNodeType::Expression; }
};

class ObjectExpression : public Expression {
public:
    std::vector<std::pair<ExpressionPtr, ExpressionPtr>> properties;

    explicit ObjectExpression(std::vector<std::pair<ExpressionPtr, ExpressionPtr>> props)
        : Expression(ExpressionKind::Object)
        , properties(std::move(props)) {
    }
    AstNodeType getType() const override { return AstNodeType::Expression; }
};

class ThisExpression : public Expression {
public:
    ThisExpression() : Expression(ExpressionKind::This) {}
    AstNodeType getType() const override { return AstNodeType::Expression; }
};

// super, in super(...), super.name and super.name(...).
class SuperExpression : public Expression {
public:
    SuperExpression() : Expression(ExpressionKind::Super) {}
    AstNodeType getType() const override { return AstNodeType::Expression; }
};

// a, b: each evaluated, the last one's value given.
class SequenceExpression : public Expression {
public:
    ExpressionList expressions;

    explicit SequenceExpression(ExpressionList list)
        : Expression(ExpressionKind::Sequence)
        , expressions(std::move(list)) {
    }
    AstNodeType getType() const override { return AstNodeType::Expression; }
};

// Vector.<int>: a parameterized type, its arguments as written.
class TypeApplication : public Expression {
public:
    ExpressionPtr base;
    std::vector<std::string> typeArguments;

    TypeApplication(ExpressionPtr b, std::vector<std::string> args)
        : Expression(ExpressionKind::TypeApplication)
        , base(std::move(b))
        , typeArguments(std::move(args)) {
    }
    AstNodeType getType() const override { return AstNodeType::Expression; }
};

class BlockStatement : public Statement {
public:
    std::vector<StatementPtr> statements;

    explicit BlockStatement(std::vector<StatementPtr> stmts)
        : statements(std::move(stmts)) {
    }
    AstNodeType getType() const override { return AstNodeType::Statement; }
};

class EmptyStatement : public Statement {
public:
    AstNodeType getType() const override { return AstNodeType::Statement; }
};

class ExpressionStatement : public Statement {
public:
    ExpressionPtr expression;

    explicit ExpressionStatement(ExpressionPtr expr) : expression(std::move(expr)) {}
    AstNodeType getType() const override { return AstNodeType::Statement; }
};

class IfStatement : public Statement {
public:
    ExpressionPtr test;
    StatementPtr consequent;
    StatementPtr alternate;

    IfStatement(ExpressionPtr t, StatementPtr c, StatementPtr a = nullptr)
        : test(std::move(t))
        , consequent(std::move(c))
        , alternate(std::move(a)) {
    }
    AstNodeType getType() const override { return AstNodeType::Statement; }
};

class WhileStatement : public Statement {
public:
    ExpressionPtr test;
    StatementPtr body;

    WhileStatement(ExpressionPtr t, StatementPtr b)
        : test(std::move(t))
        , body(std::move(b)) {
    }
    AstNodeType getType() const override { return AstNodeType::Statement; }
};

class DoWhileStatement : public Statement {
public:
    StatementPtr body;
    ExpressionPtr test;

    DoWhileStatement(StatementPtr b, ExpressionPtr t)
        : body(std::move(b))
        , test(std::move(t)) {
    }
    AstNodeType getType() const override { return AstNodeType::Statement; }
};

class ForStatement : public Statement {
public:
    StatementPtr init;
    ExpressionPtr test;
    ExpressionPtr update;
    StatementPtr body;

    ForStatement(StatementPtr i, ExpressionPtr t, ExpressionPtr u, StatementPtr b)
        : init(std::move(i))
        , test(std::move(t))
        , update(std::move(u))
        , body(std::move(b)) {
    }
    AstNodeType getType() const override { return AstNodeType::Statement; }
};

// for (x in o) and, with each, for each (x in o). The left is a variable
// definition of one name, or an expression statement giving the target.
class ForInStatement : public Statement {
public:
    StatementPtr left;
    ExpressionPtr right;
    StatementPtr body;
    bool each = false;

    ForInStatement(StatementPtr l, ExpressionPtr r, StatementPtr b, bool isEach = false)
        : left(std::move(l))
        , right(std::move(r))
        , body(std::move(b))
        , each(isEach) {
    }
    AstNodeType getType() const override { return AstNodeType::Statement; }
};

class ForOfStatement : public Statement {
public:
    ExpressionPtr left;
    ExpressionPtr right;
    StatementPtr body;

    ForOfStatement(ExpressionPtr l, ExpressionPtr r, StatementPtr b)
        : left(std::move(l))
        , right(std::move(r))
        , body(std::move(b)) {
    }
    AstNodeType getType() const override { return AstNodeType::Statement; }
};

class CaseClause : public Statement {
public:
    ExpressionPtr test;  // null for default case
    std::vector<StatementPtr> consequent;

    CaseClause(ExpressionPtr t, std::vector<StatementPtr> c)
        : test(std::move(t))
        , consequent(std::move(c)) {
    }
    AstNodeType getType() const override { return AstNodeType::Statement; }
};

class SwitchStatement : public Statement {
public:
    ExpressionPtr discriminant;
    std::vector<std::unique_ptr<CaseClause>> cases;

    SwitchStatement(ExpressionPtr d, std::vector<std::unique_ptr<CaseClause>> c)
        : discriminant(std::move(d))
        , cases(std::move(c)) {
    }
    AstNodeType getType() const override { return AstNodeType::Statement; }
};

class ContinueStatement : public Statement {
public:
    IdentifierPtr label;

    explicit ContinueStatement(IdentifierPtr l = nullptr)
        : label(std::move(l)) {
    }
    AstNodeType getType() const override { return AstNodeType::Statement; }
};

class BreakStatement : public Statement {
public:
    IdentifierPtr label;

    explicit BreakStatement(IdentifierPtr l = nullptr)
        : label(std::move(l)) {
    }
    AstNodeType getType() const override { return AstNodeType::Statement; }
};

class ReturnStatement : public Statement {
public:
    ExpressionPtr argument;

    explicit ReturnStatement(ExpressionPtr arg = nullptr)
        : argument(std::move(arg)) {
    }
    AstNodeType getType() const override { return AstNodeType::Statement; }
};

class ThrowStatement : public Statement {
public:
    ExpressionPtr argument;

    explicit ThrowStatement(ExpressionPtr arg)
        : argument(std::move(arg)) {
    }
    AstNodeType getType() const override { return AstNodeType::Statement; }
};

// catch (name:Type) { body }; the type is empty when not given.
struct CatchClause {
    IdentifierPtr variable;
    std::string type;
    StatementPtr body;
};

class TryStatement : public Statement {
public:
    StatementPtr block;
    std::vector<CatchClause> catches;
    StatementPtr finallyBody;

    TryStatement(StatementPtr b, std::vector<CatchClause> c, StatementPtr fb)
        : block(std::move(b))
        , catches(std::move(c))
        , finallyBody(std::move(fb)) {
    }
    AstNodeType getType() const override { return AstNodeType::Statement; }
};

class WithStatement : public Statement {
public:
    ExpressionPtr object;
    StatementPtr body;

    WithStatement(ExpressionPtr obj, StatementPtr b)
        : object(std::move(obj))
        , body(std::move(b)) {
    }
    AstNodeType getType() const override { return AstNodeType::Statement; }
};

class LabeledStatement : public Statement {
public:
    IdentifierPtr label;
    StatementPtr body;

    LabeledStatement(IdentifierPtr l, StatementPtr b)
        : label(std::move(l))
        , body(std::move(b)) {
    }
    AstNodeType getType() const override { return AstNodeType::Statement; }
};

enum class AttributeType {
    Public, Private, Protected, Internal,
    Static, Override, Final, Native,
    Dynamic, FinalClass, InternalNamespace
};

class Attribute : public AstNode {
public:
    AttributeType type;

    explicit Attribute(AttributeType t) : type(t) {}
    AstNodeType getType() const override { return AstNodeType::Statement; }
};

using AttributePtr = std::unique_ptr<Attribute>;

// Whether attributes include one of a type.
inline bool hasAttribute(const std::vector<AttributePtr>& attributes, AttributeType type) {
    for (const auto& attribute : attributes) {
        if (attribute && attribute->type == type) {
            return true;
        }
    }
    return false;
}

// A metadata tag, as [Embed(source="a.png")] or [Event(name="change",
// type="flash.events.Event")]: its name, and its arguments as key and value
// (the key empty for one given without a name).
struct Metadata {
    std::string name;
    std::vector<std::pair<std::string, std::string>> arguments;
};

class VariableDeclaration : public AstNode {
public:
    IdentifierPtr id;
    IdentifierPtr type;
    ExpressionPtr init;

    VariableDeclaration(IdentifierPtr i, IdentifierPtr t = nullptr, ExpressionPtr in = nullptr)
        : id(std::move(i))
        , type(std::move(t))
        , init(std::move(in)) {
    }
    AstNodeType getType() const override { return AstNodeType::Statement; }
};

using VariableDeclarationList = std::vector<std::unique_ptr<VariableDeclaration>>;

class VariableDefinition : public Statement {
public:
    std::vector<AttributePtr> attributes;
    VariableDeclarationList declarations;
    bool isConst;
    std::vector<Metadata> metadata;

    VariableDefinition(std::vector<AttributePtr> attrs, VariableDeclarationList decls, bool constFlag)
        : attributes(std::move(attrs))
        , declarations(std::move(decls))
        , isConst(constFlag) {
    }
    AstNodeType getType() const override { return AstNodeType::VariableDefinition; }
};

class Parameter : public AstNode {
public:
    IdentifierPtr name;
    IdentifierPtr type;
    ExpressionPtr defaultValue;
    bool rest;

    Parameter(IdentifierPtr n, IdentifierPtr t = nullptr, ExpressionPtr def = nullptr, bool r = false)
        : name(std::move(n))
        , type(std::move(t))
        , defaultValue(std::move(def))
        , rest(r) {
    }
    AstNodeType getType() const override { return AstNodeType::Parameter; }
};

class FunctionDefinition : public Statement {
public:
    std::vector<AttributePtr> attributes;
    IdentifierPtr name;
    std::vector<std::unique_ptr<Parameter>> parameters;
    IdentifierPtr returnType;
    // Null for a method an interface declares, or a native one.
    StatementPtr body;
    bool isGetter;
    bool isSetter;
    std::vector<Metadata> metadata;

    FunctionDefinition(
        std::vector<AttributePtr> attrs,
        IdentifierPtr n,
        std::vector<std::unique_ptr<Parameter>> params,
        IdentifierPtr ret,
        StatementPtr b,
        bool getter = false,
        bool setter = false)
        : attributes(std::move(attrs))
        , name(std::move(n))
        , parameters(std::move(params))
        , returnType(std::move(ret))
        , body(std::move(b))
        , isGetter(getter), isSetter(setter) {
    }
    AstNodeType getType() const override { return AstNodeType::FunctionDefinition; }
};

// A class, or with isInterface an interface (whose superclass is empty and
// whose interfaces are the ones it extends).
class ClassDefinition : public Statement {
public:
    std::vector<AttributePtr> attributes;
    IdentifierPtr name;
    IdentifierPtr superClass;
    std::vector<IdentifierPtr> interfaces;
    std::vector<AstNodePtr> members;
    bool isInterface = false;
    std::vector<Metadata> metadata;

    ClassDefinition(
        std::vector<AttributePtr> attrs,
        IdentifierPtr n,
        IdentifierPtr super_,
        std::vector<IdentifierPtr> interfaces_,
        std::vector<AstNodePtr> mems)
        : attributes(std::move(attrs))
        , name(std::move(n))
        , superClass(std::move(super_))
        , interfaces(std::move(interfaces_))
        , members(std::move(mems)) {
    }
    AstNodeType getType() const override { return AstNodeType::ClassDefinition; }
};

class NamespaceDefinition : public Statement {
public:
    IdentifierPtr name;
    IdentifierPtr uri;

    NamespaceDefinition(IdentifierPtr n, IdentifierPtr u)
        : name(std::move(n))
        , uri(std::move(u)) {
    }
    AstNodeType getType() const override { return AstNodeType::Statement; }
};

// import a.b.C (path "a.b", name "C") or import a.b.* (path "a.b", name
// "*", isPackage).
class ImportDirective : public AstNode {
public:
    std::string path;
    IdentifierPtr name;
    bool isPackage;

    ImportDirective(const std::string& p, IdentifierPtr n, bool isPkg)
        : path(p)
        , name(std::move(n))
        , isPackage(isPkg) {
    }
    AstNodeType getType() const override { return AstNodeType::ImportDirective; }
};

using ImportDirectivePtr = std::unique_ptr<ImportDirective>;

class PackageDefinition : public AstNode {
public:
    IdentifierPtr name;
    std::vector<ImportDirectivePtr> imports;
    std::vector<AstNodePtr> members;

    PackageDefinition(IdentifierPtr n, std::vector<ImportDirectivePtr> imps, std::vector<AstNodePtr> mems)
        : name(std::move(n))
        , imports(std::move(imps))
        , members(std::move(mems)) {
    }
    AstNodeType getType() const override { return AstNodeType::PackageDefinition; }
};

class Program : public AstNode {
public:
    std::vector<ImportDirectivePtr> imports;
    std::vector<AstNodePtr> statements;
    // The file it was read from, for messages; empty when not known.
    std::string fileName;

    AstNodeType getType() const override { return AstNodeType::Program; }
};

} // namespace as3
#endif
