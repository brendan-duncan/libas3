#ifndef PARSER_H
#define PARSER_H

#include <string>
#include <vector>
#include <memory>
#include "../lexer/lexer.h"
#include "../lexer/token.h"
#include "../ast/ast_nodes.h"

namespace as3 {

class Parser {
public:
    explicit Parser(const std::string& source);

    ~Parser();

    std::unique_ptr<Program> parse();

    const std::string& getErrors() const { return _errors; }

    bool hasErrors() const { return !_errors.empty(); }

private:
    Lexer _lexer;
    std::vector<Token> _tokens;
    size_t _position;
    std::string _errors;
    Token _eofToken;

    const Token& currentToken() const;
    const Token& peek(int offset = 0) const;
    bool peekForInOrOf();
    void advance();
    bool check(TokenType type) const;
    bool check(KeywordType keyword) const;
    bool match(TokenType type);
    bool match(KeywordType keyword);
    void expect(bool condition, const std::string& message);
    void expectToken(TokenType type, const std::string& message);
    void expectKeyword(KeywordType keyword, const std::string& message);

    void addError(const std::string& message);
    void synchronize();
    std::string parseQualifiedName();

    std::unique_ptr<Program> parseProgram();
    std::vector<std::unique_ptr<ImportDirective>> parseImports();
    std::unique_ptr<PackageDefinition> parsePackage();
    std::unique_ptr<AstNode> parseTopLevel();

    std::unique_ptr<Statement> parseStatement();
    std::unique_ptr<Statement> parseStatementBody();
    std::unique_ptr<Statement> parseBlockStatement();
    std::unique_ptr<Statement> parseEmptyStatement();
    std::unique_ptr<Statement> parseExpressionStatement();
    std::unique_ptr<Statement> parseIfStatement();
    std::unique_ptr<Statement> parseSwitchStatement();
    std::unique_ptr<Statement> parseWhileStatement();
    std::unique_ptr<Statement> parseDoWhileStatement();
    std::unique_ptr<Statement> parseForStatement();
    std::unique_ptr<Statement> parseForInOrOfStatement(bool isForEach);
    std::unique_ptr<Statement> parseBreakStatement();
    std::unique_ptr<Statement> parseContinueStatement();
    std::unique_ptr<Statement> parseReturnStatement();
    std::unique_ptr<Statement> parseThrowStatement();
    std::unique_ptr<Statement> parseTryStatement();
    std::unique_ptr<Statement> parseWithStatement();
    std::unique_ptr<Statement> parseLabeledStatement();
    std::unique_ptr<Statement> parseVariableStatement();
    std::unique_ptr<Statement> parseFunctionDeclaration();
    std::unique_ptr<Statement> parseClassDeclaration();
    std::unique_ptr<Statement> parseNamespaceDeclaration();
    std::unique_ptr<Statement> parseLegacyBlock();
    void endStatement(const std::string& message);

    std::unique_ptr<Expression> parseExpression();
    std::unique_ptr<Expression> parseAssignmentExpression();
    std::unique_ptr<Expression> parseConditionalExpression();
    std::unique_ptr<Expression> parseNullCoalescingExpression();
    std::unique_ptr<Expression> parseLogicalOrExpression();
    std::unique_ptr<Expression> parseLogicalAndExpression();
    std::unique_ptr<Expression> parseBitwiseOrExpression();
    std::unique_ptr<Expression> parseBitwiseXorExpression();
    std::unique_ptr<Expression> parseBitwiseAndExpression();
    std::unique_ptr<Expression> parseEqualityExpression();
    std::unique_ptr<Expression> parseRelationalExpression();
    std::unique_ptr<Expression> parseShiftExpression();
    std::unique_ptr<Expression> parseAdditiveExpression();
    std::unique_ptr<Expression> parseMultiplicativeExpression();
    std::unique_ptr<Expression> parseUnaryExpression();
    std::unique_ptr<Expression> parsePostfixExpression();
    std::unique_ptr<Expression> parseCallMemberExpression();
    std::unique_ptr<Expression> parseNewExpression();
    std::unique_ptr<Expression> parsePrimaryExpression();

    std::unique_ptr<Identifier> parseIdentifier();
    std::vector<std::unique_ptr<Attribute>> parseAttributes();
    std::unique_ptr<Attribute> parseAttribute();
    std::unique_ptr<FunctionDefinition> parseFunctionDefinition(bool isDeclaration = true);
    std::unique_ptr<FunctionDefinition> parseFunctionDefinitionWithAttributes(std::vector<AttributePtr> attrs);
    std::unique_ptr<Statement> parseVariableStatementWithAttributes(std::vector<AttributePtr> attrs);
    std::unique_ptr<AstNode> parsePropertyWithAttributes(std::vector<AttributePtr> attrs);
    std::unique_ptr<ClassDefinition> parseClassDefinition();
    std::vector<std::unique_ptr<Parameter>> parseParameters();
    std::unique_ptr<Parameter> parseParameter();

    OperatorType getBinaryOperator(TokenType type) const;
};

}

#endif
