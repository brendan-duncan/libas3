#pragma once

#include "../ast/ast_nodes.h"
#include "../runtime/value.h"
#include "../runtime/environment.h"
#include <memory>
#include <stdexcept>

namespace as3 {

// Exception for flow control
class ReturnException : public std::exception {
public:
    ValuePtr value;
    explicit ReturnException(ValuePtr v) : value(v) {}
};

class BreakException : public std::exception {};
class ContinueException : public std::exception {};

class Executor {
public:
    explicit Executor();

    // Execute a program
    void execute(Program* program);

    // Get global environment (for testing/debugging)
    std::shared_ptr<Environment> getGlobalEnvironment() { return globalEnv; }

private:
    std::shared_ptr<Environment> globalEnv;
    std::shared_ptr<Environment> currentEnv;

    // Statement execution
    void executeStatement(Statement* stmt);
    void executeBlockStatement(BlockStatement* stmt);
    void executeExpressionStatement(ExpressionStatement* stmt);
    void executeIfStatement(IfStatement* stmt);
    void executeWhileStatement(WhileStatement* stmt);
    void executeDoWhileStatement(DoWhileStatement* stmt);
    void executeForStatement(ForStatement* stmt);
    void executeForInStatement(ForInStatement* stmt);
    void executeSwitchStatement(SwitchStatement* stmt);
    void executeBreakStatement(BreakStatement* stmt);
    void executeContinueStatement(ContinueStatement* stmt);
    void executeReturnStatement(ReturnStatement* stmt);
    void executeThrowStatement(ThrowStatement* stmt);
    void executeTryStatement(TryStatement* stmt);
    void executeVariableDefinition(VariableDefinition* stmt);
    void executeFunctionDefinition(FunctionDefinition* stmt);
    void executeClassDefinition(ClassDefinition* stmt);

    // Expression evaluation
    ValuePtr evaluateExpression(Expression* expr);
    ValuePtr evaluateIdentifier(Identifier* expr);
    ValuePtr evaluateLiteral(Literal* expr);
    ValuePtr evaluateBinaryExpression(BinaryExpression* expr);
    ValuePtr evaluateUnaryExpression(UnaryExpression* expr);
    ValuePtr evaluateConditionalExpression(ConditionalExpression* expr);
    ValuePtr evaluateAssignmentExpression(AssignmentExpression* expr);
    ValuePtr evaluateCallExpression(CallExpression* expr);
    ValuePtr evaluateNewExpression(NewExpression* expr);
    ValuePtr evaluateMemberExpression(MemberExpression* expr);
    ValuePtr evaluateArrayExpression(ArrayExpression* expr);
    ValuePtr evaluateObjectExpression(ObjectExpression* expr);
    ValuePtr evaluateThisExpression(ThisExpression* expr);
    ValuePtr evaluateFunctionExpression(FunctionExpression* expr);

    // Helper methods
    ValuePtr callFunction(ValuePtr func, const std::vector<ValuePtr>& args);
    void setupBuiltins();
};

} // namespace as3
