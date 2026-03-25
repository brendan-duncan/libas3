#include "executor.h"
#include <iostream>
#include <cmath>

namespace as3 {

Executor::Executor() {
    globalEnv = std::make_shared<Environment>();
    currentEnv = globalEnv;
    setupBuiltins();
}

void Executor::setupBuiltins() {
    // trace() function - AS3's built-in logging
    globalEnv->define("trace", Value::makeNativeFunction([](const std::vector<ValuePtr>& args) {
        for (size_t i = 0; i < args.size(); i++) {
            if (i > 0) std::cout << " ";
            std::cout << args[i]->toString();
        }
        std::cout << std::endl;
        return Value::makeUndefined();
    }));

    // print() function - alias for trace
    globalEnv->define("print", Value::makeNativeFunction([](const std::vector<ValuePtr>& args) {
        for (size_t i = 0; i < args.size(); i++) {
            if (i > 0) std::cout << " ";
            std::cout << args[i]->toString();
        }
        std::cout << std::endl;
        return Value::makeUndefined();
    }));

    // parseInt()
    globalEnv->define("parseInt", Value::makeNativeFunction([](const std::vector<ValuePtr>& args) {
        if (args.empty()) return Value::makeNumber(std::nan(""));
        std::string str = args[0]->toString();
        try {
            return Value::makeNumber(std::stoi(str));
        } catch (...) {
            return Value::makeNumber(std::nan(""));
        }
    }));

    // parseFloat()
    globalEnv->define("parseFloat", Value::makeNativeFunction([](const std::vector<ValuePtr>& args) {
        if (args.empty()) return Value::makeNumber(std::nan(""));
        std::string str = args[0]->toString();
        try {
            return Value::makeNumber(std::stod(str));
        } catch (...) {
            return Value::makeNumber(std::nan(""));
        }
    }));

    // isNaN()
    globalEnv->define("isNaN", Value::makeNativeFunction([](const std::vector<ValuePtr>& args) {
        if (args.empty()) return Value::makeBoolean(true);
        double num = args[0]->toNumber();
        return Value::makeBoolean(std::isnan(num));
    }));

    // Global constants
    globalEnv->define("undefined", Value::makeUndefined());
    globalEnv->define("null", Value::makeNull());
    globalEnv->define("NaN", Value::makeNumber(std::nan("")));
    globalEnv->define("Infinity", Value::makeNumber(std::numeric_limits<double>::infinity()));
}

void Executor::execute(Program* program) {
    // Execute top-level statements
    for (auto& node : program->statements) {
        // Cast AstNode to Statement
        if (auto* stmt = dynamic_cast<Statement*>(node.get())) {
            executeStatement(stmt);
        }
    }
}

void Executor::executeStatement(Statement* stmt) {
    if (!stmt) return;

    if (auto* block = dynamic_cast<BlockStatement*>(stmt)) {
        executeBlockStatement(block);
    } else if (auto* exprStmt = dynamic_cast<ExpressionStatement*>(stmt)) {
        executeExpressionStatement(exprStmt);
    } else if (auto* ifStmt = dynamic_cast<IfStatement*>(stmt)) {
        executeIfStatement(ifStmt);
    } else if (auto* whileStmt = dynamic_cast<WhileStatement*>(stmt)) {
        executeWhileStatement(whileStmt);
    } else if (auto* doWhileStmt = dynamic_cast<DoWhileStatement*>(stmt)) {
        executeDoWhileStatement(doWhileStmt);
    } else if (auto* forStmt = dynamic_cast<ForStatement*>(stmt)) {
        executeForStatement(forStmt);
    } else if (auto* forInStmt = dynamic_cast<ForInStatement*>(stmt)) {
        executeForInStatement(forInStmt);
    } else if (auto* switchStmt = dynamic_cast<SwitchStatement*>(stmt)) {
        executeSwitchStatement(switchStmt);
    } else if (auto* breakStmt = dynamic_cast<BreakStatement*>(stmt)) {
        executeBreakStatement(breakStmt);
    } else if (auto* continueStmt = dynamic_cast<ContinueStatement*>(stmt)) {
        executeContinueStatement(continueStmt);
    } else if (auto* returnStmt = dynamic_cast<ReturnStatement*>(stmt)) {
        executeReturnStatement(returnStmt);
    } else if (auto* throwStmt = dynamic_cast<ThrowStatement*>(stmt)) {
        executeThrowStatement(throwStmt);
    } else if (auto* tryStmt = dynamic_cast<TryStatement*>(stmt)) {
        executeTryStatement(tryStmt);
    } else if (auto* varDef = dynamic_cast<VariableDefinition*>(stmt)) {
        executeVariableDefinition(varDef);
    } else if (auto* funcDef = dynamic_cast<FunctionDefinition*>(stmt)) {
        executeFunctionDefinition(funcDef);
    } else if (auto* classDef = dynamic_cast<ClassDefinition*>(stmt)) {
        executeClassDefinition(classDef);
    }
    // EmptyStatement does nothing
}

void Executor::executeBlockStatement(BlockStatement* stmt) {
    // Create new scope
    auto previousEnv = currentEnv;
    currentEnv = currentEnv->createChild();

    for (auto& s : stmt->statements) {
        executeStatement(s.get());
    }

    // Restore previous scope
    currentEnv = previousEnv;
}

void Executor::executeExpressionStatement(ExpressionStatement* stmt) {
    evaluateExpression(stmt->expression.get());
}

void Executor::executeIfStatement(IfStatement* stmt) {
    ValuePtr condition = evaluateExpression(stmt->test.get());
    if (condition->toBoolean()) {
        executeStatement(stmt->consequent.get());
    } else if (stmt->alternate) {
        executeStatement(stmt->alternate.get());
    }
}

void Executor::executeWhileStatement(WhileStatement* stmt) {
    try {
        while (true) {
            ValuePtr condition = evaluateExpression(stmt->test.get());
            if (!condition->toBoolean()) break;

            try {
                executeStatement(stmt->body.get());
            } catch (const ContinueException&) {
                continue;
            }
        }
    } catch (const BreakException&) {
        // Break out of loop
    }
}

void Executor::executeDoWhileStatement(DoWhileStatement* stmt) {
    try {
        do {
            try {
                executeStatement(stmt->body.get());
            } catch (const ContinueException&) {
                // Continue to condition check
            }

            ValuePtr condition = evaluateExpression(stmt->test.get());
            if (!condition->toBoolean()) break;
        } while (true);
    } catch (const BreakException&) {
        // Break out of loop
    }
}

void Executor::executeForStatement(ForStatement* stmt) {
    // Create new scope for loop
    auto previousEnv = currentEnv;
    currentEnv = currentEnv->createChild();

    // Initialize
    if (stmt->init) {
        executeStatement(stmt->init.get());
    }

    try {
        while (true) {
            // Test condition
            if (stmt->test) {
                ValuePtr condition = evaluateExpression(stmt->test.get());
                if (!condition->toBoolean()) break;
            }

            // Execute body
            try {
                executeStatement(stmt->body.get());
            } catch (const ContinueException&) {
                // Continue to update
            }

            // Update
            if (stmt->update) {
                evaluateExpression(stmt->update.get());
            }
        }
    } catch (const BreakException&) {
        // Break out of loop
    }

    // Restore scope
    currentEnv = previousEnv;
}

void Executor::executeForInStatement(ForInStatement* stmt) {
    // Evaluate the right-hand side (object/array to iterate over)
    ValuePtr collection = evaluateExpression(stmt->right.get());

    // Create new scope for loop
    auto previousEnv = currentEnv;
    currentEnv = currentEnv->createChild();

    // Get the loop variable name
    std::string varName;
    if (auto* varDef = dynamic_cast<VariableDefinition*>(stmt->left.get())) {
        if (!varDef->declarations.empty()) {
            varName = varDef->declarations[0]->id->name;
            currentEnv->define(varName, Value::makeUndefined());
        }
    }

    try {
        if (collection->isArray()) {
            // Iterate over array indices
            for (size_t i = 0; i < collection->elements.size(); i++) {
                currentEnv->set(varName, Value::makeNumber(static_cast<double>(i)));

                try {
                    executeStatement(stmt->body.get());
                } catch (const ContinueException&) {
                    continue;
                }
            }
        } else if (collection->isObject()) {
            // Iterate over object properties
            for (auto& prop : collection->properties) {
                currentEnv->set(varName, Value::makeString(prop.first));

                try {
                    executeStatement(stmt->body.get());
                } catch (const ContinueException&) {
                    continue;
                }
            }
        }
    } catch (const BreakException&) {
        // Break out of loop
    }

    // Restore scope
    currentEnv = previousEnv;
}

void Executor::executeSwitchStatement(SwitchStatement* stmt) {
    ValuePtr discriminant = evaluateExpression(stmt->discriminant.get());

    bool matched = false;
    bool foundDefault = false;

    try {
        for (auto& caseClause : stmt->cases) {
            // Check if this is default or if we match
            if (!caseClause->test) {
                foundDefault = true;
                matched = true;
            } else if (!matched) {
                ValuePtr caseValue = evaluateExpression(caseClause->test.get());
                // Strict equality check
                if (discriminant->type == caseValue->type) {
                    if (discriminant->isNumber() && caseValue->isNumber() &&
                        discriminant->numberValue == caseValue->numberValue) {
                        matched = true;
                    } else if (discriminant->isString() && caseValue->isString() &&
                               discriminant->stringValue == caseValue->stringValue) {
                        matched = true;
                    } else if (discriminant->isBoolean() && caseValue->isBoolean() &&
                               discriminant->boolValue == caseValue->boolValue) {
                        matched = true;
                    }
                }
            }

            // Execute consequent if matched (with fallthrough)
            if (matched) {
                for (auto& s : caseClause->consequent) {
                    executeStatement(s.get());
                }
            }
        }
    } catch (const BreakException&) {
        // Break out of switch
    }
}

void Executor::executeBreakStatement(BreakStatement* stmt) {
    throw BreakException();
}

void Executor::executeContinueStatement(ContinueStatement* stmt) {
    throw ContinueException();
}

void Executor::executeReturnStatement(ReturnStatement* stmt) {
    ValuePtr value = Value::makeUndefined();
    if (stmt->argument) {
        value = evaluateExpression(stmt->argument.get());
    }
    throw ReturnException(value);
}

void Executor::executeThrowStatement(ThrowStatement* stmt) {
    ValuePtr value = evaluateExpression(stmt->argument.get());
    throw std::runtime_error("AS3 Exception: " + value->toString());
}

void Executor::executeTryStatement(TryStatement* stmt) {
    try {
        executeStatement(stmt->block.get());
    } catch (const ReturnException&) {
        throw; // Re-throw return
    } catch (const BreakException&) {
        throw; // Re-throw break
    } catch (const ContinueException&) {
        throw; // Re-throw continue
    } catch (const std::exception& e) {
        if (stmt->catchBody) {
            // Create new scope for catch block
            auto previousEnv = currentEnv;
            currentEnv = currentEnv->createChild();

            // Bind error to catch variable
            if (stmt->catchVar) {
                ValuePtr errorObj = Value::makeObject();
                errorObj->setProperty("message", Value::makeString(e.what()));
                currentEnv->define(stmt->catchVar->name, errorObj);
            }

            executeStatement(stmt->catchBody.get());

            currentEnv = previousEnv;
        }
    }

    // Always execute finally block
    if (stmt->finallyBody) {
        executeStatement(stmt->finallyBody.get());
    }
}

void Executor::executeVariableDefinition(VariableDefinition* stmt) {
    for (auto& decl : stmt->declarations) {
        ValuePtr value = Value::makeUndefined();
        if (decl->init) {
            value = evaluateExpression(decl->init.get());
        }
        currentEnv->define(decl->id->name, value);
    }
}

void Executor::executeFunctionDefinition(FunctionDefinition* stmt) {
    ValuePtr func = Value::makeFunction(stmt, currentEnv);
    currentEnv->define(stmt->name->name, func);
}

void Executor::executeClassDefinition(ClassDefinition* stmt) {
    // For now, create a constructor function
    ValuePtr classObj = Value::makeObject();
    currentEnv->define(stmt->name->name, classObj);
    // TODO: Implement full class support later
}

// Expression evaluation
ValuePtr Executor::evaluateExpression(Expression* expr) {
    if (!expr) return Value::makeUndefined();

    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        return evaluateIdentifier(id);
    } else if (auto* lit = dynamic_cast<Literal*>(expr)) {
        return evaluateLiteral(lit);
    } else if (auto* bin = dynamic_cast<BinaryExpression*>(expr)) {
        return evaluateBinaryExpression(bin);
    } else if (auto* un = dynamic_cast<UnaryExpression*>(expr)) {
        return evaluateUnaryExpression(un);
    } else if (auto* cond = dynamic_cast<ConditionalExpression*>(expr)) {
        return evaluateConditionalExpression(cond);
    } else if (auto* assign = dynamic_cast<AssignmentExpression*>(expr)) {
        return evaluateAssignmentExpression(assign);
    } else if (auto* call = dynamic_cast<CallExpression*>(expr)) {
        return evaluateCallExpression(call);
    } else if (auto* newExpr = dynamic_cast<NewExpression*>(expr)) {
        return evaluateNewExpression(newExpr);
    } else if (auto* member = dynamic_cast<MemberExpression*>(expr)) {
        return evaluateMemberExpression(member);
    } else if (auto* arr = dynamic_cast<ArrayExpression*>(expr)) {
        return evaluateArrayExpression(arr);
    } else if (auto* obj = dynamic_cast<ObjectExpression*>(expr)) {
        return evaluateObjectExpression(obj);
    } else if (auto* thisExpr = dynamic_cast<ThisExpression*>(expr)) {
        return evaluateThisExpression(thisExpr);
    } else if (auto* funcExpr = dynamic_cast<FunctionExpression*>(expr)) {
        return evaluateFunctionExpression(funcExpr);
    }

