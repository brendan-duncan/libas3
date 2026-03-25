#include "compiler.h"
#include <iostream>
#include <stdexcept>

namespace as3 {

// CompilerContext implementation

CompilerContext::CompilerContext(CompilerContext* parent)
    : enclosing(parent)
    , function(nullptr)
    , scopeDepth(0)
    , loopStart(-1)
    , loopScopeDepth(-1) {
}

int CompilerContext::addLocal(const std::string& name) {
    if (locals.size() >= UINT8_MAX) {
        throw std::runtime_error("Too many local variables in function");
    }

    locals.emplace_back(name, -1);  // -1 means uninitialized
    return static_cast<int>(locals.size() - 1);
}

int CompilerContext::resolveLocal(const std::string& name) {
    // Walk backwards to find most recent declaration
    for (int i = static_cast<int>(locals.size()) - 1; i >= 0; i--) {
        if (locals[i].name == name) {
            if (locals[i].depth == -1) {
                throw std::runtime_error("Can't read local variable in its own initializer");
            }
            return i;
        }
    }
    return -1;  // Not found
}

int CompilerContext::resolveUpvalue(const std::string& name) {
    if (enclosing == nullptr) return -1;

    // Try to resolve in enclosing function's locals
    int local = enclosing->resolveLocal(name);
    if (local != -1) {
        enclosing->locals[local].isCaptured = true;
        upvalues.emplace_back(static_cast<uint8_t>(local), true);
        return static_cast<int>(upvalues.size() - 1);
    }

    // Try to resolve in enclosing function's upvalues
    int upvalue = enclosing->resolveUpvalue(name);
    if (upvalue != -1) {
        upvalues.emplace_back(static_cast<uint8_t>(upvalue), false);
        return static_cast<int>(upvalues.size() - 1);
    }

    return -1;
}

void CompilerContext::beginScope() {
    scopeDepth++;
}

void CompilerContext::endScope(Chunk& chunk) {
    scopeDepth--;

    // Pop all locals from this scope
    while (!locals.empty() && locals.back().depth > scopeDepth) {
        if (locals.back().isCaptured) {
            chunk.writeOpCode(OpCode::OP_CLOSE_UPVALUE);
        } else {
            chunk.writeOpCode(OpCode::OP_POP);
        }
        locals.pop_back();
    }
}

void CompilerContext::markInitialized() {
    if (scopeDepth == 0) return;  // Global variables
    if (!locals.empty()) {
        locals.back().depth = scopeDepth;
    }
}

// Compiler implementation

Compiler::Compiler()
    : current(nullptr)
    , hadError(false) {
}

std::shared_ptr<BytecodeFunction> Compiler::compile(Program* program) {
    // Create top-level script function
    auto scriptFunction = std::make_shared<BytecodeFunction>("<script>", 0);
    CompilerContext context(nullptr);
    context.function = scriptFunction.get();
    current = &context;

    Chunk& chunk = scriptFunction->chunk;

    // Compile all statements
    for (auto& stmt : program->statements) {
        if (auto* s = dynamic_cast<Statement*>(stmt.get())) {
            compileStatement(s, chunk);
        }
    }

    // Implicit return undefined at end
    emitOpCode(OpCode::OP_UNDEFINED, chunk);
    emitReturn(chunk);

    if (hadError) {
        throw std::runtime_error("Compilation failed: " + errorMessage);
    }

    return scriptFunction;
}

void Compiler::compileStatement(Statement* stmt, Chunk& chunk) {
    if (!stmt) return;

    if (auto* block = dynamic_cast<BlockStatement*>(stmt)) {
        compileBlockStatement(block, chunk);
    } else if (auto* exprStmt = dynamic_cast<ExpressionStatement*>(stmt)) {
        compileExpressionStatement(exprStmt, chunk);
    } else if (auto* ifStmt = dynamic_cast<IfStatement*>(stmt)) {
        compileIfStatement(ifStmt, chunk);
    } else if (auto* whileStmt = dynamic_cast<WhileStatement*>(stmt)) {
        compileWhileStatement(whileStmt, chunk);
    } else if (auto* doWhileStmt = dynamic_cast<DoWhileStatement*>(stmt)) {
        compileDoWhileStatement(doWhileStmt, chunk);
    } else if (auto* forStmt = dynamic_cast<ForStatement*>(stmt)) {
        compileForStatement(forStmt, chunk);
    } else if (auto* forInStmt = dynamic_cast<ForInStatement*>(stmt)) {
        compileForInStatement(forInStmt, chunk);
    } else if (auto* switchStmt = dynamic_cast<SwitchStatement*>(stmt)) {
        compileSwitchStatement(switchStmt, chunk);
    } else if (auto* returnStmt = dynamic_cast<ReturnStatement*>(stmt)) {
        compileReturnStatement(returnStmt, chunk);
    } else if (auto* varDef = dynamic_cast<VariableDefinition*>(stmt)) {
        compileVariableDefinition(varDef, chunk);
    } else if (auto* funcDef = dynamic_cast<FunctionDefinition*>(stmt)) {
        compileFunctionDefinition(funcDef, chunk);
    } else if (auto* breakStmt = dynamic_cast<BreakStatement*>(stmt)) {
        // TODO: Implement break
        emitJump(OpCode::OP_JUMP, chunk);
    } else if (auto* continueStmt = dynamic_cast<ContinueStatement*>(stmt)) {
        // TODO: Implement continue
        emitLoop(current->loopStart, chunk);
    }
    // EmptyStatement does nothing
}

void Compiler::compileBlockStatement(BlockStatement* stmt, Chunk& chunk) {
    current->beginScope();
    for (auto& s : stmt->statements) {
        compileStatement(s.get(), chunk);
    }
    current->endScope(chunk);
}

void Compiler::compileExpressionStatement(ExpressionStatement* stmt, Chunk& chunk) {
    compileExpression(stmt->expression.get(), chunk);
    emitOpCode(OpCode::OP_POP, chunk);  // Discard result
}

void Compiler::compileIfStatement(IfStatement* stmt, Chunk& chunk) {
    // Compile condition
    compileExpression(stmt->test.get(), chunk);

    // Jump if false
    size_t thenJump = emitJump(OpCode::OP_JUMP_IF_FALSE, chunk);
    emitOpCode(OpCode::OP_POP, chunk);  // Pop condition

    // Compile then branch
    compileStatement(stmt->consequent.get(), chunk);

    if (stmt->alternate) {
        size_t elseJump = emitJump(OpCode::OP_JUMP, chunk);
        patchJump(thenJump, chunk);
        emitOpCode(OpCode::OP_POP, chunk);  // Pop condition

        // Compile else branch
        compileStatement(stmt->alternate.get(), chunk);
        patchJump(elseJump, chunk);
    } else {
        patchJump(thenJump, chunk);
        emitOpCode(OpCode::OP_POP, chunk);  // Pop condition
    }
}

void Compiler::compileWhileStatement(WhileStatement* stmt, Chunk& chunk) {
    size_t loopStart = chunk.getCurrentOffset();
    int previousLoopStart = current->loopStart;
    int previousLoopDepth = current->loopScopeDepth;

    current->loopStart = static_cast<int>(loopStart);
    current->loopScopeDepth = current->scopeDepth;

    // Compile condition
    compileExpression(stmt->test.get(), chunk);

    // Jump if false (exit loop)
    size_t exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE, chunk);
    emitOpCode(OpCode::OP_POP, chunk);

