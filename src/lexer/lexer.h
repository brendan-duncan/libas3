#pragma once

#include <string>
#include <vector>
#include "token.h"

namespace as3 {

class Lexer {
public:
    explicit Lexer(const std::string& source);

    std::vector<Token> tokenize();

    const Token& currentToken() const;

    const Token& peek(int offset) const;

    void advance();

    bool hasMore() const;

    const std::string& getErrors() const { return _errors; }

private:
    std::string _source;
    std::vector<Token> _tokens;
    size_t _position;
    size_t _currentTokenIndex;
    uint32_t _line;
    uint32_t _column;
    // A line break was skipped since the last token.
    bool _newLineSeen = false;
    std::string _errors;

    static bool isDigit(char c);
    static bool isHexDigit(char c);
    static bool isAlpha(char c);
    static bool isAlphaNumeric(char c);
    static bool isWhiteSpace(char c);
    static bool isNewLine(char c);

    char peekChar(int offset = 0) const;
    char advanceChar();
    void addToken(const Token& token);
    void scanToken();
    void scanIdentifier();
    void scanString();
    void scanNumber();
    void scanRegExp();
    bool regexAllowed() const;
    // Where < starts an XML literal: where a value is expected, but not
    // after new (new <int>[]) or a dot (Vector.<int>).
    bool xmlAllowed() const;
    void scanXml();
    void skipWhiteSpace();
    void skipComment();

    KeywordType getKeywordType(const std::string& text) const;
    SourceLocation makeLocation() const;
    Token makeToken(TokenType type) const;
    Token makeKeyword(KeywordType keyword) const;
    Token makeIdentifier(const std::string& text) const;
    Token makeString(const std::string& value) const;
    Token makeNumber(double value) const;
    Token makeError(const std::string& message);
};

} // namespace as3
