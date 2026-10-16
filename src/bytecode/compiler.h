#pragma once

#include "../ast/ast_nodes.h"
#include "chunk.h"
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <stdexcept>

namespace as3 {

// A compile error: what was wrong, and where.
class CompileError : public std::runtime_error {
public:
    CompileError(const std::string& message, const std::string& file, int line)
        : std::runtime_error(message), fileName(file), lineNumber(line) {
    }
    std::string fileName;
    int lineNumber;
};

// Bytecode compiler: AST -> Bytecode
class Compiler {
public:
    Compiler();
    ~Compiler();

    // Compile a program to bytecode. Throws CompileError (a
    // std::runtime_error) for code that cannot be compiled, with every
    // error found, a line each.
    std::shared_ptr<BytecodeFunction> compile(Program* program);

    // What was accepted though not quite right, a line each.
    const std::vector<std::string>& getWarnings() const { return warnings; }

    struct FunctionState;
    struct ClassContext;
    struct ControlEntry;
    struct VarInfo;

private:
    FunctionState* fs = nullptr;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::string fileName;
    // The line of the statement being compiled, for what it emits.
    int currentLine = 0;

    // Functions
    std::shared_ptr<BytecodeFunction> compileFunction(const std::string& name,
                                                      const std::vector<std::unique_ptr<Parameter>>& params,
                                                      Identifier* returnType, Statement* body,
                                                      ClassContext* cls, bool isStatic, bool isMethod,
                                                      bool isConstructor, int line,
                                                      const std::vector<std::pair<std::string, Expression*>>* fieldInits = nullptr,
                                                      const std::string& selfName = std::string());
    void emitClosure(const std::shared_ptr<BytecodeFunction>& function);
    void compileClass(ClassDefinition* definition, const std::shared_ptr<CompileUnit>& unit);
    void compileUnitMembers(std::vector<AstNode*>& members, const std::shared_ptr<CompileUnit>& unit);

    // Statements
    void compileStatement(Statement* stmt);
    void compileBlockStatement(BlockStatement* stmt);
    void compileIfStatement(IfStatement* stmt);
    void compileWhileStatement(WhileStatement* stmt, const std::vector<std::string>& labels);
    void compileDoWhileStatement(DoWhileStatement* stmt, const std::vector<std::string>& labels);
    void compileForStatement(ForStatement* stmt, const std::vector<std::string>& labels);
    void compileForInStatement(ForInStatement* stmt, const std::vector<std::string>& labels);
    void compileSwitchStatement(SwitchStatement* stmt, const std::vector<std::string>& labels);
    void compileLabeledStatement(LabeledStatement* stmt);
    void compileTryStatement(TryStatement* stmt);
    void compileReturnStatement(ReturnStatement* stmt);
    void compileBreakContinue(const std::string& label, bool isContinue);
    void compileVariableDefinition(VariableDefinition* stmt);
    void compileLoopBody(Statement* body, const std::vector<std::string>& labels, bool isLoop);

    // Expressions. With discard, the value is not left on the stack.
    void compileExpression(Expression* expr);
    void compileDiscarded(Expression* expr);
    void compileIdentifier(Identifier* expr);
    void compileLiteral(Literal* expr);
    void compileBinaryExpression(BinaryExpression* expr);
    void compileUnaryExpression(UnaryExpression* expr, bool discard);
    void compileIncrement(Expression* target, bool increment, bool postfix, bool discard);
    void compileAssignment(AssignmentExpression* expr, bool discard);
    void compileCallExpression(CallExpression* expr);
    void compileNewExpression(NewExpression* expr);
    void compileMemberExpression(MemberExpression* expr);
    void compileArrayExpression(ArrayExpression* expr);
    void compileObjectExpression(ObjectExpression* expr);
    void compileFunctionExpression(FunctionExpression* expr);
    void compileTypeExpression(const std::string& typeName);
    int compileArguments(const ExpressionList& args);
    bool emitBinaryOperator(OperatorType op);

    // Names
    enum class NameKind { Local, Cell, Upvalue, Member, Static, OwnClass, Global };
    struct Resolved {
        NameKind kind = NameKind::Global;
        uint16_t index = 0;
        Coerce coerce = Coerce::None;
        bool isConst = false;
    };
    Resolved resolve(const std::string& name);
    int resolveUpvalue(FunctionState* state, const std::string& name);
    void emitLoad(const Resolved& where, const std::string& name);
    // The value on top stored; with keep it is left on the stack.
    void emitStore(const Resolved& where, const std::string& name, bool keep);
    // a.b.C as a qualified name, when its root names a package.
    bool qualifiedName(Expression* expr, std::string& name, std::vector<std::string>& rest);

    // Emission
    void emitOp(OpCode op);
    void emitU8(uint8_t value);
    void emitU16(uint16_t value);
    void emitS32(int32_t value);
    void adjust(int delta);
    void emitConstant(const Val& value);
    void emitNumber(double value);
    void emitString(const std::string& value);
    uint16_t nameIndex(const std::string& name);
    uint16_t newCache();
    size_t emitJump(OpCode op);
    void patchJump(size_t at);
    void patchJumpTo(size_t at, size_t target);
    void emitJumpTo(OpCode op, size_t target);
    size_t here() const;
    uint16_t allocTemp();
    void emitGetProp(const std::string& name);
    void emitSetProp(const std::string& name);
    void emitCallProp(const std::string& name, int argc);
    void emitCoerce(Coerce coerce);

    // Exceptions and jumps out of try blocks
    void pauseRegions(size_t from);
    void resumeRegions(size_t from);
    void emitFinallyFor(size_t entryIndex);

    void error(const std::string& message);
    void warning(const std::string& message);
};

} // namespace as3