    // Compile body
    compileStatement(stmt->body.get(), chunk);

    // Loop back
    emitLoop(loopStart, chunk);

    // Exit
    patchJump(exitJump, chunk);
    emitOpCode(OpCode::OP_POP, chunk);

    current->loopStart = previousLoopStart;
    current->loopScopeDepth = previousLoopDepth;
}

void Compiler::compileDoWhileStatement(DoWhileStatement* stmt, Chunk& chunk) {
    size_t loopStart = chunk.getCurrentOffset();
    int previousLoopStart = current->loopStart;
    int previousLoopDepth = current->loopScopeDepth;

    current->loopStart = static_cast<int>(loopStart);
    current->loopScopeDepth = current->scopeDepth;

    // Compile body
    compileStatement(stmt->body.get(), chunk);

    // Compile condition
    compileExpression(stmt->test.get(), chunk);

    // Loop if true
    size_t exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE, chunk);
    emitOpCode(OpCode::OP_POP, chunk);
    emitLoop(loopStart, chunk);

    // Exit
    patchJump(exitJump, chunk);
    emitOpCode(OpCode::OP_POP, chunk);

    current->loopStart = previousLoopStart;
    current->loopScopeDepth = previousLoopDepth;
}

void Compiler::compileForStatement(ForStatement* stmt, Chunk& chunk) {
    current->beginScope();

    // Initialize
    if (stmt->init) {
        compileStatement(stmt->init.get(), chunk);
    }

    size_t loopStart = chunk.getCurrentOffset();
    int previousLoopStart = current->loopStart;
    int previousLoopDepth = current->loopScopeDepth;

    current->loopStart = static_cast<int>(loopStart);
    current->loopScopeDepth = current->scopeDepth;

    // Condition
    size_t exitJump = SIZE_MAX;
    if (stmt->test) {
        compileExpression(stmt->test.get(), chunk);
        exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE, chunk);
        emitOpCode(OpCode::OP_POP, chunk);
    }

    // Body
    compileStatement(stmt->body.get(), chunk);

    // Update
    if (stmt->update) {
        compileExpression(stmt->update.get(), chunk);
        emitOpCode(OpCode::OP_POP, chunk);
    }

    // Loop back
    emitLoop(loopStart, chunk);

    // Exit
    if (exitJump != SIZE_MAX) {
        patchJump(exitJump, chunk);
        emitOpCode(OpCode::OP_POP, chunk);
    }

    current->loopStart = previousLoopStart;
    current->loopScopeDepth = previousLoopDepth;

    current->endScope(chunk);
}