    return Value::makeUndefined();
}

ValuePtr Executor::evaluateIdentifier(Identifier* expr) {
    return currentEnv->get(expr->name);
}

ValuePtr Executor::evaluateLiteral(Literal* expr) {
    switch (expr->valueType) {
        case Literal::LiteralValueType::Bool:
            return Value::makeBoolean(expr->boolValue);
        case Literal::LiteralValueType::Number:
            return Value::makeNumber(expr->numberValue);
        case Literal::LiteralValueType::String:
            return Value::makeString(expr->stringValue);
        default:
            if (expr->kind == Literal::LiteralKind::Null) {
                return Value::makeNull();
            }
            return Value::makeUndefined();
    }
}

ValuePtr Executor::evaluateBinaryExpression(BinaryExpression* expr) {
    ValuePtr left = evaluateExpression(expr->left.get());
    ValuePtr right = evaluateExpression(expr->right.get());

    switch (expr->op) {
        case OperatorType::Add:
            // String concatenation or numeric addition
            if (left->isString() || right->isString()) {
                return Value::makeString(left->toString() + right->toString());
            }
            return Value::makeNumber(left->toNumber() + right->toNumber());

        case OperatorType::Subtract:
            return Value::makeNumber(left->toNumber() - right->toNumber());
        case OperatorType::Multiply:
            return Value::makeNumber(left->toNumber() * right->toNumber());
        case OperatorType::Divide:
            return Value::makeNumber(left->toNumber() / right->toNumber());
        case OperatorType::Remainder:
            return Value::makeNumber(std::fmod(left->toNumber(), right->toNumber()));

        case OperatorType::Lt:
            return Value::makeBoolean(left->toNumber() < right->toNumber());
        case OperatorType::Gt:
            return Value::makeBoolean(left->toNumber() > right->toNumber());
        case OperatorType::Le:
            return Value::makeBoolean(left->toNumber() <= right->toNumber());
        case OperatorType::Ge:
            return Value::makeBoolean(left->toNumber() >= right->toNumber());

        case OperatorType::Equals:
            // Loose equality
            if (left->type == right->type) {
                if (left->isNumber()) return Value::makeBoolean(left->numberValue == right->numberValue);
                if (left->isString()) return Value::makeBoolean(left->stringValue == right->stringValue);
                if (left->isBoolean()) return Value::makeBoolean(left->boolValue == right->boolValue);
            }
            return Value::makeBoolean(false);

        case OperatorType::NotEquals:
            // Loose inequality
            if (left->type == right->type) {
                if (left->isNumber()) return Value::makeBoolean(left->numberValue != right->numberValue);
                if (left->isString()) return Value::makeBoolean(left->stringValue != right->stringValue);
                if (left->isBoolean()) return Value::makeBoolean(left->boolValue != right->boolValue);
            }
            return Value::makeBoolean(true);

        case OperatorType::StrictEquals:
            if (left->type != right->type) return Value::makeBoolean(false);
            if (left->isNumber()) return Value::makeBoolean(left->numberValue == right->numberValue);
            if (left->isString()) return Value::makeBoolean(left->stringValue == right->stringValue);
            if (left->isBoolean()) return Value::makeBoolean(left->boolValue == right->boolValue);
            return Value::makeBoolean(left.get() == right.get());

        case OperatorType::StrictNotEquals:
            if (left->type != right->type) return Value::makeBoolean(true);
            if (left->isNumber()) return Value::makeBoolean(left->numberValue != right->numberValue);
            if (left->isString()) return Value::makeBoolean(left->stringValue != right->stringValue);
            if (left->isBoolean()) return Value::makeBoolean(left->boolValue != right->boolValue);
            return Value::makeBoolean(left.get() != right.get());

        case OperatorType::LogicalAnd:
            if (!left->toBoolean()) return left;
            return right;

        case OperatorType::LogicalOr:
            if (left->toBoolean()) return left;
            return right;

        case OperatorType::BitwiseAnd:
            return Value::makeNumber(static_cast<int32_t>(left->toNumber()) & static_cast<int32_t>(right->toNumber()));
        case OperatorType::BitwiseOr:
            return Value::makeNumber(static_cast<int32_t>(left->toNumber()) | static_cast<int32_t>(right->toNumber()));
        case OperatorType::BitwiseXor:
            return Value::makeNumber(static_cast<int32_t>(left->toNumber()) ^ static_cast<int32_t>(right->toNumber()));

        default:
            return Value::makeUndefined();
    }
}

