#pragma once

#include "../ast/ast_nodes.h"
#include "chunk.h"
#include <memory>
#include <vector>
#include <string>
#include <map>

namespace as3 {

// Local variable info
struct Local {
    std::string name;
    int depth;          // Scope depth (0 = global)
    bool isCaptured;    // Is this captured by a closure?

    Local(const std::string& n, int d)
        : name(n)
        , depth(d)
        , isCaptured(false) {
    }
};

// Upvalue info (captured variables)
struct Upvalue {
    uint8_t index;
    bool isLocal;  // Is it a local in the enclosing function?

    Upvalue(uint8_t i, bool local)
        : index(i)
        , isLocal(local) {
    }
};

// Compiler context for a function
class CompilerContext {
public:
    CompilerContext* enclosing;
    BytecodeFunction* function;
    std::vector<Local> locals;
    std::vector<Upvalue> upvalues;
    int scopeDepth;
    int loopStart;          // For break/continue
    int loopScopeDepth;     // Scope depth of current loop

    explicit CompilerContext(CompilerContext* parent = nullptr);

    // Local variables
    int addLocal(const std::string& name);
    int resolveLocal(const std::string& name);
    int resolveUpvalue(const std::string& name);

    // Scope management
    void beginScope();
    void endScope(Chunk& chunk);

    // Helper to mark local as initialized
    void markInitialized();
};

// Bytecode compiler: AST -> Bytecode
class Compiler {
public:
    Compiler();

    // Compile a program to bytecode
    std::shared_ptr<BytecodeFunction> compile(Program* program);

private:
    CompilerContext* current;
    bool hadError;
    std::string errorMessage;

    // Statement compilation
    void compileStatement(Statement* stmt, Chunk& chunk);
    void compileBlockStatement(BlockStatement* stmt, Chunk& chunk);
    void compileExpressionStatement(ExpressionStatement* stmt, Chunk& chunk);
    void compileIfStatement(IfStatement* stmt, Chunk& chunk);
    void compileWhileStatement(WhileStatement* stmt, Chunk& chunk);
    void compileDoWhileStatement(DoWhileStatement* stmt, Chunk& chunk);
    void compileForStatement(ForStatement* stmt, Chunk& chunk);
    void compileForInStatement(ForInStatement* stmt, Chunk& chunk);
    void compileSwitchStatement(SwitchStatement* stmt, Chunk& chunk);
    void compileReturnStatement(ReturnStatement* stmt, Chunk& chunk);
    void compileVariableDefinition(VariableDefinition* stmt, Chunk& chunk);
    void compileFunctionDefinition(FunctionDefinition* stmt, Chunk& chunk);
    void compileClassDefinition(ClassDefinition* stmt, Chunk& chunk);

    // Expression compilation
    void compileExpression(Expression* expr, Chunk& chunk);
    void compileIdentifier(Identifier* expr, Chunk& chunk);
    void compileLiteral(Literal* expr, Chunk& chunk);
    void compileBinaryExpression(BinaryExpression* expr, Chunk& chunk);
    // The opcode of a binary operator; false when there is none.
    bool emitBinaryOperator(OperatorType op, Chunk& chunk);
    // ++ and --, before or after: the target read, stepped by one and
    // written back, leaving the new value or, after, the old one.
    void compileIncrement(Expression* target, bool increment, bool postfix, Chunk& chunk);
    void compileUnaryExpression(UnaryExpression* expr, Chunk& chunk);
    void compileConditionalExpression(ConditionalExpression* expr, Chunk& chunk);
    void compileAssignmentExpression(AssignmentExpression* expr, Chunk& chunk);
    void compileCallExpression(CallExpression* expr, Chunk& chunk);
    void compileNewExpression(NewExpression* expr, Chunk& chunk);
    void compileMemberExpression(MemberExpression* expr, Chunk& chunk);
    void compileArrayExpression(ArrayExpression* expr, Chunk& chunk);
    void compileObjectExpression(ObjectExpression* expr, Chunk& chunk);
    void compileFunctionExpression(FunctionExpression* expr, Chunk& chunk);

    // Helpers
    uint16_t makeConstant(ValuePtr value, Chunk& chunk);
    uint16_t identifierConstant(const std::string& name, Chunk& chunk);
    void emitByte(uint8_t byte, Chunk& chunk);
    void emitBytes(uint8_t byte1, uint8_t byte2, Chunk& chunk);
    void emitOpCode(OpCode op, Chunk& chunk);
    void emitConstant(ValuePtr value, Chunk& chunk);
    void emitReturn(Chunk& chunk);
    size_t emitJump(OpCode jumpOp, Chunk& chunk);
    void patchJump(size_t offset, Chunk& chunk);
    void emitLoop(size_t loopStart, Chunk& chunk);

    // Variable handling
    void namedVariable(const std::string& name, bool isAssignment, Chunk& chunk);
    void defineVariable(uint16_t global, Chunk& chunk);

    // Error handling
    void error(const std::string& message);
};

} // namespace as3