void Compiler::compileForInStatement(ForInStatement* stmt, Chunk& chunk) {
    // TODO: Implement for-in properly
    // For now, just compile as empty statement
    error("for-in not yet implemented in bytecode compiler");
}

void Compiler::compileSwitchStatement(SwitchStatement* stmt, Chunk& chunk) {
    // TODO: Implement switch
    error("switch not yet implemented in bytecode compiler");
}

void Compiler::compileReturnStatement(ReturnStatement* stmt, Chunk& chunk) {
    if (stmt->argument) {
        compileExpression(stmt->argument.get(), chunk);
    } else {
        emitOpCode(OpCode::OP_UNDEFINED, chunk);
    }
    emitReturn(chunk);
}

void Compiler::compileVariableDefinition(VariableDefinition* stmt, Chunk& chunk) {
    for (auto& decl : stmt->declarations) {
        // Compile initializer
        if (decl->init) {
            compileExpression(decl->init.get(), chunk);
        } else {
            emitOpCode(OpCode::OP_UNDEFINED, chunk);
        }

        // Define variable
        if (current->scopeDepth == 0) {
            // Global variable
            uint16_t global = identifierConstant(decl->id->name, chunk);
            defineVariable(global, chunk);
        } else {
            // Local variable
            current->addLocal(decl->id->name);
            current->markInitialized();
        }
    }
}