ValuePtr Executor::evaluateUnaryExpression(UnaryExpression* expr) {
    ValuePtr operand = evaluateExpression(expr->operand.get());

    switch (expr->op) {
        case OperatorType::Positive:
            return Value::makeNumber(operand->toNumber());
        case OperatorType::Negative:
            return Value::makeNumber(-operand->toNumber());
        case OperatorType::LogicalNot:
            return Value::makeBoolean(!operand->toBoolean());
        case OperatorType::BitwiseNot:
            return Value::makeNumber(~static_cast<int32_t>(operand->toNumber()));

        case OperatorType::PreIncrement:
        case OperatorType::PostIncrement: {
            double newValue = operand->toNumber() + 1;
            if (auto* id = dynamic_cast<Identifier*>(expr->operand.get())) {
                currentEnv->set(id->name, Value::makeNumber(newValue));
            }
            return expr->isPostfix ? operand : Value::makeNumber(newValue);
        }

        case OperatorType::PreDecrement:
        case OperatorType::PostDecrement: {
            double newValue = operand->toNumber() - 1;
            if (auto* id = dynamic_cast<Identifier*>(expr->operand.get())) {
                currentEnv->set(id->name, Value::makeNumber(newValue));
            }
            return expr->isPostfix ? operand : Value::makeNumber(newValue);
        }

        default:
            return Value::makeUndefined();
    }
}

