#include "parser.h"
#include <sstream>
#include <iostream>

namespace as3 {

Parser::Parser(const std::string& source)
    : _lexer(source)
    , _position(0) {
    _tokens = _lexer.tokenize();
    _eofToken.type = TokenType::EndOfFile;
    _eofToken.keyword = KeywordType::None;
}

Parser::~Parser() {
}

const Token& Parser::currentToken() const {
    if (_position < _tokens.size()) {
        return _tokens[_position];
    }
    return _eofToken;
}

const Token& Parser::peek(int offset) const {
    size_t idx = _position + offset;
    if (idx < _tokens.size()) {
        return _tokens[idx];
    }
    return _eofToken;
}

void Parser::advance() {
    if (_position < _tokens.size()) {
        _position++;
    }
}

bool Parser::check(TokenType type) const {
    return currentToken().type == type;
}

bool Parser::check(KeywordType keyword) const {
    return currentToken().type == TokenType::Keyword && currentToken().keyword == keyword;
}

bool Parser::peekForInOrOf() {
    size_t savedPos = _position;
    if (check(KeywordType::Var) || check(KeywordType::Const) || check(KeywordType::Let)) {
        advance();
        if (check(TokenType::Identifier)) {
            advance();
            if (check(KeywordType::In)) {
                _position = savedPos;
                return true;
            }
            if (check(TokenType::Colon)) {
                advance();
                if (check(TokenType::Identifier)) {
                    advance();
                    if (check(KeywordType::In)) {
                        _position = savedPos;
                        return true;
                    }
                }
            }
        }
    }
    _position = savedPos;
    return false;
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::match(KeywordType keyword) {
    if (check(keyword)) {
        advance();
        return true;
    }
    return false;
}

void Parser::expect(bool condition, const std::string& message) {
    if (!condition) {
        addError(message);
    }
}

void Parser::expectToken(TokenType type, const std::string& message) {
    if (!check(type)) {
        addError(message);
    } else {
        advance();
    }
}

void Parser::expectKeyword(KeywordType keyword, const std::string& message) {
    if (!check(keyword)) {
        addError(message);
    } else {
        advance();
    }
}

void Parser::addError(const std::string& message) {
    std::ostringstream oss;
    oss << "Parse error at line " << currentToken().start.line
        << ", column " << currentToken().start.column << ": " << message;
    if (currentToken().type != TokenType::EndOfFile) {
        oss << " but found '" << currentToken().text << "'";
    } else {
        oss << " but found end of file";
    }
    _errors += oss.str() + "\n";
}

void Parser::synchronize() {
    while (!check(TokenType::EndOfFile)) {
        if (check(TokenType::SemiColon)) {
            advance();
            return;
        }

        switch (currentToken().keyword) {
            case KeywordType::Class:
            case KeywordType::Function:
            case KeywordType::Var:
            case KeywordType::Const:
            case KeywordType::Let:
            case KeywordType::For:
            case KeywordType::If:
            case KeywordType::While:
            case KeywordType::Return:
            case KeywordType::Try:
            case KeywordType::Import:
            case KeywordType::Package:
            case KeywordType::Namespace:
                return;
            default:
                break;
        }

        if (check(TokenType::RBrace) || check(TokenType::RBracket) || check(TokenType::RParen)) {
            return;
        }

        advance();
    }
}

std::string Parser::parseQualifiedName() {
    if (!check(TokenType::Identifier)) {
        addError("Expected identifier in qualified name");
        return "";
    }

    std::string name = currentToken().text;
    advance();

    while (match(TokenType::Dot)) {
        if (!check(TokenType::Identifier)) {
            addError("Expected identifier after '.' in qualified name");
            return name;
        }
        name += "." + currentToken().text;
        advance();
    }

    return name;
}

std::unique_ptr<Program> Parser::parse() {
    auto program = std::make_unique<Program>();

    while (!check(TokenType::EndOfFile)) {
        auto stmt = parseTopLevel();
        if (stmt) {
            program->statements.push_back(std::move(stmt));
        } else {
            synchronize();
        }
    }

    return program;
}

std::vector<std::unique_ptr<ImportDirective>> Parser::parseImports() {
    std::vector<std::unique_ptr<ImportDirective>> imports;

    while (match(KeywordType::Import)) {
        std::string path = parseQualifiedName();

        std::string name;
        bool isWildcard = false;

        // Check for wildcard import: import com.example.*;
        if (match(TokenType::Dot)) {
            if (match(TokenType::Multiply)) {
                isWildcard = true;
                name = "*";
            } else {
                addError("Expected '*' after '.' in import");
            }
        }

        // TODO: Handle 'as' alias for imports
        // if (match(KeywordType::As)) {
        //     name = parseIdentifier()->name;
        // }

        if (!isWildcard && !path.empty()) {
            size_t lastDot = path.rfind('.');
            if (lastDot != std::string::npos) {
                name = path.substr(lastDot + 1);
                path = path.substr(0, lastDot);
            } else {
                name = path;
                path = "";
            }
        }

        expectToken(TokenType::SemiColon, "Expected ';' after import");

        auto id = std::make_unique<Identifier>(name);
        auto import = std::make_unique<ImportDirective>(path, std::move(id), isWildcard);
        imports.push_back(std::move(import));
    }

    return imports;
}

std::unique_ptr<AstNode> Parser::parseTopLevel() {
    if (match(KeywordType::Package)) {
        return parsePackage();
    }

    if (check(KeywordType::Import)) {
        // Parse single import as a statement-like node
        auto imports = parseImports();
        if (!imports.empty()) {
            // Return the first import (there should only be one)
            return std::move(imports[0]);
        }
    }

    return parseStatement();
}

std::unique_ptr<PackageDefinition> Parser::parsePackage() {
    std::string packageName = parseQualifiedName();
    auto name = std::make_unique<Identifier>(packageName);

    expectToken(TokenType::LBrace, "Expected '{' after package name");

    auto package = std::make_unique<PackageDefinition>(std::move(name), parseImports(), std::vector<AstNodePtr>());

    while (!check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
        auto member = parseTopLevel();
        if (member) {
            package->members.push_back(std::move(member));
        } else {
            synchronize();
        }
    }

    expectToken(TokenType::RBrace, "Expected '}' after package body");

    return package;
}

std::unique_ptr<Statement> Parser::parseStatement() {
    if (check(TokenType::LBrace)) {
        return parseBlockStatement();
    }
    if (check(KeywordType::If)) {
        return parseIfStatement();
    }
    if (check(KeywordType::Switch)) {
        return parseSwitchStatement();
    }
    if (check(KeywordType::While)) {
        return parseWhileStatement();
    }
    if (check(KeywordType::Do)) {
        return parseDoWhileStatement();
    }
    if (check(KeywordType::For)) {
        return parseForStatement();
    }
    if (check(KeywordType::Break)) {
        return parseBreakStatement();
    }
    if (check(KeywordType::Continue)) {
        return parseContinueStatement();
    }
    if (check(KeywordType::Return)) {
        return parseReturnStatement();
    }
    if (check(KeywordType::Throw)) {
        return parseThrowStatement();
    }
    if (check(KeywordType::Try)) {
        return parseTryStatement();
    }
    if (check(KeywordType::With)) {
        return parseWithStatement();
    }
    if (check(KeywordType::Var) || check(KeywordType::Const) || check(KeywordType::Let)) {
        return parseVariableStatement();
    }
    if (check(KeywordType::Function)) {
        return parseFunctionDeclaration();
    }
    if (check(KeywordType::Class)) {
        return parseClassDeclaration();
    }
    if (check(KeywordType::Namespace)) {
        return parseNamespaceDeclaration();
    }
    if (check(TokenType::SemiColon)) {
        return parseEmptyStatement();
    }
    if (check(TokenType::Identifier)) {
        if (peek(1).type == TokenType::Colon) {
            return parseLabeledStatement();
        }
        if (auto legacy = parseLegacyBlock()) {
            return legacy;
        }
    }

    return parseExpressionStatement();
}

// ActionScript 1 and 2 blocks, which name what they are for in parentheses
// and then give a block: on (release) { ... } and onClipEvent (enterFrame)
// { ... } become calls to __on and __onClipEvent with the event names and
// the block as a function, for the host to run when the event comes;
// tellTarget (target) { ... } becomes __tellTarget(target, function); and
// ifFrameLoaded (frame) { ... } becomes if (ifFrameLoaded(frame)) { ... }.
// A call to a function of one of these names, with no block after it, is
// left as the call it is.
std::unique_ptr<Statement> Parser::parseLegacyBlock() {
    const std::string word = currentToken().text;
    if (word != "on" && word != "onClipEvent" && word != "ifFrameLoaded" && word != "tellTarget") {
        return nullptr;
    }
    if (peek(1).type != TokenType::LParen) {
        return nullptr;
    }
    // The parentheses' close, and a block after it.
    int depth = 0;
    int close = -1;
    for (int i = 1;; ++i) {
        const Token& token = peek(i);
        if (token.type == TokenType::EndOfFile) {
            return nullptr;
        }
        if (token.type == TokenType::LParen) {
            ++depth;
        } else if (token.type == TokenType::RParen && --depth == 0) {
            close = i;
            break;
        }
    }
    if (peek(close + 1).type != TokenType::LBrace) {
        return nullptr;
    }

    advance();  // the word
    advance();  // (
    const auto stringLiteral = [](const std::string& text) {
        auto literal = std::make_unique<Literal>(Literal::LiteralKind::String);
        literal->valueType = Literal::LiteralValueType::String;
        literal->stringValue = text;
        return literal;
    };
    const auto blockFunction = [this]() {
        StatementPtr body = parseBlockStatement();
        return std::make_unique<FunctionExpression>(nullptr, std::vector<std::unique_ptr<Parameter>>(), nullptr,
                                                    std::move(body));
    };

    if (word == "on" || word == "onClipEvent") {
        // The events, as written: release, press, keyPress "<Enter>"...
        ExpressionList events;
        std::string event;
        while (!check(TokenType::RParen) && !check(TokenType::EndOfFile)) {
            if (check(TokenType::Comma)) {
                if (!event.empty()) {
                    events.push_back(stringLiteral(event));
                }
                event.clear();
            } else {
                const Token& token = currentToken();
                const std::string part = token.type == TokenType::String ? token.stringValue : token.text;
                event += event.empty() ? part : " " + part;
            }
            advance();
        }
        if (!event.empty()) {
            events.push_back(stringLiteral(event));
        }
        expectToken(TokenType::RParen, "Expected ')' after the events");
        ExpressionList arguments;
        arguments.push_back(std::make_unique<ArrayExpression>(std::move(events)));
        arguments.push_back(blockFunction());
        auto call = std::make_unique<CallExpression>(
            std::make_unique<Identifier>(word == "on" ? "__on" : "__onClipEvent"), std::move(arguments));
        return std::make_unique<ExpressionStatement>(std::move(call));
    }

    ExpressionList arguments;
    if (!check(TokenType::RParen)) {
        arguments.push_back(parseAssignmentExpression());
        while (match(TokenType::Comma)) {
            arguments.push_back(parseAssignmentExpression());
        }
    }
    expectToken(TokenType::RParen, "Expected ')'");
    if (word == "tellTarget") {
        arguments.push_back(blockFunction());
        auto call = std::make_unique<CallExpression>(std::make_unique<Identifier>("__tellTarget"), std::move(arguments));
        return std::make_unique<ExpressionStatement>(std::move(call));
    }
    auto test = std::make_unique<CallExpression>(std::make_unique<Identifier>("ifFrameLoaded"), std::move(arguments));
    StatementPtr body = parseBlockStatement();
    return std::make_unique<IfStatement>(std::move(test), std::move(body));
}

std::unique_ptr<Statement> Parser::parseBlockStatement() {
    std::vector<StatementPtr> statements;

    expectToken(TokenType::LBrace, "Expected '{'");

    while (!check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
        auto stmt = parseStatement();
        if (stmt) {
            statements.push_back(std::move(stmt));
        } else {
            synchronize();
        }
    }

    expectToken(TokenType::RBrace, "Expected '}'");

    return std::make_unique<BlockStatement>(std::move(statements));
}

// A statement ends at a semicolon, or, as ActionScript allows, without one
// at the end of a line, before a closing brace or at the end of the script.
void Parser::endStatement(const std::string& message) {
    if (match(TokenType::SemiColon)) {
        return;
    }
    if (check(TokenType::RBrace) || check(TokenType::EndOfFile) || currentToken().newLineBefore) {
        return;
    }
    expectToken(TokenType::SemiColon, message);
}

std::unique_ptr<Statement> Parser::parseEmptyStatement() {
    expectToken(TokenType::SemiColon, "Expected ';'");
    return std::make_unique<EmptyStatement>();
}

std::unique_ptr<Statement> Parser::parseExpressionStatement() {
    auto expr = parseExpression();
    if (!expr) {
        addError("Failed to parse expression");
        return nullptr;
    }
    endStatement("Expected ';' after expression");
    return std::make_unique<ExpressionStatement>(std::move(expr));
}

std::unique_ptr<Statement> Parser::parseIfStatement() {
    expectKeyword(KeywordType::If, "Expected 'if'");
    expectToken(TokenType::LParen, "Expected '(' after 'if'");
    auto test = parseExpression();
    if (!test) {
        addError("Failed to parse if condition");
        return nullptr;
    }
    expectToken(TokenType::RParen, "Expected ')'");
    auto consequent = parseStatement();
    if (!consequent) {
        return nullptr;
    }

    StatementPtr alternate = nullptr;
    if (match(KeywordType::Else)) {
        alternate = parseStatement();
    }

    return std::make_unique<IfStatement>(std::move(test), std::move(consequent), std::move(alternate));
}

std::unique_ptr<Statement> Parser::parseSwitchStatement() {
    expectKeyword(KeywordType::Switch, "Expected 'switch'");
    expectToken(TokenType::LParen, "Expected '(' after 'switch'");
    auto discriminant = parseExpression();
    if (!discriminant) {
        addError("Failed to parse switch discriminant");
        return nullptr;
    }
    expectToken(TokenType::RParen, "Expected ')'");
    expectToken(TokenType::LBrace, "Expected '{'");

    std::vector<std::unique_ptr<CaseClause>> cases;

    while (!check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
        if (match(KeywordType::Default)) {
            expectToken(TokenType::Colon, "Expected ':'");
            std::vector<StatementPtr> consequent;
            while (!check(KeywordType::Case) && !check(KeywordType::Default) &&
                   !check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
                auto stmt = parseStatement();
                if (stmt) {
                    consequent.push_back(std::move(stmt));
                }
            }
            cases.push_back(std::make_unique<CaseClause>(nullptr, std::move(consequent)));
        } else if (match(KeywordType::Case)) {
            auto caseExpr = parseExpression();
            if (!caseExpr) {
                addError("Failed to parse case expression");
                continue;
            }
            expectToken(TokenType::Colon, "Expected ':'");
            std::vector<StatementPtr> consequent;
            while (!check(KeywordType::Case) && !check(KeywordType::Default) &&
                   !check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
                auto stmt = parseStatement();
                if (stmt) {
                    consequent.push_back(std::move(stmt));
                }
            }
            cases.push_back(std::make_unique<CaseClause>(std::move(caseExpr), std::move(consequent)));
        } else {
            addError("Expected 'case' or 'default' in switch body");
            synchronize();
            break;
        }
    }

    expectToken(TokenType::RBrace, "Expected '}'");

    return std::make_unique<SwitchStatement>(std::move(discriminant), std::move(cases));
}

std::unique_ptr<Statement> Parser::parseWhileStatement() {
    expectKeyword(KeywordType::While, "Expected 'while'");
    expectToken(TokenType::LParen, "Expected '(' after 'while'");
    auto test = parseExpression();
    if (!test) {
        addError("Failed to parse while condition");
        return nullptr;
    }
    expectToken(TokenType::RParen, "Expected ')'");
    auto body = parseStatement();
    if (!body) {
        return nullptr;
    }

    return std::make_unique<WhileStatement>(std::move(test), std::move(body));
}

std::unique_ptr<Statement> Parser::parseDoWhileStatement() {
    expectKeyword(KeywordType::Do, "Expected 'do'");
    auto body = parseStatement();
    if (!body) {
        return nullptr;
    }
    expectKeyword(KeywordType::While, "Expected 'while' after do-statement");
    expectToken(TokenType::LParen, "Expected '(' after 'while'");
    auto test = parseExpression();
    if (!test) {
        addError("Failed to parse while condition");
        return nullptr;
    }
    expectToken(TokenType::RParen, "Expected ')'");
    endStatement("Expected ';'");

    return std::make_unique<DoWhileStatement>(std::move(body), std::move(test));
}

std::unique_ptr<Statement> Parser::parseForStatement() {
    expectKeyword(KeywordType::For, "Expected 'for'");
    
    bool isForEach = match(KeywordType::Each);
    
    expectToken(TokenType::LParen, "Expected '(' after 'for'");
    
    if (isForEach) {
        return parseForInOrOfStatement(isForEach);
    }
    
    if (check(KeywordType::In)) {
        return parseForInOrOfStatement(isForEach);
    }
    
    if (check(KeywordType::Var) || check(KeywordType::Const) || check(KeywordType::Let)) {
        if (peekForInOrOf()) {
            return parseForInOrOfStatement(isForEach);
        }
    }
    
    StatementPtr init = nullptr;
    if (!check(TokenType::SemiColon)) {
        if (check(KeywordType::Var) || check(KeywordType::Const) || check(KeywordType::Let)) {
            // Parse variable declaration without expecting semicolon
            bool isConst = false;
            if (check(KeywordType::Const)) {
                isConst = true;
                advance();
            } else if (match(KeywordType::Var) || match(KeywordType::Let)) {
            }

            VariableDeclarationList declarations;
            do {
                auto id = parseIdentifier();
                IdentifierPtr type = nullptr;
                if (match(TokenType::Colon)) {
                    type = parseIdentifier();
                }
                ExpressionPtr initExpr = nullptr;
                if (match(TokenType::Assign)) {
                    initExpr = parseAssignmentExpression();
                }
                declarations.push_back(std::make_unique<VariableDeclaration>(std::move(id), std::move(type), std::move(initExpr)));
            } while (match(TokenType::Comma));

            init = std::make_unique<VariableDefinition>(std::vector<AttributePtr>(), std::move(declarations), isConst);
        } else {
            auto expr = parseExpression();
            if (expr) {
                init = std::make_unique<ExpressionStatement>(std::move(expr));
            }
        }
    }

    expectToken(TokenType::SemiColon, "Expected ';'");

    ExpressionPtr test = nullptr;
    if (!check(TokenType::SemiColon)) {
        test = parseExpression();
    }
    expectToken(TokenType::SemiColon, "Expected ';'");

    ExpressionPtr update = nullptr;
    if (!check(TokenType::RParen)) {
        update = parseExpression();
    }
    expectToken(TokenType::RParen, "Expected ')'");

    auto body = parseStatement();
    if (!body) {
        return nullptr;
    }

    return std::make_unique<ForStatement>(std::move(init), std::move(test), std::move(update), std::move(body));
}

std::unique_ptr<Statement> Parser::parseForInOrOfStatement(bool isForEach) {
    StatementPtr init = nullptr;
    if (check(KeywordType::Var) || check(KeywordType::Const) || check(KeywordType::Let)) {
        bool isConst = check(KeywordType::Const);
        advance();  // consume var/const/let

        auto id = parseIdentifier();
        IdentifierPtr type = nullptr;
        if (match(TokenType::Colon)) {
            type = parseIdentifier();
        }
        ExpressionPtr initExpr = nullptr;
        if (match(TokenType::Assign)) {
            initExpr = parseAssignmentExpression();
        }
        auto decl = std::make_unique<VariableDeclaration>(std::move(id), std::move(type), std::move(initExpr));
        VariableDeclarationList decls;
        decls.push_back(std::move(decl));
        init = std::make_unique<VariableDefinition>(std::vector<AttributePtr>(), std::move(decls), isConst);
    } else {
        auto expr = parseExpression();
        if (expr) {
            init = std::make_unique<ExpressionStatement>(std::move(expr));
        }
    }

    ExpressionPtr right = nullptr;
    if (match(KeywordType::In)) {
        right = parseExpression();
    } else if (match(KeywordType::Of)) {
        right = parseExpression();
    } else {
        addError("Expected 'in' or 'of' in for-in/for-each statement");
    }

    expectToken(TokenType::RParen, "Expected ')'");

    auto body = parseStatement();
    if (!body) {
        return nullptr;
    }

    return std::make_unique<ForInStatement>(std::move(init), std::move(right), std::move(body));
}

std::unique_ptr<Statement> Parser::parseBreakStatement() {
    expectKeyword(KeywordType::Break, "Expected 'break'");
    IdentifierPtr label = nullptr;
    if (check(TokenType::Identifier) && !currentToken().newLineBefore) {
        label = parseIdentifier();
    }
    endStatement("Expected ';'");
    return std::make_unique<BreakStatement>(std::move(label));
}

std::unique_ptr<Statement> Parser::parseContinueStatement() {
    expectKeyword(KeywordType::Continue, "Expected 'continue'");
    IdentifierPtr label = nullptr;
    if (check(TokenType::Identifier) && !currentToken().newLineBefore) {
        label = parseIdentifier();
    }
    endStatement("Expected ';'");
    return std::make_unique<ContinueStatement>(std::move(label));
}

std::unique_ptr<Statement> Parser::parseReturnStatement() {
    expectKeyword(KeywordType::Return, "Expected 'return'");
    ExpressionPtr argument = nullptr;
    if (!check(TokenType::SemiColon) && !check(TokenType::EndOfFile) && !check(TokenType::RBrace) &&
        !currentToken().newLineBefore) {
        argument = parseExpression();
    }
    endStatement("Expected ';'");
    return std::make_unique<ReturnStatement>(std::move(argument));
}

std::unique_ptr<Statement> Parser::parseThrowStatement() {
    expectKeyword(KeywordType::Throw, "Expected 'throw'");
    auto argument = parseExpression();
    if (!argument) {
        addError("Failed to parse throw expression");
        return nullptr;
    }
    endStatement("Expected ';'");
    return std::make_unique<ThrowStatement>(std::move(argument));
}

std::unique_ptr<Statement> Parser::parseTryStatement() {
    expectKeyword(KeywordType::Try, "Expected 'try'");
    auto block = parseBlockStatement();
    if (!block) {
        return nullptr;
    }

    IdentifierPtr catchVar = nullptr;
    StatementPtr catchBody = nullptr;

    if (match(KeywordType::Catch)) {
        expectToken(TokenType::LParen, "Expected '(' after 'catch'");
        catchVar = parseIdentifier();

        if (check(TokenType::Colon)) {
            advance();
            parseIdentifier();
        }

        expectToken(TokenType::RParen, "Expected ')'");
        catchBody = parseBlockStatement();
    }

    StatementPtr finallyBody = nullptr;
    if (match(KeywordType::Finally)) {
        finallyBody = parseBlockStatement();
    }

    return std::make_unique<TryStatement>(std::move(block), std::move(catchVar),
                                          std::move(catchBody), std::move(finallyBody));
}

std::unique_ptr<Statement> Parser::parseWithStatement() {
    expectKeyword(KeywordType::With, "Expected 'with'");
    expectToken(TokenType::LParen, "Expected '(' after 'with'");
    auto object = parseExpression();
    if (!object) {
        addError("Failed to parse with expression");
        return nullptr;
    }
    expectToken(TokenType::RParen, "Expected ')'");
    auto body = parseStatement();
    if (!body) {
        return nullptr;
    }

    return std::make_unique<WithStatement>(std::move(object), std::move(body));
}

std::unique_ptr<Statement> Parser::parseLabeledStatement() {
    auto label = parseIdentifier();
    expectToken(TokenType::Colon, "Expected ':'");
    auto body = parseStatement();
    if (!body) {
        return nullptr;
    }

    return std::make_unique<LabeledStatement>(std::move(label), std::move(body));
}

std::unique_ptr<Statement> Parser::parseVariableStatement() {
    bool isConst = false;
    if (check(KeywordType::Const)) {
        isConst = true;
        advance();
    } else if (match(KeywordType::Var) || match(KeywordType::Let)) {
    }

    VariableDeclarationList declarations;

    do {
        auto id = parseIdentifier();
        IdentifierPtr type = nullptr;

        if (match(TokenType::Colon)) {
            type = parseIdentifier();
        }

        ExpressionPtr init = nullptr;

        if (match(TokenType::Assign)) {
            init = parseAssignmentExpression();
        }

        declarations.push_back(std::make_unique<VariableDeclaration>(std::move(id), std::move(type), std::move(init)));
    } while (match(TokenType::Comma));

    endStatement("Expected ';'");

    return std::make_unique<VariableDefinition>(std::vector<AttributePtr>(), std::move(declarations), isConst);
}

std::unique_ptr<Statement> Parser::parseFunctionDeclaration() {
    expectKeyword(KeywordType::Function, "Expected 'function'");

    auto name = parseIdentifier();
    expectToken(TokenType::LParen, "Expected '('");

    auto params = parseParameters();

    IdentifierPtr returnType = nullptr;
    if (match(TokenType::Colon)) {
        returnType = parseIdentifier();
    }

    auto body = parseBlockStatement();
    if (!body) {
        return nullptr;
    }

    return std::make_unique<FunctionDefinition>(
        std::vector<AttributePtr>(),
        std::move(name),
        std::move(params),
        std::move(returnType),
        std::move(body)
    );
}

std::unique_ptr<Statement> Parser::parseClassDeclaration() {
    expectKeyword(KeywordType::Class, "Expected 'class'");

    auto name = parseIdentifier();

    IdentifierPtr superClass = nullptr;
    if (match(KeywordType::Extends)) {
        superClass = parseIdentifier();
    }

    std::vector<IdentifierPtr> interfaces;
    if (match(KeywordType::Implements)) {
        do {
            interfaces.push_back(parseIdentifier());
        } while (match(TokenType::Comma));
    }

    expectToken(TokenType::LBrace, "Expected '{'");

    std::vector<AstNodePtr> members;
    while (!check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
        auto attrs = parseAttributes();

        if (check(KeywordType::Function)) {
            auto func = parseFunctionDefinitionWithAttributes(std::move(attrs));
            if (func) {
                members.push_back(std::move(func));
            }
        } else if (check(KeywordType::Var) || check(KeywordType::Const) || check(KeywordType::Let)) {
            auto var = parseVariableStatementWithAttributes(std::move(attrs));
            if (var) {
                members.push_back(std::move(var));
            }
        } else if (check(KeywordType::Get) || check(KeywordType::Set)) {
            auto prop = parsePropertyWithAttributes(std::move(attrs));
            if (prop) {
                members.push_back(std::move(prop));
            }
        } else if (attrs.empty()) {
            // Try parsing as a regular statement if no attributes
            auto member = parseStatement();
            if (member) {
                members.push_back(std::move(member));
            } else {
                synchronize();
            }
        } else {
            addError("Expected function, variable, or property after attributes");
            synchronize();
        }
    }

    expectToken(TokenType::RBrace, "Expected '}'");

    return std::make_unique<ClassDefinition>(
        std::vector<AttributePtr>(),
        std::move(name),
        std::move(superClass),
        std::move(interfaces),
        std::move(members)
    );
}

std::unique_ptr<Statement> Parser::parseNamespaceDeclaration() {
    expectKeyword(KeywordType::Namespace, "Expected 'namespace'");

    auto name = parseIdentifier();

    expectToken(TokenType::Assign, "Expected '=' after namespace name");

    IdentifierPtr uri = nullptr;
    if (check(TokenType::String)) {
        uri = std::make_unique<Identifier>(currentToken().stringValue);
        advance();
    } else {
        addError("Expected string literal for namespace URI");
        return nullptr;
    }

    expectToken(TokenType::SemiColon, "Expected ';' after namespace declaration");

    return std::make_unique<NamespaceDefinition>(std::move(name), std::move(uri));
}

std::unique_ptr<Expression> Parser::parseExpression() {
    return parseAssignmentExpression();
}

std::unique_ptr<Expression> Parser::parseAssignmentExpression() {
    auto left = parseConditionalExpression();
    if (!left) {
        return nullptr;
    }

    if (check(TokenType::Assign) || check(TokenType::PlusAssign) ||
        check(TokenType::MinusAssign) || check(TokenType::MultiplyAssign) ||
        check(TokenType::DivideAssign) || check(TokenType::RemainderAssign) ||
        check(TokenType::LeftShiftAssign) || check(TokenType::RightShiftAssign) ||
        check(TokenType::UnsignedRightShiftAssign) ||
        check(TokenType::AmpersandAssign) || check(TokenType::PipeAssign) ||
        check(TokenType::CaretAssign) || check(TokenType::PowerAssign) ||
        check(TokenType::NullCoalescingAssign) || check(TokenType::AndAssign) ||
        check(TokenType::OrAssign)) {

        TokenType op = currentToken().type;
        advance();
        auto right = parseAssignmentExpression();
        if (!right) {
            addError("Failed to parse right side of assignment");
            return nullptr;
        }

        return std::make_unique<AssignmentExpression>(
            getBinaryOperator(op),
            std::move(left),
            std::move(right)
        );
    }

    return left;
}

std::unique_ptr<Expression> Parser::parseConditionalExpression() {
    auto test = parseNullCoalescingExpression();
    if (!test) {
        return nullptr;
    }

    if (match(TokenType::Question)) {
        auto consequent = parseAssignmentExpression();
        if (!consequent) {
            addError("Failed to parse consequent of conditional expression");
            return nullptr;
        }
        expectToken(TokenType::Colon, "Expected ':'");
        auto alternate = parseAssignmentExpression();
        if (!alternate) {
            addError("Failed to parse alternate of conditional expression");
            return nullptr;
        }

        return std::make_unique<ConditionalExpression>(
            std::move(test),
            std::move(consequent),
            std::move(alternate)
        );
    }

    return test;
}

std::unique_ptr<Expression> Parser::parseNullCoalescingExpression() {
    auto left = parseLogicalOrExpression();
    if (!left) {
        return nullptr;
    }

    if (match(TokenType::NullCoalescing)) {
        auto right = parseNullCoalescingExpression();
        if (!right) {
            addError("Failed to parse right operand of null coalescing expression");
            return nullptr;
        }

        return std::make_unique<BinaryExpression>(
            OperatorType::NullCoalescing,
            std::move(left),
            std::move(right)
        );
    }

    return left;
}

std::unique_ptr<Expression> Parser::parseLogicalOrExpression() {
    auto left = parseLogicalAndExpression();
    if (!left) {
        return nullptr;
    }

    while (match(TokenType::LogicalOr)) {
        auto right = parseLogicalAndExpression();
        if (!right) {
            addError("Failed to parse right operand of logical OR expression");
            return nullptr;
        }

        left = std::make_unique<BinaryExpression>(
            OperatorType::LogicalOr,
            std::move(left),
            std::move(right)
        );
    }

    return left;
}

std::unique_ptr<Expression> Parser::parseLogicalAndExpression() {
    auto left = parseBitwiseOrExpression();
    if (!left) {
        return nullptr;
    }

    while (match(TokenType::LogicalAnd)) {
        auto right = parseBitwiseOrExpression();
        if (!right) {
            addError("Failed to parse right operand of logical AND expression");
            return nullptr;
        }

        left = std::make_unique<BinaryExpression>(
            OperatorType::LogicalAnd,
            std::move(left),
            std::move(right)
        );
    }

    return left;
}

std::unique_ptr<Expression> Parser::parseBitwiseOrExpression() {
    auto left = parseBitwiseXorExpression();
    if (!left) {
        return nullptr;
    }

    while (match(TokenType::Pipe)) {
        auto right = parseBitwiseXorExpression();
        if (!right) {
            addError("Failed to parse right operand of bitwise OR expression");
            return nullptr;
        }

        left = std::make_unique<BinaryExpression>(
            OperatorType::BitwiseOr,
            std::move(left),
            std::move(right)
        );
    }

    return left;
}

std::unique_ptr<Expression> Parser::parseBitwiseXorExpression() {
    auto left = parseBitwiseAndExpression();
    if (!left) {
        return nullptr;
    }

    while (match(TokenType::Caret)) {
        auto right = parseBitwiseAndExpression();
        if (!right) {
            addError("Failed to parse right operand of bitwise XOR expression");
            return nullptr;
        }

        left = std::make_unique<BinaryExpression>(
            OperatorType::BitwiseXor,
            std::move(left),
            std::move(right)
        );
    }

    return left;
}

std::unique_ptr<Expression> Parser::parseBitwiseAndExpression() {
    auto left = parseEqualityExpression();
    if (!left) {
        return nullptr;
    }

    while (match(TokenType::Ampersand)) {
        auto right = parseEqualityExpression();
        if (!right) {
            addError("Failed to parse right operand of bitwise AND expression");
            return nullptr;
        }

        left = std::make_unique<BinaryExpression>(
            OperatorType::BitwiseAnd,
            std::move(left),
            std::move(right)
        );
    }

    return left;
}

std::unique_ptr<Expression> Parser::parseEqualityExpression() {
    auto left = parseRelationalExpression();
    if (!left) {
        return nullptr;
    }

    while (check(TokenType::Equals) || check(TokenType::NotEquals) ||
           check(TokenType::StrictEquals) || check(TokenType::StrictNotEquals)) {

        TokenType op = currentToken().type;
        advance();
        auto right = parseRelationalExpression();
        if (!right) {
            addError("Failed to parse right operand of equality expression");
            return nullptr;
        }

        left = std::make_unique<BinaryExpression>(
            getBinaryOperator(op),
            std::move(left),
            std::move(right)
        );
    }

    return left;
}

std::unique_ptr<Expression> Parser::parseRelationalExpression() {
    auto left = parseShiftExpression();
    if (!left) {
        return nullptr;
    }

    while (check(TokenType::LessThan) || check(TokenType::GreaterThan) ||
           check(TokenType::LessThanOrEqual) || check(TokenType::GreaterThanOrEqual) ||
           check(KeywordType::Instanceof) || check(KeywordType::In) ||
           check(KeywordType::Is) || check(KeywordType::As)) {

        TokenType op = currentToken().type;
        KeywordType kw = currentToken().keyword;
        advance();
        auto right = parseShiftExpression();
        if (!right) {
            addError("Failed to parse right operand of relational expression");
            return nullptr;
        }

        if (kw == KeywordType::Instanceof) {
            left = std::make_unique<BinaryExpression>(
                OperatorType::Instanceof,
                std::move(left),
                std::move(right)
            );
        } else if (kw == KeywordType::In) {
            left = std::make_unique<BinaryExpression>(
                OperatorType::In,
                std::move(left),
                std::move(right)
            );
        } else if (kw == KeywordType::Is) {
            left = std::make_unique<BinaryExpression>(
                OperatorType::Is,
                std::move(left),
                std::move(right)
            );
        } else if (kw == KeywordType::As) {
            left = std::make_unique<BinaryExpression>(
                OperatorType::As,
                std::move(left),
                std::move(right)
            );
        } else {
            left = std::make_unique<BinaryExpression>(
                getBinaryOperator(op),
                std::move(left),
                std::move(right)
            );
        }
    }

    return left;
}

std::unique_ptr<Expression> Parser::parseShiftExpression() {
    auto left = parseAdditiveExpression();
    if (!left) {
        return nullptr;
    }

    while (check(TokenType::LeftShift) || check(TokenType::RightShift) ||
           check(TokenType::UnsignedRightShift)) {

        TokenType op = currentToken().type;
        advance();
        auto right = parseAdditiveExpression();
        if (!right) {
            addError("Failed to parse right operand of shift expression");
            return nullptr;
        }

        left = std::make_unique<BinaryExpression>(
            getBinaryOperator(op),
            std::move(left),
            std::move(right)
        );
    }

    return left;
}

std::unique_ptr<Expression> Parser::parseAdditiveExpression() {
    auto left = parseMultiplicativeExpression();
    if (!left) {
        return nullptr;
    }

    while (check(TokenType::Plus) || check(TokenType::Minus)) {
        TokenType op = currentToken().type;
        advance();
        auto right = parseMultiplicativeExpression();
        if (!right) {
            addError("Failed to parse right operand of additive expression");
            return nullptr;
        }

        left = std::make_unique<BinaryExpression>(
            getBinaryOperator(op),
            std::move(left),
            std::move(right)
        );
    }

    return left;
}

std::unique_ptr<Expression> Parser::parseMultiplicativeExpression() {
    auto left = parseUnaryExpression();
    if (!left) {
        return nullptr;
    }

    while (check(TokenType::Multiply) || check(TokenType::Divide) ||
           check(TokenType::Remainder)) {

        TokenType op = currentToken().type;
        advance();
        auto right = parseUnaryExpression();
        if (!right) {
            addError("Failed to parse right operand of multiplicative expression");
            return nullptr;
        }

        left = std::make_unique<BinaryExpression>(
            getBinaryOperator(op),
            std::move(left),
            std::move(right)
        );
    }

    return left;
}

std::unique_ptr<Expression> Parser::parseUnaryExpression() {
    if (check(TokenType::PreIncrement) || check(TokenType::PreDecrement) ||
        check(TokenType::Plus) || check(TokenType::Minus) || check(TokenType::Tilde) ||
        check(TokenType::Exclamation) || check(KeywordType::Delete) ||
        check(KeywordType::Void) || check(KeywordType::Typeof)) {

        TokenType op = currentToken().type;
        advance();
        auto operand = parseUnaryExpression();
        if (!operand) {
            addError("Failed to parse operand of unary expression");
            return nullptr;
        }

        return std::make_unique<UnaryExpression>(
            getBinaryOperator(op),
            std::move(operand),
            false
        );
    }

    return parsePostfixExpression();
}

std::unique_ptr<Expression> Parser::parsePostfixExpression() {
    auto expr = parseCallMemberExpression();
    if (!expr) {
        return nullptr;
    }

    if (check(TokenType::Increment) || check(TokenType::Decrement)) {
        if (!check(TokenType::EndOfFile)) {
            TokenType op = currentToken().type;
            advance();

            if (op == TokenType::Increment) {
                return std::make_unique<UnaryExpression>(OperatorType::PostIncrement, std::move(expr), true);
            } else {
                return std::make_unique<UnaryExpression>(OperatorType::PostDecrement, std::move(expr), true);
            }
        }
    }

    return expr;
}

std::unique_ptr<Expression> Parser::parseCallMemberExpression() {
    if (check(KeywordType::New)) {
        return parseNewExpression();
    }

    auto expr = parsePrimaryExpression();
    if (!expr) {
        return nullptr;
    }

    while (check(TokenType::Dot) || check(TokenType::LParen) ||
           check(TokenType::LBracket) || check(TokenType::QuestionDot)) {

        if (match(TokenType::LParen)) {
            ExpressionList args;
            while (!check(TokenType::RParen) && !check(TokenType::EndOfFile)) {
                auto arg = parseAssignmentExpression();
                if (!arg) {
                    addError("Failed to parse function argument");
                    synchronize();
                    break;
                }
                args.push_back(std::move(arg));
                if (!match(TokenType::Comma)) break;
            }
            expectToken(TokenType::RParen, "Expected ')'");

            auto call = std::make_unique<CallExpression>(std::move(expr), std::move(args));
            expr = std::move(call);
        } else if (match(TokenType::Dot) || match(TokenType::QuestionDot)) {
            auto property = parseIdentifier();
            auto member = std::make_unique<MemberExpression>(
                std::move(expr),
                std::move(property),
                false,
                false
            );
            expr = std::move(member);
        } else if (match(TokenType::LBracket)) {
            auto index = parseExpression();
            if (!index) {
                addError("Failed to parse computed property expression");
                return nullptr;
            }
            expectToken(TokenType::RBracket, "Expected ']'");

            auto member = std::make_unique<MemberExpression>(
                std::move(expr),
                std::move(index),
                false  // optional
            );
            expr = std::move(member);
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<Expression> Parser::parseNewExpression() {
    expectKeyword(KeywordType::New, "Expected 'new'");
    auto callee = parsePrimaryExpression();
    if (!callee) {
        addError("Failed to parse new expression callee");
        return nullptr;
    }

    ExpressionList args;
    if (match(TokenType::LParen)) {
        while (!check(TokenType::RParen) && !check(TokenType::EndOfFile)) {
            auto arg = parseAssignmentExpression();
            if (!arg) {
                addError("Failed to parse constructor argument");
                synchronize();
                break;
            }
            args.push_back(std::move(arg));
            if (!match(TokenType::Comma)) break;
        }
        expectToken(TokenType::RParen, "Expected ')'");
    }

    return std::make_unique<NewExpression>(std::move(callee), std::move(args));
}

std::unique_ptr<Expression> Parser::parsePrimaryExpression() {
    if (check(TokenType::Identifier)) {
        auto id = parseIdentifier();
        return id;
    }
    
    if (check(TokenType::Number)) {
        auto lit = std::make_unique<Literal>(Literal::LiteralKind::Numeric);
        lit->valueType = Literal::LiteralValueType::Number;
        lit->numberValue = currentToken().numericValue;
        advance();
        return lit;
    }
    
    if (check(TokenType::String)) {
        auto lit = std::make_unique<Literal>(Literal::LiteralKind::String);
        lit->valueType = Literal::LiteralValueType::String;
        lit->stringValue = currentToken().stringValue;
        advance();
        return lit;
    }
    
    if (check(TokenType::Null)) {
        auto lit = std::make_unique<Literal>(Literal::LiteralKind::Null);
        advance();
        return lit;
    }
    
    if (check(TokenType::Boolean)) {
        auto lit = std::make_unique<Literal>(Literal::LiteralKind::Boolean);
        lit->valueType = Literal::LiteralValueType::Bool;
        lit->boolValue = currentToken().numericValue != 0;
        advance();
        return lit;
    }
    
    if (check(KeywordType::This)) {
        advance();
        return std::make_unique<ThisExpression>();
    }
    
    if (check(KeywordType::Function)) {
        advance();
        auto name = check(TokenType::Identifier) ? parseIdentifier() : nullptr;
        expectToken(TokenType::LParen, "Expected '('");
        auto params = parseParameters();
        IdentifierPtr returnType = nullptr;
        if (match(TokenType::Colon)) {
            returnType = parseIdentifier();
        }
        auto body = parseBlockStatement();
        
        return std::make_unique<FunctionExpression>(
            std::move(name),
            std::move(params),
            std::move(returnType),
            std::move(body)
        );
    }
    
    if (match(TokenType::LBracket)) {
        ExpressionList elements;
        while (!check(TokenType::RBracket) && !check(TokenType::EndOfFile)) {
            elements.push_back(parseAssignmentExpression());
            if (!match(TokenType::Comma)) break;
        }
        expectToken(TokenType::RBracket, "Expected ']'");
        return std::make_unique<ArrayExpression>(std::move(elements));
    }
    
    if (match(TokenType::LBrace)) {
        std::vector<std::pair<ExpressionPtr, ExpressionPtr>> props;
        while (!check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
            auto key = parseAssignmentExpression();
            expectToken(TokenType::Colon, "Expected ':'");
            auto value = parseAssignmentExpression();
            props.emplace_back(std::move(key), std::move(value));
            if (!match(TokenType::Comma)) break;
        }
        expectToken(TokenType::RBrace, "Expected '}'");
        return std::make_unique<ObjectExpression>(std::move(props));
    }
    
    if (match(TokenType::LParen)) {
        auto expr = parseExpression();
        expectToken(TokenType::RParen, "Expected ')'");
        return expr;
    }
    
    addError("Unexpected token in expression");
    advance();
    return nullptr;
}

std::unique_ptr<Identifier> Parser::parseIdentifier() {
    if (check(TokenType::Identifier)) {
        auto id = std::make_unique<Identifier>(currentToken().text);
        advance();
        return id;
    }
    
    if (check(KeywordType::Void)) {
        auto id = std::make_unique<Identifier>("void");
        advance();
        return id;
    }
    
    addError("Expected identifier");
    return std::make_unique<Identifier>("");
}

std::vector<std::unique_ptr<Attribute>> Parser::parseAttributes() {
    std::vector<AttributePtr> attrs;
    
    while (check(KeywordType::Public) || check(KeywordType::Private) ||
           check(KeywordType::Protected) || check(KeywordType::Internal) ||
           check(KeywordType::Static) || check(KeywordType::Override) ||
           check(KeywordType::Final) || check(KeywordType::Native) ||
           check(KeywordType::Dynamic)) {
        
        attrs.push_back(parseAttribute());
    }
    
    return attrs;
}

std::unique_ptr<Attribute> Parser::parseAttribute() {
    if (match(KeywordType::Public)) {
        return std::make_unique<Attribute>(AttributeType::Public);
    }
    if (match(KeywordType::Private)) {
        return std::make_unique<Attribute>(AttributeType::Private);
    }
    if (match(KeywordType::Protected)) {
        return std::make_unique<Attribute>(AttributeType::Protected);
    }
    if (match(KeywordType::Internal)) {
        return std::make_unique<Attribute>(AttributeType::Internal);
    }
    if (match(KeywordType::Static)) {
        return std::make_unique<Attribute>(AttributeType::Static);
    }
    if (match(KeywordType::Override)) {
        return std::make_unique<Attribute>(AttributeType::Override);
    }
    if (match(KeywordType::Final)) {
        return std::make_unique<Attribute>(AttributeType::Final);
    }
    if (match(KeywordType::Native)) {
        return std::make_unique<Attribute>(AttributeType::Native);
    }
    if (match(KeywordType::Dynamic)) {
        return std::make_unique<Attribute>(AttributeType::Dynamic);
    }
    
    addError("Expected attribute");
    return nullptr;
}

std::unique_ptr<FunctionDefinition> Parser::parseFunctionDefinition(bool isDeclaration) {
    expectKeyword(KeywordType::Function, "Expected 'function'");
    
    auto name = parseIdentifier();
    expectToken(TokenType::LParen, "Expected '('");
    
    auto params = parseParameters();
    
    IdentifierPtr returnType = nullptr;
    if (match(TokenType::Colon)) {
        returnType = parseIdentifier();
    }
    
    auto body = parseBlockStatement();
    
    return std::make_unique<FunctionDefinition>(
        std::vector<AttributePtr>(),
        std::move(name),
        std::move(params),
        std::move(returnType),
        std::move(body)
    );
}

std::unique_ptr<ClassDefinition> Parser::parseClassDefinition() {
    expectKeyword(KeywordType::Class, "Expected 'class'");
    
    auto name = parseIdentifier();
    
    IdentifierPtr superClass = nullptr;
    if (match(KeywordType::Extends)) {
        superClass = parseIdentifier();
    }
    
    std::vector<IdentifierPtr> interfaces;
    if (match(KeywordType::Implements)) {
        do {
            interfaces.push_back(parseIdentifier());
        } while (match(TokenType::Comma));
    }
    
    expectToken(TokenType::LBrace, "Expected '{'");
    
    std::vector<AstNodePtr> members;
    while (!check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
        auto attrs = parseAttributes();
        
        if (check(KeywordType::Function)) {
            auto func = parseFunctionDefinitionWithAttributes(std::move(attrs));
            if (func) {
                members.push_back(std::move(func));
            }
        } else if (check(KeywordType::Var) || check(KeywordType::Const) || check(KeywordType::Let)) {
            auto var = parseVariableStatementWithAttributes(std::move(attrs));
            if (var) {
                members.push_back(std::move(var));
            }
        } else if (check(KeywordType::Get) || check(KeywordType::Set)) {
            auto prop = parsePropertyWithAttributes(std::move(attrs));
            if (prop) {
                members.push_back(std::move(prop));
            }
        } else {
            addError("Unexpected token in class body");
            advance();
        }
    }
    
    expectToken(TokenType::RBrace, "Expected '}'");
    
    return std::make_unique<ClassDefinition>(
        std::vector<AttributePtr>(),
        std::move(name),
        std::move(superClass),
        std::move(interfaces),
        std::move(members)
    );
}

std::unique_ptr<FunctionDefinition> Parser::parseFunctionDefinitionWithAttributes(std::vector<AttributePtr> attrs) {
    expectKeyword(KeywordType::Function, "Expected 'function'");
    
    bool isGetter = false;
    bool isSetter = false;
    if (check(KeywordType::Get)) {
        isGetter = true;
        advance();
    } else if (check(KeywordType::Set)) {
        isSetter = true;
        advance();
    }
    
    auto name = parseIdentifier();
    expectToken(TokenType::LParen, "Expected '('");
    
    auto params = parseParameters();
    
    IdentifierPtr returnType = nullptr;
    if (match(TokenType::Colon)) {
        returnType = parseIdentifier();
    }
    
    auto body = parseBlockStatement();
    
    return std::make_unique<FunctionDefinition>(
        std::move(attrs),
        std::move(name),
        std::move(params),
        std::move(returnType),
        std::move(body),
        isGetter,
        isSetter
    );
}

std::unique_ptr<Statement> Parser::parseVariableStatementWithAttributes(std::vector<AttributePtr> attrs) {
    bool isConst = false;
    if (check(KeywordType::Const)) {
        isConst = true;
        advance();
    } else if (match(KeywordType::Var)) {
        // var keyword matched
    } else if (match(KeywordType::Let)) {
        // let keyword matched
    }

    VariableDeclarationList declarations;

    do {
        auto id = parseIdentifier();
        IdentifierPtr type = nullptr;

        if (match(TokenType::Colon)) {
            type = parseIdentifier();
        }

        ExpressionPtr init = nullptr;

        if (match(TokenType::Assign)) {
            init = parseAssignmentExpression();
        }

        declarations.push_back(std::make_unique<VariableDeclaration>(std::move(id), std::move(type), std::move(init)));
    } while (match(TokenType::Comma));

    endStatement("Expected ';'");

    return std::make_unique<VariableDefinition>(std::move(attrs), std::move(declarations), isConst);
}

std::unique_ptr<AstNode> Parser::parsePropertyWithAttributes(std::vector<AttributePtr> attrs) {
    bool isGetter = match(KeywordType::Get);
    bool isSetter = !isGetter && match(KeywordType::Set);
    
    auto name = parseIdentifier();
    expectToken(TokenType::LParen, "Expected '('");
    expectToken(TokenType::RParen, "Expected ')'");
    
    auto body = parseBlockStatement();
    
    return std::make_unique<FunctionDefinition>(
        std::move(attrs),
        std::move(name),
        std::vector<std::unique_ptr<Parameter>>(),
        nullptr,
        std::move(body),
        isGetter,
        isSetter
    );
}

std::vector<std::unique_ptr<Parameter>> Parser::parseParameters() {
    std::vector<std::unique_ptr<Parameter>> params;
    
    while (!check(TokenType::RParen) && !check(TokenType::EndOfFile)) {
        params.push_back(parseParameter());
        if (!match(TokenType::Comma)) break;
    }
    
    expectToken(TokenType::RParen, "Expected ')'");
    
    return params;
}

std::unique_ptr<Parameter> Parser::parseParameter() {
    bool rest = false;
    if (check(TokenType::DotDotDot)) {
        rest = true;
        advance();
    }
    
    auto name = parseIdentifier();
    IdentifierPtr type = nullptr;
    
    if (match(TokenType::Colon)) {
        type = parseIdentifier();
    }
    
    ExpressionPtr defaultValue = nullptr;
    if (match(TokenType::Assign)) {
        defaultValue = parseAssignmentExpression();
    }
    
    return std::make_unique<Parameter>(std::move(name), std::move(type), std::move(defaultValue), rest);
}

OperatorType Parser::getBinaryOperator(TokenType type) const {
    switch (type) {
        case TokenType::Plus: return OperatorType::Add;
        case TokenType::Minus: return OperatorType::Subtract;
        case TokenType::Multiply: return OperatorType::Multiply;
        case TokenType::Divide: return OperatorType::Divide;
        case TokenType::Remainder: return OperatorType::Remainder;
        case TokenType::LeftShift: return OperatorType::ShiftLeft;
        case TokenType::RightShift: return OperatorType::ShiftRight;
        case TokenType::UnsignedRightShift: return OperatorType::ShiftRightUnsigned;
        case TokenType::LessThan: return OperatorType::Lt;
        case TokenType::GreaterThan: return OperatorType::Gt;
        case TokenType::LessThanOrEqual: return OperatorType::Le;
        case TokenType::GreaterThanOrEqual: return OperatorType::Ge;
        case TokenType::Equals: return OperatorType::Equals;
        case TokenType::NotEquals: return OperatorType::NotEquals;
        case TokenType::StrictEquals: return OperatorType::StrictEquals;
        case TokenType::StrictNotEquals: return OperatorType::StrictNotEquals;
        case TokenType::Ampersand: return OperatorType::BitwiseAnd;
        case TokenType::Caret: return OperatorType::BitwiseXor;
        case TokenType::Pipe: return OperatorType::BitwiseOr;
        case TokenType::LogicalAnd: return OperatorType::LogicalAnd;
        case TokenType::LogicalOr: return OperatorType::LogicalOr;
        case TokenType::LogicalXor: return OperatorType::LogicalXor;
        case TokenType::Power: return OperatorType::Power;
        case TokenType::NullCoalescing: return OperatorType::NullCoalescing;
        case TokenType::Assign: return OperatorType::Add;  // Assignment uses Add for simple =
        case TokenType::PlusAssign: return OperatorType::Add;
        case TokenType::MinusAssign: return OperatorType::Subtract;
        case TokenType::MultiplyAssign: return OperatorType::Multiply;
        case TokenType::DivideAssign: return OperatorType::Divide;
        case TokenType::RemainderAssign: return OperatorType::Remainder;
        case TokenType::LeftShiftAssign: return OperatorType::ShiftLeft;
        case TokenType::RightShiftAssign: return OperatorType::ShiftRight;
        case TokenType::UnsignedRightShiftAssign: return OperatorType::ShiftRightUnsigned;
        case TokenType::AmpersandAssign: return OperatorType::BitwiseAnd;
        case TokenType::PipeAssign: return OperatorType::BitwiseOr;
        case TokenType::CaretAssign: return OperatorType::BitwiseXor;
        case TokenType::PowerAssign: return OperatorType::Power;
        case TokenType::NullCoalescingAssign: return OperatorType::NullCoalescing;
        case TokenType::AndAssign: return OperatorType::LogicalAnd;
        case TokenType::OrAssign: return OperatorType::LogicalOr;
        case TokenType::Increment: return OperatorType::PostIncrement;
        case TokenType::Decrement: return OperatorType::PostDecrement;
        case TokenType::PreIncrement: return OperatorType::PreIncrement;
        case TokenType::PreDecrement: return OperatorType::PreDecrement;
        case TokenType::Exclamation: return OperatorType::LogicalNot;
        case TokenType::Tilde: return OperatorType::BitwiseNot;
        default: return OperatorType::Add;
    }
}

} // namespace as3