void Compiler::compileFunctionDefinition(FunctionDefinition* stmt, Chunk& chunk) {
    // Create new compiler context for function
    CompilerContext functionContext(current);
    auto func = std::make_shared<BytecodeFunction>(stmt->name->name, static_cast<int>(stmt->parameters.size()));
    functionContext.function = func.get();

    CompilerContext* previousCurrent = current;
    current = &functionContext;

    current->beginScope();

    // Compile parameters
    for (auto& param : stmt->parameters) {
        current->addLocal(param->name->name);
        current->markInitialized();
    }

    // Compile body
    compileStatement(stmt->body.get(), func->chunk);

    // Implicit return undefined
    emitOpCode(OpCode::OP_UNDEFINED, func->chunk);
    emitReturn(func->chunk);

    current = previousCurrent;

    // Store the compiled function as a constant
    uint16_t constant = makeConstant(Value::makeFunction(func), chunk);
    emitOpCode(OpCode::OP_CLOSURE, chunk);
    chunk.writeU16(constant);

    // Define function variable
    if (current->scopeDepth == 0) {
        uint16_t global = identifierConstant(stmt->name->name, chunk);
        defineVariable(global, chunk);
    } else {
        current->addLocal(stmt->name->name);
        current->markInitialized();
    }
}

// Expression compilation

void Compiler::compileExpression(Expression* expr, Chunk& chunk) {
    if (!expr) {
        emitOpCode(OpCode::OP_UNDEFINED, chunk);
        return;
    }

    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        compileIdentifier(id, chunk);
    } else if (auto* lit = dynamic_cast<Literal*>(expr)) {
        compileLiteral(lit, chunk);
    } else if (auto* bin = dynamic_cast<BinaryExpression*>(expr)) {
        compileBinaryExpression(bin, chunk);
    } else if (auto* un = dynamic_cast<UnaryExpression*>(expr)) {
        compileUnaryExpression(un, chunk);
    } else if (auto* cond = dynamic_cast<ConditionalExpression*>(expr)) {
        compileConditionalExpression(cond, chunk);
    } else if (auto* assign = dynamic_cast<AssignmentExpression*>(expr)) {
        compileAssignmentExpression(assign, chunk);
    } else if (auto* call = dynamic_cast<CallExpression*>(expr)) {
        compileCallExpression(call, chunk);
    } else if (auto* member = dynamic_cast<MemberExpression*>(expr)) {
        compileMemberExpression(member, chunk);
    } else if (auto* arr = dynamic_cast<ArrayExpression*>(expr)) {
        compileArrayExpression(arr, chunk);
    } else if (auto* obj = dynamic_cast<ObjectExpression*>(expr)) {
        compileObjectExpression(obj, chunk);
    } else if (auto* funcExpr = dynamic_cast<FunctionExpression*>(expr)) {
        compileFunctionExpression(funcExpr, chunk);
    } else if (auto* thisExpr = dynamic_cast<ThisExpression*>(expr)) {
        // TODO: Implement this
        emitOpCode(OpCode::OP_UNDEFINED, chunk);
    }
}

void Compiler::compileIdentifier(Identifier* expr, Chunk& chunk) {
    namedVariable(expr->name, false, chunk);
}

void Compiler::compileLiteral(Literal* expr, Chunk& chunk) {
    switch (expr->valueType) {
        case Literal::LiteralValueType::Bool:
            emitOpCode(expr->boolValue ? OpCode::OP_TRUE : OpCode::OP_FALSE, chunk);
            break;
        case Literal::LiteralValueType::Number:
            // Optimize common numbers
            if (expr->numberValue == 0.0) {
                emitOpCode(OpCode::OP_NUMBER_0, chunk);
            } else if (expr->numberValue == 1.0) {
                emitOpCode(OpCode::OP_NUMBER_1, chunk);
            } else if (expr->numberValue == -1.0) {
                emitOpCode(OpCode::OP_NUMBER_NEG1, chunk);
            } else {
                emitConstant(Value::makeNumber(expr->numberValue), chunk);
            }
            break;
        case Literal::LiteralValueType::String:
            emitConstant(Value::makeString(expr->stringValue), chunk);
            break;
        default:
            if (expr->kind == Literal::LiteralKind::Null) {
                emitOpCode(OpCode::OP_NULL, chunk);
            } else {
                emitOpCode(OpCode::OP_UNDEFINED, chunk);
            }
            break;
    }
}