ValuePtr Executor::evaluateConditionalExpression(ConditionalExpression* expr) {
    ValuePtr test = evaluateExpression(expr->test.get());
    if (test->toBoolean()) {
        return evaluateExpression(expr->consequent.get());
    } else {
        return evaluateExpression(expr->alternate.get());
    }
}

ValuePtr Executor::evaluateAssignmentExpression(AssignmentExpression* expr) {
    ValuePtr value = evaluateExpression(expr->right.get());

    if (auto* id = dynamic_cast<Identifier*>(expr->left.get())) {
        currentEnv->set(id->name, value);
    } else if (auto* member = dynamic_cast<MemberExpression*>(expr->left.get())) {
        ValuePtr obj = evaluateExpression(member->object.get());
        if (member->computed && member->computedProperty) {
            ValuePtr prop = evaluateExpression(member->computedProperty.get());
            if (obj->isArray()) {
                obj->setElement(static_cast<size_t>(prop->toNumber()), value);
            } else {
                obj->setProperty(prop->toString(), value);
            }
        } else if (member->property) {
            obj->setProperty(member->property->name, value);
        }
    }

    return value;
}

ValuePtr Executor::evaluateCallExpression(CallExpression* expr) {
    ValuePtr func = evaluateExpression(expr->callee.get());

    std::vector<ValuePtr> args;
    for (auto& arg : expr->arguments) {
        args.push_back(evaluateExpression(arg.get()));
    }

    return callFunction(func, args);
}

