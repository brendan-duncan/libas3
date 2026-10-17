#ifndef PARSER_H
#define PARSER_H

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <functional>
#include <optional>
#include "../lexer/lexer.h"
#include "../lexer/token.h"
#include "../ast/ast_nodes.h"

namespace as3 {

class Parser {
public:
    // The file name, when given, is kept on the program and named in
    // messages.
    explicit Parser(const std::string& source, const std::string& fileName = std::string());

    ~Parser();

    std::unique_ptr<Program> parse();

    // Errors, a line each: "Parse error at line N, column C: what".
    const std::string& getErrors() const { return _errors; }

    bool hasErrors() const { return !_errors.empty(); }

    // What was accepted though not quite right, a line each, as two
    // expressions on one line with nothing between them (a.b 0.07;), which
    // is taken as two statements. In strict mode these are errors.
    const std::string& getWarnings() const { return _warnings; }
    void setStrict(bool strict) { _strict = strict; }

    // A conditional compilation constant, as CONFIG::debug: its value as
    // written ("true", "false", a number or a quoted string). A block or
    // definition after one is kept when it is true and left out when it is
    // false; one not defined is false. In an expression it is its value.
    void defineConstant(const std::string& qualifiedName, const std::string& value) {
        _constants[qualifiedName] = value;
    }

    // What include "file.as" reads: the handler gives the file's source
    // (nullopt when there is none, an error). Without one, an include is
    // left out with a warning.
    using IncludeHandler = std::function<std::optional<std::string>(const std::string& path)>;
    void setIncludeHandler(IncludeHandler handler) { _includeHandler = std::move(handler); }

private:
    Lexer _lexer;
    std::vector<Token> _tokens;
    size_t _position;
    std::string _errors;
    std::string _warnings;
    std::string _fileName;
    Token _eofToken;
    bool _strict = false;
    // In a for statement's first part, where "in" is not an operator.
    bool _noIn = false;
    // Metadata read before a definition, for it.
    std::vector<Metadata> _pendingMetadata;
    std::map<std::string, std::string> _constants;
    IncludeHandler _includeHandler;
    int _includeDepth = 0;
    // How deep statements and expressions nest, capped so that a parse
    // cannot use the whole stack.
    int _nesting = 0;
    bool enterNesting();
    // ns::name before a block or a definition: whether that is kept.
    bool atConfigCondition() const;
    bool readConfigCondition();

    const Token& currentToken() const;
    const Token& peek(int offset = 0) const;
    void advance();
    bool check(TokenType type) const;
    bool check(KeywordType keyword) const;
    bool match(TokenType type);
    bool match(KeywordType keyword);
    void expectToken(TokenType type, const std::string& message);
    void expectKeyword(KeywordType keyword, const std::string& message);

    // Whether a token can be a name: an identifier, or a keyword that is
    // only one in some places (get, set, each, namespace...).
    static bool isNameToken(const Token& token);
    // Whether a token can name a property after a dot: any word.
    static bool isPropertyNameToken(const Token& token);
    static std::string tokenWord(const Token& token);

    void addError(const std::string& message);
    void addWarning(const std::string& message);
    void synchronize();
    std::string parseQualifiedName();
    // A type as written: *, void, a.b.C, Vector.<T>.
    std::string parseTypeName();
    // The > closing type arguments, taken from >> or >>> when nested.
    void expectTypeClose();
    IdentifierPtr parseTypeAnnotation();
    bool looksLikeMetadata() const;
    void parseMetadata();
    std::vector<Metadata> takeMetadata();

    std::unique_ptr<Program> parseProgram();
    // Consecutive imports; only the next one with \a one.
    std::vector<std::unique_ptr<ImportDirective>> parseImports(bool one = false);
    std::unique_ptr<PackageDefinition> parsePackage();
    std::unique_ptr<AstNode> parseTopLevel();
    // What attributes and metadata come before: a class, interface,
    // function, variable or namespace.
    std::unique_ptr<AstNode> parseDefinition(std::vector<AttributePtr> attrs);
    bool atAttribute() const;

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
    std::unique_ptr<Statement> parseBreakStatement();
    std::unique_ptr<Statement> parseContinueStatement();
    std::unique_ptr<Statement> parseReturnStatement();
    std::unique_ptr<Statement> parseThrowStatement();
    std::unique_ptr<Statement> parseTryStatement();
    std::unique_ptr<Statement> parseWithStatement();
    std::unique_ptr<Statement> parseLabeledStatement();
    std::unique_ptr<Statement> parseVariableStatement(std::vector<AttributePtr> attrs = {});
    std::unique_ptr<VariableDefinition> parseVariableList(std::vector<AttributePtr> attrs, bool isConst);
    std::unique_ptr<Statement> parseClassDeclaration(std::vector<AttributePtr> attrs, bool isInterface);
    std::unique_ptr<Statement> parseNamespaceDeclaration();
    std::unique_ptr<Statement> parseLegacyBlock();
    std::unique_ptr<Statement> parseUseOrInclude();
    void endStatement(const std::string& message);

    std::unique_ptr<Expression> parseExpression();
    std::unique_ptr<Expression> parseAssignmentExpression();
    std::unique_ptr<Expression> parseConditionalExpression();
    std::unique_ptr<Expression> parseBinaryExpression(int minimum);
    int binaryLevel(OperatorType& op) const;
    // Whether a token can begin an operand, on the line of what is before.
    static bool startsOperand(const Token& token);
    std::unique_ptr<Expression> parseUnaryExpression();
    std::unique_ptr<Expression> parsePostfixExpression();
    std::unique_ptr<Expression> parseCallMemberExpression();
    std::unique_ptr<Expression> parseNewExpression();
    std::unique_ptr<Expression> parsePrimaryExpression();
    // An XML literal: XML("...") of its text, what is in braces evaluated
    // and put in.
    std::unique_ptr<Expression> xmlLiteral(const std::string& text, int line);
    // E4X after a dot: .@name, ..name, .*, .(filter); nullptr when not.
    std::unique_ptr<Expression> parseXmlMember(std::unique_ptr<Expression>& object, int line);
    ExpressionList parseArguments();
    std::vector<std::string> parseTypeArguments();

    std::unique_ptr<Identifier> parseIdentifier();
    std::vector<std::unique_ptr<Attribute>> parseAttributes();
    std::unique_ptr<FunctionDefinition> parseFunctionDefinitionWithAttributes(std::vector<AttributePtr> attrs);
    std::unique_ptr<Expression> parseFunctionExpression();
    std::vector<std::unique_ptr<Parameter>> parseParameters();
    std::unique_ptr<Parameter> parseParameter();

    OperatorType getBinaryOperator(TokenType type) const;
};

}

#endif