void Compiler::compileBinaryExpression(BinaryExpression* expr, Chunk& chunk) {
    // Compile operands
    compileExpression(expr->left.get(), chunk);
    compileExpression(expr->right.get(), chunk);

    // Emit operator
    switch (expr->op) {
        case OperatorType::Add: emitOpCode(OpCode::OP_ADD, chunk); break;
        case OperatorType::Subtract: emitOpCode(OpCode::OP_SUBTRACT, chunk); break;
        case OperatorType::Multiply: emitOpCode(OpCode::OP_MULTIPLY, chunk); break;
        case OperatorType::Divide: emitOpCode(OpCode::OP_DIVIDE, chunk); break;
        case OperatorType::Remainder: emitOpCode(OpCode::OP_MODULO, chunk); break;
        case OperatorType::Power: emitOpCode(OpCode::OP_POWER, chunk); break;

        case OperatorType::Equals: emitOpCode(OpCode::OP_EQUAL, chunk); break;
        case OperatorType::NotEquals: emitOpCode(OpCode::OP_NOT_EQUAL, chunk); break;
        case OperatorType::StrictEquals: emitOpCode(OpCode::OP_STRICT_EQUAL, chunk); break;
        case OperatorType::StrictNotEquals: emitOpCode(OpCode::OP_STRICT_NOT_EQUAL, chunk); break;
        case OperatorType::Lt: emitOpCode(OpCode::OP_LESS, chunk); break;
        case OperatorType::Le: emitOpCode(OpCode::OP_LESS_EQUAL, chunk); break;
        case OperatorType::Gt: emitOpCode(OpCode::OP_GREATER, chunk); break;
        case OperatorType::Ge: emitOpCode(OpCode::OP_GREATER_EQUAL, chunk); break;

        case OperatorType::BitwiseAnd: emitOpCode(OpCode::OP_BIT_AND, chunk); break;
        case OperatorType::BitwiseOr: emitOpCode(OpCode::OP_BIT_OR, chunk); break;
        case OperatorType::BitwiseXor: emitOpCode(OpCode::OP_BIT_XOR, chunk); break;
        case OperatorType::ShiftLeft: emitOpCode(OpCode::OP_LEFT_SHIFT, chunk); break;
        case OperatorType::ShiftRight: emitOpCode(OpCode::OP_RIGHT_SHIFT, chunk); break;
        case OperatorType::ShiftRightUnsigned: emitOpCode(OpCode::OP_UNSIGNED_RIGHT_SHIFT, chunk); break;

        case OperatorType::LogicalAnd: emitOpCode(OpCode::OP_AND, chunk); break;
        case OperatorType::LogicalOr: emitOpCode(OpCode::OP_OR, chunk); break;

        case OperatorType::Instanceof: emitOpCode(OpCode::OP_INSTANCEOF, chunk); break;
        case OperatorType::In: emitOpCode(OpCode::OP_IN, chunk); break;

        default:
            error("Unsupported binary operator");
            break;
    }
}

void Compiler::compileUnaryExpression(UnaryExpression* expr, Chunk& chunk) {
    compileExpression(expr->operand.get(), chunk);

    switch (expr->op) {
        case OperatorType::Negative: emitOpCode(OpCode::OP_NEGATE, chunk); break;
        case OperatorType::LogicalNot: emitOpCode(OpCode::OP_NOT, chunk); break;
        case OperatorType::BitwiseNot: emitOpCode(OpCode::OP_BIT_NOT, chunk); break;
        case OperatorType::PreIncrement: emitOpCode(OpCode::OP_INCREMENT, chunk); break;
        case OperatorType::PreDecrement: emitOpCode(OpCode::OP_DECREMENT, chunk); break;
        case OperatorType::PostIncrement: emitOpCode(OpCode::OP_POST_INCREMENT, chunk); break;
        case OperatorType::PostDecrement: emitOpCode(OpCode::OP_POST_DECREMENT, chunk); break;
        case OperatorType::Typeof: emitOpCode(OpCode::OP_TYPEOF, chunk); break;
        case OperatorType::Delete: emitOpCode(OpCode::OP_DELETE, chunk); break;
        default: break;
    }
}