ValuePtr Executor::evaluateNewExpression(NewExpression* expr) {
    // For now, just create an empty object
    ValuePtr obj = Value::makeObject();

    // Call constructor if available
    ValuePtr constructor = evaluateExpression(expr->callee.get());
    if (constructor->isFunction()) {
        std::vector<ValuePtr> args;
        for (auto& arg : expr->arguments) {
            args.push_back(evaluateExpression(arg.get()));
        }
        // TODO: Call constructor with 'this' bound to obj
    }

    return obj;
}

ValuePtr Executor::evaluateMemberExpression(MemberExpression* expr) {
    ValuePtr obj = evaluateExpression(expr->object.get());

    if (expr->computed && expr->computedProperty) {
        ValuePtr prop = evaluateExpression(expr->computedProperty.get());
        if (obj->isArray()) {
            return obj->getElement(static_cast<size_t>(prop->toNumber()));
        } else {
            return obj->getProperty(prop->toString());
        }
    } else if (expr->property) {
        return obj->getProperty(expr->property->name);
    }

    return Value::makeUndefined();
}

ValuePtr Executor::evaluateArrayExpression(ArrayExpression* expr) {
    ValuePtr arr = Value::makeArray();
    for (auto& elem : expr->elements) {
        arr->elements.push_back(evaluateExpression(elem.get()));
    }
    return arr;
}

