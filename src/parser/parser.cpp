#include "parser.h"
#include <sstream>
#include <iostream>

namespace as3 {

Parser::Parser(const std::string& source, const std::string& fileName)
    : _lexer(source)
    , _position(0)
    , _fileName(fileName) {
    _tokens = _lexer.tokenize();
    _eofToken.type = TokenType::EndOfFile;
    _eofToken.keyword = KeywordType::None;
    if (!_tokens.empty()) {
        _eofToken.start = _tokens.back().start;
    }
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

bool Parser::isNameToken(const Token& token) {
    if (token.type == TokenType::Identifier) {
        return true;
    }
    if (token.type != TokenType::Keyword) {
        return false;
    }
    switch (token.keyword) {
        case KeywordType::Get:
        case KeywordType::Set:
        case KeywordType::Each:
        case KeywordType::Of:
        case KeywordType::Namespace:
        case KeywordType::Dynamic:
        case KeywordType::Final:
        case KeywordType::Native:
        case KeywordType::Override:
        case KeywordType::Static:
        case KeywordType::Yield:
        case KeywordType::Async:
        case KeywordType::Await:
        case KeywordType::Enum:
        case KeywordType::Let:
            return true;
        default:
            return false;
    }
}

bool Parser::isPropertyNameToken(const Token& token) {
    return token.type == TokenType::Identifier || token.type == TokenType::Keyword ||
           token.type == TokenType::Boolean || token.type == TokenType::Null;
}

std::string Parser::tokenWord(const Token& token) {
    return token.text;
}

void Parser::addError(const std::string& message) {
    std::ostringstream oss;
    oss << "Parse error";
    if (!_fileName.empty()) {
        oss << " in " << _fileName;
    }
    oss << " at line " << currentToken().start.line
        << ", column " << currentToken().start.column << ": " << message;
    if (currentToken().type != TokenType::EndOfFile) {
        oss << " but found '" << currentToken().text << "'";
    } else {
        oss << " but found end of file";
    }
    _errors += oss.str() + "\n";
}

void Parser::addWarning(const std::string& message) {
    std::ostringstream oss;
    oss << "Warning";
    if (!_fileName.empty()) {
        oss << " in " << _fileName;
    }
    oss << " at line " << currentToken().start.line << ", column " << currentToken().start.column << ": "
        << message;
    _warnings += oss.str() + "\n";
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
            case KeywordType::For:
            case KeywordType::If:
            case KeywordType::While:
            case KeywordType::Return:
            case KeywordType::Try:
            case KeywordType::Import:
            case KeywordType::Package:
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
    if (!isNameToken(currentToken())) {
        addError("Expected identifier in qualified name");
        return "";
    }

    std::string name = tokenWord(currentToken());
    advance();

    while (check(TokenType::Dot) && isPropertyNameToken(peek(1))) {
        advance();
        name += "." + tokenWord(currentToken());
        advance();
    }

    return name;
}

std::string Parser::parseTypeName() {
    if (match(TokenType::Multiply)) {
        return "*";
    }
    if (match(KeywordType::Void)) {
        return "void";
    }
    if (!isNameToken(currentToken())) {
        addError("Expected a type");
        return "*";
    }
    std::string name = tokenWord(currentToken());
    advance();
    while (check(TokenType::Dot)) {
        if (peek(1).type == TokenType::LessThan) {
            advance();  // .
            advance();  // <
            name += ".<" + parseTypeName();
            while (match(TokenType::Comma)) {
                name += "," + parseTypeName();
            }
            expectTypeClose();
            name += ">";
            continue;
        }
        if (!isPropertyNameToken(peek(1))) {
            break;
        }
        advance();
        name += "." + tokenWord(currentToken());
        advance();
    }
    return name;
}

void Parser::expectTypeClose() {
    Token& token = _position < _tokens.size() ? _tokens[_position] : _eofToken;
    switch (token.type) {
        case TokenType::GreaterThan:
            advance();
            return;
        case TokenType::RightShift:
            token.type = TokenType::GreaterThan;
            token.text = ">";
            return;
        case TokenType::UnsignedRightShift:
            token.type = TokenType::RightShift;
            token.text = ">>";
            return;
        case TokenType::GreaterThanOrEqual:
            token.type = TokenType::Assign;
            token.text = "=";
            return;
        case TokenType::RightShiftAssign:
            token.type = TokenType::GreaterThanOrEqual;
            token.text = ">=";
            return;
        default:
            addError("Expected '>' after type arguments");
            return;
    }
}

IdentifierPtr Parser::parseTypeAnnotation() {
    return std::make_unique<Identifier>(parseTypeName());
}

// [Name] or [Name(...)] before a definition, rather than an array: followed
// by another tag or a definition.
bool Parser::looksLikeMetadata() const {
    if (!check(TokenType::LBracket) || peek(1).type != TokenType::Identifier) {
        return false;
    }
    int depth = 0;
    for (int i = 0;; ++i) {
        const Token& token = peek(i);
        if (token.type == TokenType::EndOfFile) {
            return false;
        }
        if (token.type == TokenType::LBracket || token.type == TokenType::LParen) {
            ++depth;
        } else if (token.type == TokenType::RBracket || token.type == TokenType::RParen) {
            if (--depth == 0) {
                if (token.type != TokenType::RBracket) {
                    return false;
                }
                const Token& next = peek(i + 1);
                if (next.type == TokenType::LBracket) {
                    return true;
                }
                if (next.type != TokenType::Keyword) {
                    return false;
                }
                switch (next.keyword) {
                    case KeywordType::Class: case KeywordType::Interface: case KeywordType::Public:
                    case KeywordType::Private: case KeywordType::Protected: case KeywordType::Internal:
                    case KeywordType::Final: case KeywordType::Dynamic: case KeywordType::Function:
                    case KeywordType::Var: case KeywordType::Const: case KeywordType::Static:
                    case KeywordType::Override:
                        return true;
                    default:
                        return false;
                }
            }
        }
    }
}

void Parser::parseMetadata() {
    expectToken(TokenType::LBracket, "Expected '['");
    Metadata tag;
    tag.name = parseQualifiedName();
    if (match(TokenType::LParen)) {
        while (!check(TokenType::RParen) && !check(TokenType::EndOfFile)) {
            std::string key;
            std::string value;
            if (isPropertyNameToken(currentToken()) && peek(1).type == TokenType::Assign) {
                key = tokenWord(currentToken());
                advance();
                advance();
            }
            const Token& token = currentToken();
            value = token.type == TokenType::String ? token.stringValue : token.text;
            if (token.type == TokenType::Number && value.empty()) {
                std::ostringstream number;
                number << token.numericValue;
                value = number.str();
            }
            advance();
            tag.arguments.emplace_back(key, value);
            if (!match(TokenType::Comma)) {
                break;
            }
        }
        expectToken(TokenType::RParen, "Expected ')' after metadata arguments");
    }
    expectToken(TokenType::RBracket, "Expected ']' after metadata");
    _pendingMetadata.push_back(std::move(tag));
}

std::vector<Metadata> Parser::takeMetadata() {
    std::vector<Metadata> taken;
    taken.swap(_pendingMetadata);
    return taken;
}

bool Parser::atConfigCondition() const {
    if (!check(TokenType::Identifier) || peek(1).type != TokenType::DoubleColon || !isPropertyNameToken(peek(2))) {
        return false;
    }
    const Token& next = peek(3);
    if (next.type == TokenType::LBrace) {
        return true;
    }
    if (next.type != TokenType::Keyword) {
        return false;
    }
    switch (next.keyword) {
        case KeywordType::Function: case KeywordType::Var: case KeywordType::Const: case KeywordType::Public:
        case KeywordType::Private: case KeywordType::Protected: case KeywordType::Internal:
        case KeywordType::Static: case KeywordType::Override: case KeywordType::Final: case KeywordType::Class:
            return true;
        default:
            return false;
    }
}

bool Parser::readConfigCondition() {
    const std::string name = tokenWord(currentToken()) + "::" + tokenWord(peek(2));
    advance();
    advance();
    advance();
    auto found = _constants.find(name);
    return found != _constants.end() && found->second == "true";
}

std::unique_ptr<Program> Parser::parse() {
    auto program = std::make_unique<Program>();
    program->fileName = _fileName;

    while (!check(TokenType::EndOfFile)) {
        const size_t before = _position;
        auto stmt = parseTopLevel();
        if (stmt) {
            program->statements.push_back(std::move(stmt));
        } else {
            synchronize();
            // Stray closing brackets at the top level are skipped.
            if (_position == before) {
                advance();
            }
        }
    }

    // What the lexer found wrong, as parse errors.
    if (!_lexer.getErrors().empty()) {
        std::istringstream lines(_lexer.getErrors());
        std::string line;
        while (std::getline(lines, line)) {
            const std::string at = "Error at line ";
            if (line.compare(0, at.size(), at) == 0) {
                const size_t colon = line.find(':');
                _errors += "Parse error" + (_fileName.empty() ? std::string() : " in " + _fileName) + " at line " +
                           line.substr(at.size(), colon - at.size()) + ", column 1:" + line.substr(colon + 1) +
                           "\n";
            }
        }
    }

    return program;
}

std::vector<std::unique_ptr<ImportDirective>> Parser::parseImports() {
    std::vector<std::unique_ptr<ImportDirective>> imports;

    while (match(KeywordType::Import)) {
        std::string path = isNameToken(currentToken()) ? tokenWord(currentToken()) : std::string();
        if (path.empty()) {
            addError("Expected identifier in import");
        } else {
            advance();
        }

        std::string name;
        bool isWildcard = false;
        while (match(TokenType::Dot)) {
            if (match(TokenType::Multiply)) {
                isWildcard = true;
                name = "*";
                break;
            }
            if (!isPropertyNameToken(currentToken())) {
                addError("Expected identifier or '*' after '.' in import");
                break;
            }
            path += "." + tokenWord(currentToken());
            advance();
        }

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

        endStatement("Expected ';' after import");

        auto id = std::make_unique<Identifier>(name);
        auto import = std::make_unique<ImportDirective>(path, std::move(id), isWildcard);
        imports.push_back(std::move(import));
    }

    return imports;
}

bool Parser::atAttribute() const {
    if (check(KeywordType::Public) || check(KeywordType::Private) || check(KeywordType::Protected) ||
        check(KeywordType::Internal) || check(KeywordType::Final) || check(KeywordType::Dynamic) ||
        check(KeywordType::Native) || check(KeywordType::Override)) {
        return true;
    }
    // static, unless a name (static = 1 in a function).
    if (check(KeywordType::Static)) {
        const Token& next = peek(1);
        return next.type == TokenType::Keyword || next.type == TokenType::Identifier;
    }
    // A namespace as an attribute: mx_internal function f().
    if (check(TokenType::Identifier)) {
        const Token& next = peek(1);
        if (next.type == TokenType::Keyword && !next.newLineBefore &&
            (next.keyword == KeywordType::Function || next.keyword == KeywordType::Var ||
             next.keyword == KeywordType::Const || next.keyword == KeywordType::Static ||
             next.keyword == KeywordType::Override)) {
            return true;
        }
    }
    return false;
}

std::unique_ptr<AstNode> Parser::parseTopLevel() {
    if (match(KeywordType::Package)) {
        return parsePackage();
    }

    if (atConfigCondition()) {
        const bool kept = readConfigCondition();
        auto node = parseTopLevel();
        if (!kept) {
            return std::make_unique<EmptyStatement>();
        }
        return node;
    }

    if (looksLikeMetadata()) {
        parseMetadata();
        return parseTopLevel();
    }

    if (atAttribute() || check(KeywordType::Class) || check(KeywordType::Interface)) {
        return parseDefinition(parseAttributes());
    }

    if (check(KeywordType::Import)) {
        auto imports = parseImports();
        if (!imports.empty()) {
            return std::move(imports[0]);
        }
    }

    return parseStatement();
}

std::unique_ptr<AstNode> Parser::parseDefinition(std::vector<AttributePtr> attrs) {
    const int line = static_cast<int>(currentToken().start.line);
    if (check(KeywordType::Class) || check(KeywordType::Interface)) {
        const bool isInterface = check(KeywordType::Interface);
        auto definition = parseClassDeclaration(std::move(attrs), isInterface);
        if (definition) {
            definition->line = line;
        }
        return definition;
    }
    if (check(KeywordType::Function)) {
        auto function = parseFunctionDefinitionWithAttributes(std::move(attrs));
        if (function) {
            function->line = line;
        }
        return function;
    }
    if (check(KeywordType::Var) || check(KeywordType::Const) || check(KeywordType::Let)) {
        auto variables = parseVariableStatement(std::move(attrs));
        if (variables) {
            variables->line = line;
        }
        return variables;
    }
    if (check(KeywordType::Namespace)) {
        return parseNamespaceDeclaration();
    }
    addError("Expected a class, function or variable after its attributes");
    return nullptr;
}

std::unique_ptr<PackageDefinition> Parser::parsePackage() {
    // The top-level package has no name: package { ... }.
    std::string packageName = check(TokenType::LBrace) ? std::string() : parseQualifiedName();
    auto name = std::make_unique<Identifier>(packageName);

    expectToken(TokenType::LBrace, "Expected '{' after package name");

    auto package = std::make_unique<PackageDefinition>(std::move(name), parseImports(), std::vector<AstNodePtr>());

    while (!check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
        if (check(KeywordType::Import)) {
            for (auto& import : parseImports()) {
                package->imports.push_back(std::move(import));
            }
            continue;
        }
        const size_t before = _position;
        auto member = parseTopLevel();
        if (member) {
            package->members.push_back(std::move(member));
        } else {
            synchronize();
            if (_position == before) {
                advance();
            }
        }
    }

    expectToken(TokenType::RBrace, "Expected '}' after package body");

    return package;
}

namespace {

// Counts a level of nesting while it lasts.
struct Nesting {
    int& depth;
    ~Nesting() { --depth; }
};

constexpr int kMaxNesting = 400;

} // namespace

bool Parser::enterNesting() {
    if (++_nesting > kMaxNesting) {
        if (_nesting == kMaxNesting + 1) {
            addError("Nested too deeply");
        }
        // Skips the rest, so that the error is not made again and again.
        _position = _tokens.size();
        return false;
    }
    return true;
}

std::unique_ptr<Statement> Parser::parseStatement() {
    Nesting nesting{_nesting};
    if (!enterNesting()) {
        return nullptr;
    }
    const int line = static_cast<int>(currentToken().start.line);
    auto statement = parseStatementBody();
    if (statement && statement->line == 0) {
        statement->line = line;
    }
    return statement;
}

std::unique_ptr<Statement> Parser::parseStatementBody() {
    if (atConfigCondition()) {
        const bool kept = readConfigCondition();
        auto statement = parseStatement();
        if (!kept) {
            return std::make_unique<EmptyStatement>();
        }
        return statement;
    }
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
    if (check(KeywordType::Var) || check(KeywordType::Const) ||
        (check(KeywordType::Let) && isNameToken(peek(1)))) {
        return parseVariableStatement();
    }
    if (check(KeywordType::Function) && isNameToken(peek(1))) {
        return parseFunctionDefinitionWithAttributes({});
    }
    if (check(KeywordType::Class) || check(KeywordType::Interface)) {
        return parseClassDeclaration({}, check(KeywordType::Interface));
    }
    if (check(KeywordType::Namespace) && isNameToken(peek(1))) {
        return parseNamespaceDeclaration();
    }
    if (check(KeywordType::Import)) {
        // An import in a function or a class: names only, nothing to run.
        auto imports = parseImports();
        return std::make_unique<EmptyStatement>();
    }
    if (check(KeywordType::Use) || (check(TokenType::Identifier) && currentToken().text == "include" &&
                                    peek(1).type == TokenType::String)) {
        return parseUseOrInclude();
    }
    if (check(TokenType::SemiColon)) {
        return parseEmptyStatement();
    }
    if (isNameToken(currentToken()) && peek(1).type == TokenType::Colon) {
        return parseLabeledStatement();
    }
    if (check(TokenType::Identifier)) {
        if (auto legacy = parseLegacyBlock()) {
            return legacy;
        }
    }

    return parseExpressionStatement();
}

// use namespace x; and include "file.as"; are read and left out.
std::unique_ptr<Statement> Parser::parseUseOrInclude() {
    if (match(KeywordType::Use)) {
        match(KeywordType::Namespace);
        parseQualifiedName();
        while (match(TokenType::Comma)) {
            parseQualifiedName();
        }
    } else {
        advance();  // include
        const std::string path = currentToken().stringValue;
        advance();
        endStatement("Expected ';'");
        if (!_includeHandler) {
            addWarning("include \"" + path + "\" was left out: there is nothing to read it with");
            return std::make_unique<EmptyStatement>();
        }
        std::optional<std::string> source = _includeHandler(path);
        if (!source) {
            addError("The file \"" + path + "\" to include was not found");
            return std::make_unique<EmptyStatement>();
        }
        if (++_includeDepth > 32) {
            addError("Too many nested includes (\"" + path + "\")");
            return std::make_unique<EmptyStatement>();
        }
        // Its tokens read next, as if written here.
        Lexer lexer(*source);
        std::vector<Token> included = lexer.tokenize();
        if (!included.empty() && included.back().type == TokenType::EndOfFile) {
            included.pop_back();
        }
        _tokens.insert(_tokens.begin() + static_cast<long>(_position), included.begin(), included.end());
        return std::make_unique<EmptyStatement>();
    }
    endStatement("Expected ';'");
    return std::make_unique<EmptyStatement>();
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
        const size_t before = _position;
        auto stmt = parseStatement();
        if (stmt) {
            statements.push_back(std::move(stmt));
        } else {
            synchronize();
            if (_position == before && !check(TokenType::RBrace)) {
                advance();
            }
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
    // Something more on the line, with nothing between: as a.b 0.07; where
    // an = was left out. Taken as two statements, with a warning, unless
    // strict.
    if (!_strict && !check(TokenType::SemiColon) && !check(TokenType::RBrace) && !check(TokenType::EndOfFile) &&
        !currentToken().newLineBefore && !check(TokenType::Error)) {
        addWarning("Expected ';' after expression; what follows is taken as another statement");
        return std::make_unique<ExpressionStatement>(std::move(expr));
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
        ExpressionPtr caseExpr;
        const int line = static_cast<int>(currentToken().start.line);
        if (match(KeywordType::Default)) {
        } else if (match(KeywordType::Case)) {
            caseExpr = parseExpression();
            if (!caseExpr) {
                addError("Failed to parse case expression");
                synchronize();
                continue;
            }
        } else {
            addError("Expected 'case' or 'default' in switch body");
            synchronize();
            break;
        }
        expectToken(TokenType::Colon, "Expected ':'");
        std::vector<StatementPtr> consequent;
        while (!check(KeywordType::Case) && !check(KeywordType::Default) &&
               !check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
            const size_t before = _position;
            auto stmt = parseStatement();
            if (stmt) {
                consequent.push_back(std::move(stmt));
            } else {
                synchronize();
                if (_position == before) {
                    advance();
                }
            }
        }
        auto clause = std::make_unique<CaseClause>(std::move(caseExpr), std::move(consequent));
        clause->line = line;
        cases.push_back(std::move(clause));
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
    // The semicolon after do-while's ) may be left out, even on one line.
    match(TokenType::SemiColon);

    return std::make_unique<DoWhileStatement>(std::move(body), std::move(test));
}

std::unique_ptr<Statement> Parser::parseForStatement() {
    expectKeyword(KeywordType::For, "Expected 'for'");

    const bool isForEach = match(KeywordType::Each);

    expectToken(TokenType::LParen, "Expected '(' after 'for'");

    StatementPtr init = nullptr;
    const int line = static_cast<int>(currentToken().start.line);
    if (check(KeywordType::Var) || check(KeywordType::Const) || check(KeywordType::Let)) {
        const bool isConst = check(KeywordType::Const);
        advance();
        // var name[:Type] in ..., or the first of a list of declarations.
        auto id = parseIdentifier();
        IdentifierPtr type = nullptr;
        if (match(TokenType::Colon)) {
            type = parseTypeAnnotation();
        }
        if (check(KeywordType::In) || check(KeywordType::Of)) {
            VariableDeclarationList decls;
            decls.push_back(std::make_unique<VariableDeclaration>(std::move(id), std::move(type), nullptr));
            init = std::make_unique<VariableDefinition>(std::vector<AttributePtr>(), std::move(decls), isConst);
        } else {
            VariableDeclarationList decls;
            const bool savedNoIn = _noIn;
            _noIn = true;
            ExpressionPtr first;
            if (match(TokenType::Assign)) {
                first = parseAssignmentExpression();
            }
            decls.push_back(std::make_unique<VariableDeclaration>(std::move(id), std::move(type), std::move(first)));
            while (match(TokenType::Comma)) {
                auto nextId = parseIdentifier();
                IdentifierPtr nextType = nullptr;
                if (match(TokenType::Colon)) {
                    nextType = parseTypeAnnotation();
                }
                ExpressionPtr nextInit;
                if (match(TokenType::Assign)) {
                    nextInit = parseAssignmentExpression();
                }
                decls.push_back(
                    std::make_unique<VariableDeclaration>(std::move(nextId), std::move(nextType), std::move(nextInit)));
            }
            _noIn = savedNoIn;
            init = std::make_unique<VariableDefinition>(std::vector<AttributePtr>(), std::move(decls), isConst);
        }
        init->line = line;
    } else if (!check(TokenType::SemiColon)) {
        const bool savedNoIn = _noIn;
        _noIn = true;
        auto expr = parseExpression();
        _noIn = savedNoIn;
        if (expr) {
            init = std::make_unique<ExpressionStatement>(std::move(expr));
            init->line = line;
        }
    }

    if (match(KeywordType::In) || match(KeywordType::Of)) {
        auto right = parseExpression();
        expectToken(TokenType::RParen, "Expected ')'");
        auto body = parseStatement();
        if (!body) {
            return nullptr;
        }
        return std::make_unique<ForInStatement>(std::move(init), std::move(right), std::move(body), isForEach);
    }
    if (isForEach) {
        addError("Expected 'in' in for each");
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

std::unique_ptr<Statement> Parser::parseBreakStatement() {
    expectKeyword(KeywordType::Break, "Expected 'break'");
    IdentifierPtr label = nullptr;
    if (isNameToken(currentToken()) && !currentToken().newLineBefore) {
        label = parseIdentifier();
    }
    endStatement("Expected ';'");
    return std::make_unique<BreakStatement>(std::move(label));
}

std::unique_ptr<Statement> Parser::parseContinueStatement() {
    expectKeyword(KeywordType::Continue, "Expected 'continue'");
    IdentifierPtr label = nullptr;
    if (isNameToken(currentToken()) && !currentToken().newLineBefore) {
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

    std::vector<CatchClause> catches;
    while (match(KeywordType::Catch)) {
        expectToken(TokenType::LParen, "Expected '(' after 'catch'");
        CatchClause clause;
        clause.variable = parseIdentifier();
        if (match(TokenType::Colon)) {
            clause.type = parseTypeName();
        }
        expectToken(TokenType::RParen, "Expected ')'");
        clause.body = parseBlockStatement();
        catches.push_back(std::move(clause));
    }

    StatementPtr finallyBody = nullptr;
    if (match(KeywordType::Finally)) {
        finallyBody = parseBlockStatement();
    }
    if (catches.empty() && !finallyBody) {
        addError("Expected 'catch' or 'finally' after try block");
    }

    return std::make_unique<TryStatement>(std::move(block), std::move(catches), std::move(finallyBody));
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

std::unique_ptr<VariableDefinition> Parser::parseVariableList(std::vector<AttributePtr> attrs, bool isConst) {
    VariableDeclarationList declarations;

    do {
        auto id = parseIdentifier();
        IdentifierPtr type = nullptr;

        if (match(TokenType::Colon)) {
            type = parseTypeAnnotation();
        }

        ExpressionPtr init = nullptr;

        if (match(TokenType::Assign)) {
            init = parseAssignmentExpression();
        }

        declarations.push_back(std::make_unique<VariableDeclaration>(std::move(id), std::move(type), std::move(init)));
    } while (match(TokenType::Comma));

    auto definition = std::make_unique<VariableDefinition>(std::move(attrs), std::move(declarations), isConst);
    definition->metadata = takeMetadata();
    return definition;
}

std::unique_ptr<Statement> Parser::parseVariableStatement(std::vector<AttributePtr> attrs) {
    const bool isConst = check(KeywordType::Const);
    advance();  // var, const or let
    auto definition = parseVariableList(std::move(attrs), isConst);
    endStatement("Expected ';'");
    return definition;
}

std::unique_ptr<Statement> Parser::parseClassDeclaration(std::vector<AttributePtr> attrs, bool isInterface) {
    advance();  // class or interface
    std::vector<Metadata> metadata = takeMetadata();

    auto name = parseIdentifier();

    IdentifierPtr superClass = nullptr;
    std::vector<IdentifierPtr> interfaces;
    if (match(KeywordType::Extends)) {
        if (isInterface) {
            do {
                interfaces.push_back(std::make_unique<Identifier>(parseTypeName()));
            } while (match(TokenType::Comma));
        } else {
            superClass = std::make_unique<Identifier>(parseTypeName());
        }
    }

    if (match(KeywordType::Implements)) {
        do {
            interfaces.push_back(std::make_unique<Identifier>(parseTypeName()));
        } while (match(TokenType::Comma));
    }

    expectToken(TokenType::LBrace, "Expected '{'");

    std::vector<AstNodePtr> members;
    while (!check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
        const size_t before = _position;
        const int line = static_cast<int>(currentToken().start.line);
        if (check(TokenType::LBracket)) {
            // In a class body a [ only starts metadata.
            parseMetadata();
            continue;
        }
        if (match(TokenType::SemiColon)) {
            continue;
        }
        if (check(KeywordType::Import)) {
            parseImports();
            continue;
        }
        if (check(KeywordType::Use) ||
            (check(TokenType::Identifier) && currentToken().text == "include" && peek(1).type == TokenType::String)) {
            parseUseOrInclude();
            continue;
        }
        // CONFIG::debug before a member: kept or left out.
        bool keepMember = true;
        if (atConfigCondition()) {
            keepMember = readConfigCondition();
            if (check(TokenType::LBrace)) {
                auto block = parseBlockStatement();
                if (keepMember && block) members.push_back(std::move(block));
                continue;
            }
        }
        const size_t membersBefore = members.size();
        auto attrs = parseAttributes();

        if (check(KeywordType::Function)) {
            auto func = parseFunctionDefinitionWithAttributes(std::move(attrs));
            if (func) {
                func->line = line;
                members.push_back(std::move(func));
            }
        } else if (check(KeywordType::Var) || check(KeywordType::Const) || check(KeywordType::Let)) {
            auto var = parseVariableStatement(std::move(attrs));
            if (var) {
                var->line = line;
                members.push_back(std::move(var));
            }
        } else if (check(KeywordType::Namespace)) {
            parseNamespaceDeclaration();
        } else if (attrs.empty()) {
            // A statement in the class body runs when the class is defined.
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
        if (!keepMember && members.size() > membersBefore) {
            members.resize(membersBefore);
        }
        if (_position == before) {
            advance();
        }
    }

    expectToken(TokenType::RBrace, "Expected '}'");

    auto definition = std::make_unique<ClassDefinition>(std::move(attrs), std::move(name), std::move(superClass),
                                                        std::move(interfaces), std::move(members));
    definition->isInterface = isInterface;
    definition->metadata = std::move(metadata);
    return definition;
}

std::unique_ptr<Statement> Parser::parseNamespaceDeclaration() {
    expectKeyword(KeywordType::Namespace, "Expected 'namespace'");

    auto name = parseIdentifier();

    IdentifierPtr uri = nullptr;
    if (match(TokenType::Assign)) {
        if (check(TokenType::String)) {
            uri = std::make_unique<Identifier>(currentToken().stringValue);
            advance();
        } else {
            addError("Expected string literal for namespace URI");
            return nullptr;
        }
    }

    endStatement("Expected ';' after namespace declaration");

    return std::make_unique<NamespaceDefinition>(std::move(name), std::move(uri));
}

std::unique_ptr<Expression> Parser::parseExpression() {
    const int line = static_cast<int>(currentToken().start.line);
    auto first = parseAssignmentExpression();
    if (!first || !check(TokenType::Comma)) {
        return first;
    }
    ExpressionList list;
    list.push_back(std::move(first));
    while (match(TokenType::Comma)) {
        auto next = parseAssignmentExpression();
        if (!next) {
            return nullptr;
        }
        list.push_back(std::move(next));
    }
    auto sequence = std::make_unique<SequenceExpression>(std::move(list));
    sequence->line = line;
    return sequence;
}

std::unique_ptr<Expression> Parser::parseAssignmentExpression() {
    Nesting nesting{_nesting};
    if (!enterNesting()) {
        return nullptr;
    }
    const int line = static_cast<int>(currentToken().start.line);
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

        auto assignment = std::make_unique<AssignmentExpression>(getBinaryOperator(op), std::move(left), std::move(right));
        assignment->line = line;
        return assignment;
    }

    return left;
}

std::unique_ptr<Expression> Parser::parseConditionalExpression() {
    const int line = static_cast<int>(currentToken().start.line);
    auto test = parseBinaryExpression(0);
    if (!test) {
        return nullptr;
    }

    if (match(TokenType::Question)) {
        const bool savedNoIn = _noIn;
        _noIn = false;
        auto consequent = parseAssignmentExpression();
        _noIn = savedNoIn;
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

        auto conditional = std::make_unique<ConditionalExpression>(std::move(test), std::move(consequent),
                                                                   std::move(alternate));
        conditional->line = line;
        return conditional;
    }

    return test;
}

namespace {

// The binary operators by precedence, loosest first.
enum BinaryLevel {
    kCoalesce, kLogicalOr, kLogicalXor, kLogicalAnd, kBitOr, kBitXor, kBitAnd, kEquality, kRelational, kShift,
    kAdditive, kMultiplicative, kLevels
};

} // namespace

// A binary operator here, and its level; -1 when there is none.
int Parser::binaryLevel(OperatorType& op) const {
    const Token& token = currentToken();
    switch (token.type) {
        case TokenType::NullCoalescing: op = OperatorType::NullCoalescing; return kCoalesce;
        case TokenType::LogicalOr: op = OperatorType::LogicalOr; return kLogicalOr;
        case TokenType::LogicalXor: op = OperatorType::LogicalXor; return kLogicalXor;
        case TokenType::LogicalAnd: op = OperatorType::LogicalAnd; return kLogicalAnd;
        case TokenType::Pipe: op = OperatorType::BitwiseOr; return kBitOr;
        case TokenType::Caret: op = OperatorType::BitwiseXor; return kBitXor;
        case TokenType::Ampersand: op = OperatorType::BitwiseAnd; return kBitAnd;
        case TokenType::Equals:
        case TokenType::NotEquals:
        case TokenType::StrictEquals:
        case TokenType::StrictNotEquals:
            op = getBinaryOperator(token.type);
            return kEquality;
        case TokenType::LessThan:
        case TokenType::GreaterThan:
        case TokenType::LessThanOrEqual:
        case TokenType::GreaterThanOrEqual:
            op = getBinaryOperator(token.type);
            return kRelational;
        case TokenType::LeftShift:
        case TokenType::RightShift:
        case TokenType::UnsignedRightShift:
            op = getBinaryOperator(token.type);
            return kShift;
        case TokenType::Plus:
        case TokenType::Minus:
            op = getBinaryOperator(token.type);
            return kAdditive;
        case TokenType::Multiply:
        case TokenType::Divide:
        case TokenType::Remainder:
            op = getBinaryOperator(token.type);
            return kMultiplicative;
        case TokenType::Keyword:
            switch (token.keyword) {
                case KeywordType::Instanceof: op = OperatorType::Instanceof; return kRelational;
                case KeywordType::In:
                    if (_noIn) return -1;
                    op = OperatorType::In;
                    return kRelational;
                case KeywordType::Is: op = OperatorType::Is; return kRelational;
                case KeywordType::As: op = OperatorType::As; return kRelational;
                default: return -1;
            }
        default:
            return -1;
    }
}

// Binary operators of a level or tighter, by precedence climbing: a chain
// of one level is read in a loop, and only a tighter right side recurses.
std::unique_ptr<Expression> Parser::parseBinaryExpression(int minimum) {
    const int line = static_cast<int>(currentToken().start.line);
    auto left = parseUnaryExpression();
    if (!left) {
        return nullptr;
    }
    for (;;) {
        OperatorType op = OperatorType::Add;
        const int level = binaryLevel(op);
        if (level < 0 || level < minimum) {
            return left;
        }
        advance();
        // ?? groups to the right, as a ?? (b ?? c).
        auto right = parseBinaryExpression(op == OperatorType::NullCoalescing ? level : level + 1);
        if (!right) {
            addError("Failed to parse right operand of binary expression");
            return nullptr;
        }
        left = std::make_unique<BinaryExpression>(op, std::move(left), std::move(right));
        left->line = line;
    }
}

std::unique_ptr<Expression> Parser::parseUnaryExpression() {
    Nesting nesting{_nesting};
    if (!enterNesting()) {
        return nullptr;
    }
    const int line = static_cast<int>(currentToken().start.line);
    // ++x and --x: the lexer makes the same token as for x++.
    if (check(TokenType::Increment) || check(TokenType::Decrement)) {
        const bool increment = check(TokenType::Increment);
        advance();
        auto operand = parseUnaryExpression();
        if (!operand) {
            addError("Failed to parse operand of ++ or --");
            return nullptr;
        }
        auto unary = std::make_unique<UnaryExpression>(
            increment ? OperatorType::PreIncrement : OperatorType::PreDecrement, std::move(operand), false);
        unary->line = line;
        return unary;
    }

    OperatorType op;
    bool found = true;
    if (check(TokenType::Plus)) {
        op = OperatorType::Positive;
    } else if (check(TokenType::Minus)) {
        op = OperatorType::Negative;
    } else if (check(TokenType::Tilde)) {
        op = OperatorType::BitwiseNot;
    } else if (check(TokenType::Exclamation)) {
        op = OperatorType::LogicalNot;
    } else if (check(KeywordType::Delete)) {
        op = OperatorType::Delete;
    } else if (check(KeywordType::Void)) {
        op = OperatorType::Void;
    } else if (check(KeywordType::Typeof)) {
        op = OperatorType::Typeof;
    } else {
        found = false;
        op = OperatorType::Add;
    }
    if (found) {
        advance();
        auto operand = parseUnaryExpression();
        if (!operand) {
            addError("Failed to parse operand of unary expression");
            return nullptr;
        }
        // -5 is a number, as a default value or a case wants.
        if (op == OperatorType::Negative && operand->kind == Expression::ExpressionKind::Literal) {
            auto* literal = static_cast<Literal*>(operand.get());
            if (literal->kind == Literal::LiteralKind::Numeric) {
                literal->numberValue = -literal->numberValue;
                return operand;
            }
        }
        auto unary = std::make_unique<UnaryExpression>(op, std::move(operand), false);
        unary->line = line;
        return unary;
    }

    return parsePostfixExpression();
}

std::unique_ptr<Expression> Parser::parsePostfixExpression() {
    const int line = static_cast<int>(currentToken().start.line);
    auto expr = parseCallMemberExpression();
    if (!expr) {
        return nullptr;
    }

    // x++ on one line; a ++ on the next line is the next statement's.
    if ((check(TokenType::Increment) || check(TokenType::Decrement)) && !currentToken().newLineBefore) {
        const bool increment = check(TokenType::Increment);
        advance();
        auto unary = std::make_unique<UnaryExpression>(
            increment ? OperatorType::PostIncrement : OperatorType::PostDecrement, std::move(expr), true);
        unary->line = line;
        return unary;
    }

    return expr;
}

ExpressionList Parser::parseArguments() {
    ExpressionList args;
    const bool savedNoIn = _noIn;
    _noIn = false;
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
    _noIn = savedNoIn;
    expectToken(TokenType::RParen, "Expected ')'");
    return args;
}

std::vector<std::string> Parser::parseTypeArguments() {
    // After .<
    std::vector<std::string> types;
    types.push_back(parseTypeName());
    while (match(TokenType::Comma)) {
        types.push_back(parseTypeName());
    }
    expectTypeClose();
    return types;
}

std::unique_ptr<Expression> Parser::parseCallMemberExpression() {
    const int line = static_cast<int>(currentToken().start.line);
    // new X(...) may be followed by a member or a call too: new Array().length.
    auto expr = check(KeywordType::New) ? parseNewExpression() : parsePrimaryExpression();
    if (!expr) {
        return nullptr;
    }

    for (;;) {
        if (match(TokenType::LParen)) {
            auto call = std::make_unique<CallExpression>(std::move(expr), parseArguments());
            call->line = line;
            expr = std::move(call);
        } else if (check(TokenType::Dot) && peek(1).type == TokenType::LessThan) {
            advance();
            advance();
            auto applied = std::make_unique<TypeApplication>(std::move(expr), parseTypeArguments());
            applied->line = line;
            expr = std::move(applied);
        } else if (check(TokenType::Dot) || check(TokenType::QuestionDot)) {
            const bool optional = check(TokenType::QuestionDot);
            advance();
            if (check(TokenType::At) || check(TokenType::Dot)) {
                addError("E4X (XML) expressions are not supported");
                return nullptr;
            }
            if (!isPropertyNameToken(currentToken())) {
                addError("Expected a property name after '.'");
                return nullptr;
            }
            auto property = std::make_unique<Identifier>(tokenWord(currentToken()));
            advance();
            // ns::name after a dot: the name.
            if (check(TokenType::DoubleColon) && isPropertyNameToken(peek(1))) {
                advance();
                property = std::make_unique<Identifier>(tokenWord(currentToken()));
                advance();
            }
            auto member = std::make_unique<MemberExpression>(std::move(expr), std::move(property), false, optional);
            member->line = line;
            expr = std::move(member);
        } else if (check(TokenType::LBracket)) {
            advance();
            const bool savedNoIn = _noIn;
            _noIn = false;
            auto index = parseExpression();
            _noIn = savedNoIn;
            if (!index) {
                addError("Failed to parse computed property expression");
                return nullptr;
            }
            expectToken(TokenType::RBracket, "Expected ']'");
            auto member = std::make_unique<MemberExpression>(std::move(expr), std::move(index), false);
            member->line = line;
            expr = std::move(member);
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<Expression> Parser::parseNewExpression() {
    const int line = static_cast<int>(currentToken().start.line);
    expectKeyword(KeywordType::New, "Expected 'new'");

    // new <int>[1, 2, 3]: a vector of the elements.
    if (check(TokenType::LessThan)) {
        advance();
        const std::string type = parseTypeName();
        expectTypeClose();
        auto literal = parsePrimaryExpression();
        if (!literal || literal->kind != Expression::ExpressionKind::Array) {
            addError("Expected '[' after new <Type>");
            return nullptr;
        }
        static_cast<ArrayExpression*>(literal.get())->vectorType = type;
        return literal;
    }

    // What is made: a name or a member, not a call (new a.b.C(), new
    // Vector.<int>(), new (getClass())()).
    auto callee = check(KeywordType::New) ? parseNewExpression() : parsePrimaryExpression();
    if (!callee) {
        addError("Failed to parse new expression callee");
        return nullptr;
    }
    for (;;) {
        if (check(TokenType::Dot) && peek(1).type == TokenType::LessThan) {
            advance();
            advance();
            callee = std::make_unique<TypeApplication>(std::move(callee), parseTypeArguments());
        } else if (check(TokenType::Dot) && isPropertyNameToken(peek(1))) {
            advance();
            auto property = std::make_unique<Identifier>(tokenWord(currentToken()));
            advance();
            callee = std::make_unique<MemberExpression>(std::move(callee), std::move(property), false, false);
        } else if (check(TokenType::LBracket)) {
            advance();
            auto index = parseExpression();
            expectToken(TokenType::RBracket, "Expected ']'");
            callee = std::make_unique<MemberExpression>(std::move(callee), std::move(index), false);
        } else {
            break;
        }
    }

    ExpressionList args;
    if (match(TokenType::LParen)) {
        args = parseArguments();
    }

    auto made = std::make_unique<NewExpression>(std::move(callee), std::move(args));
    made->line = line;
    return made;
}

std::unique_ptr<Expression> Parser::parseFunctionExpression() {
    const int line = static_cast<int>(currentToken().start.line);
    advance();  // function
    auto name = isNameToken(currentToken()) ? parseIdentifier() : nullptr;
    expectToken(TokenType::LParen, "Expected '('");
    auto params = parseParameters();
    IdentifierPtr returnType = nullptr;
    if (match(TokenType::Colon)) {
        returnType = parseTypeAnnotation();
    }
    const bool savedNoIn = _noIn;
    _noIn = false;
    auto body = parseBlockStatement();
    _noIn = savedNoIn;

    auto function = std::make_unique<FunctionExpression>(std::move(name), std::move(params), std::move(returnType),
                                                         std::move(body));
    function->line = line;
    return function;
}

std::unique_ptr<Expression> Parser::parsePrimaryExpression() {
    const int line = static_cast<int>(currentToken().start.line);
    if (isNameToken(currentToken())) {
        auto id = parseIdentifier();
        // ns::name: the name; CONFIG::debug, a constant: its value.
        if (check(TokenType::DoubleColon) && isPropertyNameToken(peek(1))) {
            advance();
            auto constant = _constants.find(id->name + "::" + tokenWord(currentToken()));
            if (constant != _constants.end() || id->name == "CONFIG") {
                advance();
                const std::string value = constant == _constants.end() ? "false" : constant->second;
                if (value == "true" || value == "false") {
                    auto literal = std::make_unique<Literal>(Literal::LiteralKind::Boolean);
                    literal->valueType = Literal::LiteralValueType::Bool;
                    literal->boolValue = value == "true";
                    literal->line = line;
                    return literal;
                }
                if (!value.empty() && (value[0] == '"' || value[0] == '\'')) {
                    auto literal = std::make_unique<Literal>(Literal::LiteralKind::String);
                    literal->valueType = Literal::LiteralValueType::String;
                    literal->stringValue = value.substr(1, value.size() >= 2 ? value.size() - 2 : 0);
                    literal->line = line;
                    return literal;
                }
                auto literal = std::make_unique<Literal>(Literal::LiteralKind::Numeric);
                literal->valueType = Literal::LiteralValueType::Number;
                literal->numberValue = std::strtod(value.c_str(), nullptr);
                literal->line = line;
                return literal;
            }
            auto qualified = std::make_unique<Identifier>(tokenWord(currentToken()));
            qualified->namespacePrefix = id->name;
            qualified->line = line;
            advance();
            return qualified;
        }
        id->line = line;
        return id;
    }

    if (check(TokenType::Number)) {
        auto lit = std::make_unique<Literal>(Literal::LiteralKind::Numeric);
        lit->valueType = Literal::LiteralValueType::Number;
        lit->numberValue = currentToken().numericValue;
        lit->line = line;
        advance();
        return lit;
    }

    if (check(TokenType::String)) {
        auto lit = std::make_unique<Literal>(Literal::LiteralKind::String);
        lit->valueType = Literal::LiteralValueType::String;
        lit->stringValue = currentToken().stringValue;
        lit->line = line;
        advance();
        return lit;
    }

    if (check(TokenType::RegularExpression)) {
        auto lit = std::make_unique<Literal>(Literal::LiteralKind::RegExp);
        lit->stringValue = currentToken().text;
        lit->flags = currentToken().stringValue;
        lit->line = line;
        advance();
        return lit;
    }

    if (check(TokenType::Null)) {
        auto lit = std::make_unique<Literal>(Literal::LiteralKind::Null);
        lit->line = line;
        advance();
        return lit;
    }

    if (check(TokenType::Boolean)) {
        auto lit = std::make_unique<Literal>(Literal::LiteralKind::Boolean);
        lit->valueType = Literal::LiteralValueType::Bool;
        lit->boolValue = currentToken().numericValue != 0;
        lit->line = line;
        advance();
        return lit;
    }

    if (check(KeywordType::This)) {
        advance();
        auto self = std::make_unique<ThisExpression>();
        self->line = line;
        return self;
    }

    if (check(KeywordType::Super)) {
        advance();
        auto super = std::make_unique<SuperExpression>();
        super->line = line;
        return super;
    }

    if (check(KeywordType::Function)) {
        return parseFunctionExpression();
    }

    if (match(TokenType::LBracket)) {
        const bool savedNoIn = _noIn;
        _noIn = false;
        ExpressionList elements;
        while (!check(TokenType::RBracket) && !check(TokenType::EndOfFile)) {
            if (check(TokenType::Comma)) {
                // A hole: [a,,b].
                advance();
                elements.push_back(nullptr);
                continue;
            }
            auto element = parseAssignmentExpression();
            if (!element) {
                break;
            }
            elements.push_back(std::move(element));
            if (!match(TokenType::Comma)) break;
        }
        _noIn = savedNoIn;
        expectToken(TokenType::RBracket, "Expected ']'");
        auto array = std::make_unique<ArrayExpression>(std::move(elements));
        array->line = line;
        return array;
    }

    if (match(TokenType::LBrace)) {
        const bool savedNoIn = _noIn;
        _noIn = false;
        std::vector<std::pair<ExpressionPtr, ExpressionPtr>> props;
        while (!check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
            ExpressionPtr key;
            const Token& token = currentToken();
            if (token.type == TokenType::String) {
                auto literal = std::make_unique<Literal>(Literal::LiteralKind::String);
                literal->valueType = Literal::LiteralValueType::String;
                literal->stringValue = token.stringValue;
                key = std::move(literal);
                advance();
            } else if (token.type == TokenType::Number) {
                auto literal = std::make_unique<Literal>(Literal::LiteralKind::Numeric);
                literal->valueType = Literal::LiteralValueType::Number;
                literal->numberValue = token.numericValue;
                key = std::move(literal);
                advance();
            } else if (isPropertyNameToken(token)) {
                key = std::make_unique<Identifier>(tokenWord(token));
                advance();
            } else {
                addError("Expected a property name in object literal");
                break;
            }
            expectToken(TokenType::Colon, "Expected ':'");
            auto value = parseAssignmentExpression();
            if (!value) {
                break;
            }
            props.emplace_back(std::move(key), std::move(value));
            if (!match(TokenType::Comma)) break;
        }
        _noIn = savedNoIn;
        expectToken(TokenType::RBrace, "Expected '}'");
        auto object = std::make_unique<ObjectExpression>(std::move(props));
        object->line = line;
        return object;
    }

    if (match(TokenType::LParen)) {
        const bool savedNoIn = _noIn;
        _noIn = false;
        auto expr = parseExpression();
        _noIn = savedNoIn;
        expectToken(TokenType::RParen, "Expected ')'");
        return expr;
    }

    if (check(TokenType::LessThan) || check(TokenType::At)) {
        addError("E4X (XML) expressions are not supported");
        advance();
        return nullptr;
    }

    addError("Unexpected token in expression");
    advance();
    return nullptr;
}

std::unique_ptr<Identifier> Parser::parseIdentifier() {
    if (isNameToken(currentToken())) {
        auto id = std::make_unique<Identifier>(tokenWord(currentToken()));
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

    while (atAttribute()) {
        const Token& token = currentToken();
        if (token.type == TokenType::Identifier) {
            // A namespace: kept as nothing.
            advance();
            continue;
        }
        switch (token.keyword) {
            case KeywordType::Public: attrs.push_back(std::make_unique<Attribute>(AttributeType::Public)); break;
            case KeywordType::Private: attrs.push_back(std::make_unique<Attribute>(AttributeType::Private)); break;
            case KeywordType::Protected: attrs.push_back(std::make_unique<Attribute>(AttributeType::Protected)); break;
            case KeywordType::Internal: attrs.push_back(std::make_unique<Attribute>(AttributeType::Internal)); break;
            case KeywordType::Static: attrs.push_back(std::make_unique<Attribute>(AttributeType::Static)); break;
            case KeywordType::Override: attrs.push_back(std::make_unique<Attribute>(AttributeType::Override)); break;
            case KeywordType::Final: attrs.push_back(std::make_unique<Attribute>(AttributeType::Final)); break;
            case KeywordType::Native: attrs.push_back(std::make_unique<Attribute>(AttributeType::Native)); break;
            case KeywordType::Dynamic: attrs.push_back(std::make_unique<Attribute>(AttributeType::Dynamic)); break;
            default: break;
        }
        advance();
    }

    return attrs;
}

std::unique_ptr<FunctionDefinition> Parser::parseFunctionDefinitionWithAttributes(std::vector<AttributePtr> attrs) {
    expectKeyword(KeywordType::Function, "Expected 'function'");
    std::vector<Metadata> metadata = takeMetadata();

    // function get x() and function set x(v); but function get() is a
    // method called get.
    bool isGetter = false;
    bool isSetter = false;
    if ((check(KeywordType::Get) || check(KeywordType::Set)) && isPropertyNameToken(peek(1)) &&
        peek(1).type != TokenType::LParen) {
        isGetter = check(KeywordType::Get);
        isSetter = !isGetter;
        advance();
    }

    IdentifierPtr name;
    if (isPropertyNameToken(currentToken())) {
        name = std::make_unique<Identifier>(tokenWord(currentToken()));
        advance();
    } else {
        name = parseIdentifier();
    }
    expectToken(TokenType::LParen, "Expected '('");

    auto params = parseParameters();

    IdentifierPtr returnType = nullptr;
    if (match(TokenType::Colon)) {
        returnType = parseTypeAnnotation();
    }

    // An interface's method, or a native one, has no body.
    StatementPtr body;
    if (check(TokenType::LBrace)) {
        const bool savedNoIn = _noIn;
        _noIn = false;
        body = parseBlockStatement();
        _noIn = savedNoIn;
    } else {
        endStatement("Expected '{' or ';' after function declaration");
    }

    auto function = std::make_unique<FunctionDefinition>(std::move(attrs), std::move(name), std::move(params),
                                                         std::move(returnType), std::move(body), isGetter, isSetter);
    function->metadata = std::move(metadata);
    return function;
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
        type = parseTypeAnnotation();
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
        case TokenType::Assign: return OperatorType::Assign;
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