void Compiler::compileConditionalExpression(ConditionalExpression* expr, Chunk& chunk) {
    // Compile test
    compileExpression(expr->test.get(), chunk);

    // Jump if false
    size_t thenJump = emitJump(OpCode::OP_JUMP_IF_FALSE, chunk);
    emitOpCode(OpCode::OP_POP, chunk);

    // Compile consequent
    compileExpression(expr->consequent.get(), chunk);

    size_t elseJump = emitJump(OpCode::OP_JUMP, chunk);
    patchJump(thenJump, chunk);
    emitOpCode(OpCode::OP_POP, chunk);

    // Compile alternate
    compileExpression(expr->alternate.get(), chunk);
    patchJump(elseJump, chunk);
}

void Compiler::compileAssignmentExpression(AssignmentExpression* expr, Chunk& chunk) {
    // Compile right side
    compileExpression(expr->right.get(), chunk);

    // Assign to left side
    if (auto* id = dynamic_cast<Identifier*>(expr->left.get())) {
        namedVariable(id->name, true, chunk);
    } else if (auto* member = dynamic_cast<MemberExpression*>(expr->left.get())) {
        // TODO: Property assignment
        error("Property assignment not yet implemented");
    }
}

void Compiler::compileCallExpression(CallExpression* expr, Chunk& chunk) {
    // Compile callee
    compileExpression(expr->callee.get(), chunk);

    // Compile arguments
    for (auto& arg : expr->arguments) {
        compileExpression(arg.get(), chunk);
    }

    // Emit call
    emitOpCode(OpCode::OP_CALL, chunk);
    emitByte(static_cast<uint8_t>(expr->arguments.size()), chunk);
}

void Compiler::compileMemberExpression(MemberExpression* expr, Chunk& chunk) {
    // Compile object
    compileExpression(expr->object.get(), chunk);

    if (expr->computed && expr->computedProperty) {
        // Computed access: obj[prop]
        compileExpression(expr->computedProperty.get(), chunk);
        emitOpCode(OpCode::OP_GET_INDEX, chunk);
    } else if (expr->property) {
        // Dot access: obj.prop
        uint16_t name = identifierConstant(expr->property->name, chunk);
        emitOpCode(OpCode::OP_GET_PROPERTY, chunk);
        chunk.writeU16(name);
    }
}

void Compiler::compileArrayExpression(ArrayExpression* expr, Chunk& chunk) {
    // Compile all elements
    for (auto& elem : expr->elements) {
        compileExpression(elem.get(), chunk);
    }

    // Create array
    emitOpCode(OpCode::OP_NEW_ARRAY, chunk);
    chunk.writeU16(static_cast<uint16_t>(expr->elements.size()));
}

void Compiler::compileObjectExpression(ObjectExpression* expr, Chunk& chunk) {
    // Compile all properties (key-value pairs)
    for (auto& prop : expr->properties) {
        // Key
        if (auto* id = dynamic_cast<Identifier*>(prop.first.get())) {
            emitConstant(Value::makeString(id->name), chunk);
        } else {
            compileExpression(prop.first.get(), chunk);
        }
        // Value
        compileExpression(prop.second.get(), chunk);
    }

    // Create object
    emitOpCode(OpCode::OP_NEW_OBJECT, chunk);
    chunk.writeU16(static_cast<uint16_t>(expr->properties.size()));
}

void Compiler::compileFunctionExpression(FunctionExpression* expr, Chunk& chunk) {
    // TODO: Similar to compileFunctionDefinition but for expressions
    error("Function expressions not yet implemented in bytecode compiler");
}