ValuePtr Executor::evaluateObjectExpression(ObjectExpression* expr) {
    ValuePtr obj = Value::makeObject();
    for (auto& prop : expr->properties) {
        // For object literals, keys are often identifiers that should be treated as strings
        std::string key;
        if (auto* id = dynamic_cast<Identifier*>(prop.first.get())) {
            key = id->name;  // Use the identifier name directly
        } else {
            key = evaluateExpression(prop.first.get())->toString();
        }
        ValuePtr value = evaluateExpression(prop.second.get());
        obj->setProperty(key, value);
    }
    return obj;
}

ValuePtr Executor::evaluateThisExpression(ThisExpression* expr) {
    // TODO: Implement proper 'this' binding
    return Value::makeUndefined();
}

ValuePtr Executor::evaluateFunctionExpression(FunctionExpression* expr) {
    // Create a function value with current environment as closure
    // Note: FunctionExpression doesn't have a direct pointer, we'd need to adapt
    // For now, return undefined
    return Value::makeUndefined();
}

ValuePtr Executor::callFunction(ValuePtr func, const std::vector<ValuePtr>& args) {
    if (func->type == ValueType::NativeFunction) {
        return func->nativeFunc(args);
    } else if (func->type == ValueType::Function) {
        FunctionDefinition* funcDef = func->functionDef;

        // Create new environment for function scope
        auto previousEnv = currentEnv;
        currentEnv = func->closure->createChild();

        // Bind parameters
        for (size_t i = 0; i < funcDef->parameters.size(); i++) {
            ValuePtr arg = i < args.size() ? args[i] : Value::makeUndefined();
            currentEnv->define(funcDef->parameters[i]->name->name, arg);
        }

        // Execute function body
        ValuePtr result = Value::makeUndefined();
        try {
            executeStatement(funcDef->body.get());
        } catch (const ReturnException& ret) {
            result = ret.value;
        }

        // Restore environment
        currentEnv = previousEnv;

        return result;
    }

    return Value::makeUndefined();
}

} // namespace as3