// Helper methods

uint16_t Compiler::makeConstant(ValuePtr value, Chunk& chunk) {
    return chunk.addConstant(value);
}

uint16_t Compiler::identifierConstant(const std::string& name, Chunk& chunk) {
    return makeConstant(Value::makeString(name), chunk);
}

void Compiler::emitByte(uint8_t byte, Chunk& chunk) {
    chunk.writeByte(byte);
}

void Compiler::emitBytes(uint8_t byte1, uint8_t byte2, Chunk& chunk) {
    emitByte(byte1, chunk);
    emitByte(byte2, chunk);
}

void Compiler::emitOpCode(OpCode op, Chunk& chunk) {
    chunk.writeOpCode(op);
}

void Compiler::emitConstant(ValuePtr value, Chunk& chunk) {
    uint16_t constant = makeConstant(value, chunk);
    emitOpCode(OpCode::OP_CONSTANT, chunk);
    chunk.writeU16(constant);
}

void Compiler::emitReturn(Chunk& chunk) {
    emitOpCode(OpCode::OP_RETURN, chunk);
}

size_t Compiler::emitJump(OpCode jumpOp, Chunk& chunk) {
    emitOpCode(jumpOp, chunk);
    chunk.writeU16(0xFFFF);  // Placeholder
    return chunk.getCurrentOffset() - 2;
}

void Compiler::patchJump(size_t offset, Chunk& chunk) {
    size_t jump = chunk.getCurrentOffset() - offset - 2;
    if (jump > INT16_MAX) {
        error("Too much code to jump over");
    }
    chunk.patchJump(offset, static_cast<int16_t>(jump));
}

void Compiler::emitLoop(size_t loopStart, Chunk& chunk) {
    emitOpCode(OpCode::OP_LOOP, chunk);
    size_t offset = chunk.getCurrentOffset() - loopStart + 2;
    if (offset > UINT16_MAX) {
        error("Loop body too large");
    }
    chunk.writeU16(static_cast<uint16_t>(offset));
}

void Compiler::namedVariable(const std::string& name, bool isAssignment, Chunk& chunk) {
    // Try to resolve as local
    int local = current->resolveLocal(name);
    if (local != -1) {
        if (isAssignment) {
            emitOpCode(OpCode::OP_SET_LOCAL, chunk);
            emitByte(static_cast<uint8_t>(local), chunk);
        } else {
            emitOpCode(OpCode::OP_GET_LOCAL, chunk);
            emitByte(static_cast<uint8_t>(local), chunk);
        }
        return;
    }

    // Try to resolve as upvalue
    int upvalue = current->resolveUpvalue(name);
    if (upvalue != -1) {
        if (isAssignment) {
            emitOpCode(OpCode::OP_SET_UPVALUE, chunk);
            emitByte(static_cast<uint8_t>(upvalue), chunk);
        } else {
            emitOpCode(OpCode::OP_GET_UPVALUE, chunk);
            emitByte(static_cast<uint8_t>(upvalue), chunk);
        }
        return;
    }

    // Treat as global
    uint16_t global = identifierConstant(name, chunk);
    if (isAssignment) {
        emitOpCode(OpCode::OP_SET_GLOBAL, chunk);
        chunk.writeU16(global);
    } else {
        emitOpCode(OpCode::OP_GET_GLOBAL, chunk);
        chunk.writeU16(global);
    }
}

void Compiler::defineVariable(uint16_t global, Chunk& chunk) {
    if (current->scopeDepth > 0) {
        // Local - already on stack
        current->markInitialized();
        return;
    }

    // Global
    emitOpCode(OpCode::OP_DEFINE_GLOBAL, chunk);
    chunk.writeU16(global);
}

void Compiler::error(const std::string& message) {
    hadError = true;
    errorMessage = message;
    std::cerr << "Compilation error: " << message << std::endl;
}

} // namespace as3
