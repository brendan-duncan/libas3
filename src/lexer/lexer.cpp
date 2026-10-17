#include "lexer.h"
#include <cctype>
#include <cmath>
#include <sstream>
#include <cstdlib>

namespace as3 {

Lexer::Lexer(const std::string& source)
    : _source(source)
    , _position(0)
    , _currentTokenIndex(0)
    , _line(1)
    , _column(1) {
}

bool Lexer::isDigit(char c) {
    return c >= '0' && c <= '9';
}

bool Lexer::isHexDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool Lexer::isAlpha(char c) {
    // Bytes of a UTF-8 sequence are letters too: names may use any.
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' ||
           static_cast<unsigned char>(c) >= 0x80;
}

bool Lexer::isAlphaNumeric(char c) {
    return isAlpha(c) || isDigit(c);
}

bool Lexer::isWhiteSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

bool Lexer::isNewLine(char c) {
    return c == '\n' || c == '\r';
}

char Lexer::peekChar(int offset) const {
    size_t pos = _position + offset;
    if (pos >= _source.length()) {
        return '\0';
    }
    return _source[pos];
}

char Lexer::advanceChar() {
    if (_position >= _source.length()) {
        return '\0';
    }
    char c = _source[_position++];
    if (c == '\n') {
        _line++;
        _column = 1;
    } else {
        _column++;
    }
    return c;
}

void Lexer::addToken(const Token& token) {
    _tokens.push_back(token);
    _tokens.back().newLineBefore = _newLineSeen;
    _newLineSeen = false;
}

SourceLocation Lexer::makeLocation() const {
    SourceLocation loc;
    loc.line = _line;
    loc.column = _column;
    loc.offset = static_cast<uint32_t>(_position);
    return loc;
}

Token Lexer::makeToken(TokenType type) const {
    Token token;
    token.type = type;
    token.start = makeLocation();
    token.end = makeLocation();
    return token;
}

Token Lexer::makeKeyword(KeywordType keyword) const {
    Token token;
    token.type = TokenType::Keyword;
    token.keyword = keyword;
    token.start = makeLocation();
    token.end = makeLocation();
    return token;
}

Token Lexer::makeIdentifier(const std::string& text) const {
    Token token;
    token.type = TokenType::Identifier;
    token.text = text;
    token.start = makeLocation();
    token.end = makeLocation();
    return token;
}

Token Lexer::makeString(const std::string& value) const {
    Token token;
    token.type = TokenType::String;
    token.stringValue = value;
    token.start = makeLocation();
    token.end = makeLocation();
    return token;
}

Token Lexer::makeNumber(double value) const {
    Token token;
    token.type = TokenType::Number;
    token.numericValue = value;
    token.start = makeLocation();
    token.end = makeLocation();
    return token;
}

Token Lexer::makeError(const std::string& message) {
    Token token;
    token.type = TokenType::Error;
    token.text = message;
    _errors += "Error at line " + std::to_string(_line) + ": " + message + "\n";
    return token;
}

std::vector<Token> Lexer::tokenize() {
    // A UTF-8 byte order mark, as editors on Windows write.
    if (_source.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        _position = 3;
    }
    while (hasMore()) {
        skipWhiteSpace();
        if (!hasMore()) 
            break;
        scanToken();
    }
    Token eof;
    eof.type = TokenType::EndOfFile;
    eof.start = makeLocation();
    eof.end = makeLocation();
    addToken(eof);
    _currentTokenIndex = 0;
    return _tokens;
}

bool Lexer::hasMore() const {
    return _position < _source.length();
}

const Token& Lexer::currentToken() const {
    if (_currentTokenIndex < _tokens.size()) {
        return _tokens[_currentTokenIndex];
    }
    static Token eof;
    return eof;
}

const Token& Lexer::peek(int offset) const {
    size_t idx = _currentTokenIndex + offset;
    if (idx < _tokens.size()) {
        return _tokens[idx];
    }
    static Token eof;
    eof.type = TokenType::EndOfFile;
    return eof;
}

void Lexer::advance() {
    if (_currentTokenIndex < _tokens.size() - 1) {
        _currentTokenIndex++;
    }
}

void Lexer::skipWhiteSpace() {
    while (hasMore()) {
        char c = peekChar();
        if (isWhiteSpace(c) || c == '\n') {
            // A line break between tokens is whitespace, but a statement
            // may end at one (see Token::newLineBefore).
            if (c == '\n') {
                _newLineSeen = true;
            }
            advanceChar();
        } else if (c == '/') {
            if (peekChar(1) == '/' || peekChar(1) == '*') {
                skipComment();
            } else {
                break;
            }
        } else {
            break;
        }
    }
}

void Lexer::skipComment() {
    char c = peekChar();
    if (c == '/') {
        if (peekChar(1) == '/') {
            while (hasMore() && !isNewLine(peekChar())) {
                advanceChar();
            }
        } else if (peekChar(1) == '*') {
            advanceChar();
            advanceChar();
            while (hasMore()) {
                if (peekChar() == '*' && peekChar(1) == '/') {
                    advanceChar();
                    advanceChar();
                    break;
                }
                advanceChar();
            }
        }
    }
}

void Lexer::scanToken() {
    char c = peekChar();

    if (c == '{') {
        Token t = makeToken(TokenType::LBrace);
        t.text = "{";
        addToken(t);
        advanceChar();
    } else if (c == '}') {
        Token t = makeToken(TokenType::RBrace);
        t.text = "}";
        addToken(t);
        advanceChar();
    } else if (c == '(') {
        Token t = makeToken(TokenType::LParen);
        t.text = "(";
        addToken(t);
        advanceChar();
    } else if (c == ')') {
        Token t = makeToken(TokenType::RParen);
        t.text = ")";
        addToken(t);
        advanceChar();
    } else if (c == '[') {
        Token t = makeToken(TokenType::LBracket);
        t.text = "[";
        addToken(t);
        advanceChar();
    } else if (c == ']') {
        Token t = makeToken(TokenType::RBracket);
        t.text = "]";
        addToken(t);
        advanceChar();
    } else if (c == ';') {
        Token t = makeToken(TokenType::SemiColon);
        t.text = ";";
        addToken(t);
        advanceChar();
    } else if (c == ',') {
        Token t = makeToken(TokenType::Comma);
        t.text = ",";
        addToken(t);
        advanceChar();
    } else if (c == '?') {
        if (peekChar(1) == '.') {
            Token t = makeToken(TokenType::QuestionDot);
            t.text = "?.";
            addToken(t);
            advanceChar();
            advanceChar();
        } else if (peekChar(1) == '?') {
            Token t = makeToken(TokenType::NullCoalescing);
            t.text = "??";
            addToken(t);
            advanceChar();
            advanceChar();
        } else {
            Token t = makeToken(TokenType::Question);
            t.text = "?";
            addToken(t);
            advanceChar();
        }
    } else if (c == ':') {
        if (peekChar(1) == ':') {
            Token t = makeToken(TokenType::DoubleColon);
            t.text = "::";
            addToken(t);
            advanceChar();
            advanceChar();
        } else {
            Token t = makeToken(TokenType::Colon);
            t.text = ":";
            addToken(t);
            advanceChar();
        }
    } else if (c == '.' && isDigit(peekChar(1))) {
        // .5
        scanNumber();
    } else if (c == '.') {
        if (peekChar(1) == '.' && peekChar(2) == '.') {
            Token t = makeToken(TokenType::DotDotDot);
            t.text = "...";
            addToken(t);
            advanceChar();
            advanceChar();
            advanceChar();
        } else {
            Token t = makeToken(TokenType::Dot);
            t.text = ".";
            addToken(t);
            advanceChar();
        }
    } else if (c == '<' && xmlAllowed() &&
               (isAlpha(peekChar(1)) || peekChar(1) == '>' || peekChar(1) == '{' || peekChar(1) == '!' ||
                peekChar(1) == '?')) {
        scanXml();
    } else if (c == '<') {
        if (peekChar(1) == '=') {
            Token t = makeToken(TokenType::LessThanOrEqual);
            t.text = "<=";
            addToken(t);
            advanceChar();
            advanceChar();
        } else if (peekChar(1) == '<') {
            if (peekChar(2) == '=') {
                Token t = makeToken(TokenType::LeftShiftAssign);
                t.text = "<<=";
                addToken(t);
                advanceChar();
                advanceChar();
                advanceChar();
            } else {
                if (peekChar(2) == '<') {
                    if (peekChar(3) == '=') {
                        Token t = makeToken(TokenType::UnsignedLeftShiftAssign);
                        t.text = "<<<=";
                        addToken(t);
                        advanceChar();
                        advanceChar();
                        advanceChar();
                        advanceChar();
                    } else {
                        Token t = makeToken(TokenType::UnsignedLeftShift);
                        t.text = "<<<";
                        addToken(t);
                        advanceChar();
                        advanceChar();
                        advanceChar();
                    }
                } else {
                    Token t = makeToken(TokenType::LeftShift);
                    t.text = "<<";
                    addToken(t);
                    advanceChar();
                    advanceChar();
                }
            }
        } else {
            Token t = makeToken(TokenType::LessThan);
            t.text = "<";
            addToken(t);
            advanceChar();
        }
    } else if (c == '>') {
        if (peekChar(1) == '=') {
            Token t = makeToken(TokenType::GreaterThanOrEqual);
            t.text = ">=";
            addToken(t);
            advanceChar();
            advanceChar();
        } else if (peekChar(1) == '>') {
            if (peekChar(2) == '=') {
                Token t = makeToken(TokenType::RightShiftAssign);
                t.text = ">>=";
                addToken(t);
                advanceChar();
                advanceChar();
                advanceChar();
            } else if (peekChar(2) == '>') {
                if (peekChar(3) == '=') {
                    Token t = makeToken(TokenType::UnsignedRightShiftAssign);
                    t.text = ">>>=";
                    addToken(t);
                    advanceChar();
                    advanceChar();
                    advanceChar();
                    advanceChar();
                } else {
                    Token t = makeToken(TokenType::UnsignedRightShift);
                    t.text = ">>>";
                    addToken(t);
                    advanceChar();
                    advanceChar();
                    advanceChar();
                }
            } else {
                Token t = makeToken(TokenType::RightShift);
                t.text = ">>";
                addToken(t);
                advanceChar();
                advanceChar();
            }
        } else {
            Token t = makeToken(TokenType::GreaterThan);
            t.text = ">";
            addToken(t);
            advanceChar();
        }
    } else if (c == '=') {
        if (peekChar(1) == '=') {
            if (peekChar(2) == '=') {
                Token t = makeToken(TokenType::StrictEquals);
                t.text = "===";
                addToken(t);
                advanceChar();
                advanceChar();
                advanceChar();
            } else {
                Token t = makeToken(TokenType::Equals);
                t.text = "==";
                addToken(t);
                advanceChar();
                advanceChar();
            }
        } else {
            Token t = makeToken(TokenType::Assign);
            t.text = "=";
            addToken(t);
            advanceChar();
        }
    } else if (c == '!') {
        if (peekChar(1) == '=') {
            if (peekChar(2) == '=') {
                Token t = makeToken(TokenType::StrictNotEquals);
                t.text = "!==";
                addToken(t);
                advanceChar();
                advanceChar();
                advanceChar();
            } else {
                Token t = makeToken(TokenType::NotEquals);
                t.text = "!=";
                addToken(t);
                advanceChar();
                advanceChar();
            }
        } else {
            Token t = makeToken(TokenType::Exclamation);
            t.text = "!";
            addToken(t);
            advanceChar();
        }
    } else if (c == '+') {
        if (peekChar(1) == '+') {
            Token t = makeToken(TokenType::Increment);
            t.text = "++";
            addToken(t);
            advanceChar();
            advanceChar();
        } else if (peekChar(1) == '=') {
            Token t = makeToken(TokenType::PlusAssign);
            t.text = "+=";
            addToken(t);
            advanceChar();
            advanceChar();
        } else {
            Token t = makeToken(TokenType::Plus);
            t.text = "+";
            addToken(t);
            advanceChar();
        }
    } else if (c == '-') {
        if (peekChar(1) == '-') {
            Token t = makeToken(TokenType::Decrement);
            t.text = "--";
            addToken(t);
            advanceChar();
            advanceChar();
        } else if (peekChar(1) == '=') {
            Token t = makeToken(TokenType::MinusAssign);
            t.text = "-=";
            addToken(t);
            advanceChar();
            advanceChar();
        } else {
            Token t = makeToken(TokenType::Minus);
            t.text = "-";
            addToken(t);
            advanceChar();
        }
    } else if (c == '*') {
        if (peekChar(1) == '*') {
            if (peekChar(2) == '=') {
                Token t = makeToken(TokenType::PowerAssign);
                t.text = "**=";
                addToken(t);
                advanceChar();
                advanceChar();
                advanceChar();
            } else {
                Token t = makeToken(TokenType::Power);
                t.text = "**";
                addToken(t);
                advanceChar();
                advanceChar();
            }
        } else if (peekChar(1) == '=') {
            Token t = makeToken(TokenType::MultiplyAssign);
            t.text = "*=";
            addToken(t);
            advanceChar();
            advanceChar();
        } else {
            Token t = makeToken(TokenType::Multiply);
            t.text = "*";
            addToken(t);
            advanceChar();
        }
    } else if (c == '/') {
        if (peekChar(1) == '=' && !regexAllowed()) {
            Token t = makeToken(TokenType::DivideAssign);
            t.text = "/=";
            addToken(t);
            advanceChar();
            advanceChar();
        } else if (peekChar(1) == '/') {
            while (hasMore() && !isNewLine(peekChar())) {
                advanceChar();
            }
        } else if (peekChar(1) == '*') {
            advanceChar();
            advanceChar();
            while (hasMore()) {
                if (peekChar() == '*' && peekChar(1) == '/') {
                    advanceChar();
                    advanceChar();
                    break;
                }
                advanceChar();
            }
        } else if (regexAllowed()) {
            scanRegExp();
        } else {
            Token t = makeToken(TokenType::Divide);
            t.text = "/";
            addToken(t);
            advanceChar();
        }
    } else if (c == '%') {
        if (peekChar(1) == '=') {
            Token t = makeToken(TokenType::RemainderAssign);
            t.text = "%=";
            addToken(t);
            advanceChar();
            advanceChar();
        } else {
            Token t = makeToken(TokenType::Remainder);
            t.text = "%";
            addToken(t);
            advanceChar();
        }
    } else if (c == '&') {
        if (peekChar(1) == '&') {
            Token t = makeToken(TokenType::LogicalAnd);
            t.text = "&&";
            addToken(t);
            advanceChar();
            advanceChar();
        } else if (peekChar(1) == '=') {
            Token t = makeToken(TokenType::AmpersandAssign);
            t.text = "&=";
            addToken(t);
            advanceChar();
            advanceChar();
        } else {
            Token t = makeToken(TokenType::Ampersand);
            t.text = "&";
            addToken(t);
            advanceChar();
        }
    } else if (c == '|') {
        if (peekChar(1) == '|') {
            Token t = makeToken(TokenType::LogicalOr);
            t.text = "||";
            addToken(t);
            advanceChar();
            advanceChar();
        } else if (peekChar(1) == '=') {
            Token t = makeToken(TokenType::PipeAssign);
            t.text = "|=";
            addToken(t);
            advanceChar();
            advanceChar();
        } else {
            Token t = makeToken(TokenType::Pipe);
            t.text = "|";
            addToken(t);
            advanceChar();
        }
    } else if (c == '^') {
        if (peekChar(1) == '^') {
            Token t = makeToken(TokenType::LogicalXor);
            t.text = "^^";
            addToken(t);
            advanceChar();
            advanceChar();
        } else if (peekChar(1) == '=') {
            Token t = makeToken(TokenType::CaretAssign);
            t.text = "^=";
            addToken(t);
            advanceChar();
            advanceChar();
        } else {
            Token t = makeToken(TokenType::Caret);
            t.text = "^";
            addToken(t);
            advanceChar();
        }
    } else if (c == '~') {
        Token t = makeToken(TokenType::Tilde);
        t.text = "~";
        addToken(t);
        advanceChar();
    } else if (isDigit(c) || (c == '.' && isDigit(peekChar(1)))) {
        scanNumber();
    } else if (c == '"' || c == '\'') {
        scanString();
    } else if (c == '@') {
        Token t = makeToken(TokenType::At);
        t.text = "@";
        addToken(t);
        advanceChar();
    } else if (isAlpha(c)) {
        scanIdentifier();
    } else {
        std::string msg = "Unexpected character: ";
        msg += c;
        addToken(makeError(msg));
        advanceChar();
    }
}

void Lexer::scanIdentifier() {
    std::string text;
    SourceLocation start = makeLocation();

    while (hasMore() && isAlphaNumeric(peekChar())) {
        text += advanceChar();
    }

    if (text == "null") {
        Token t = makeToken(TokenType::Null);
        t.text = text;
        addToken(t);
    } else if (text == "true" || text == "false") {
        Token t = makeToken(TokenType::Boolean);
        t.text = text;
        t.numericValue = (text == "true") ? 1.0 : 0.0;
        addToken(t);
    } else {
        KeywordType kw = getKeywordType(text);
        if (kw != KeywordType::None) {
            Token t = makeKeyword(kw);
            t.text = text;
            addToken(t);
        } else {
            Token t = makeIdentifier(text);
            t.start = start;
            t.end = makeLocation();
            addToken(t);
        }
    }
}

namespace {

// A code point as UTF-8.
void appendUtf8(std::string& out, uint32_t code) {
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

void Lexer::scanString() {
    char quote = peekChar();
    SourceLocation start = makeLocation();
    advanceChar();
    std::string value;
    uint32_t pendingHigh = 0;  // the first half of a \u surrogate pair

    while (hasMore() && peekChar() != quote) {
        char c = peekChar();
        if (c == '\\') {
            advanceChar();
            char escaped = advanceChar();
            switch (escaped) {
                case 'n': value += '\n'; break;
                case 'r': value += '\r'; break;
                case 't': value += '\t'; break;
                case 'b': value += '\b'; break;
                case 'f': value += '\f'; break;
                case 'v': value += '\v'; break;
                case '0': value += '\0'; break;
                case '\r':
                    // A line continued: the break is not in the string.
                    if (peekChar() == '\n') advanceChar();
                    break;
                case '\n':
                    break;
                case 'x':
                case 'u': {
                    const int digits = escaped == 'x' ? 2 : 4;
                    uint32_t code = 0;
                    int read = 0;
                    while (read < digits && hexValue(peekChar()) >= 0) {
                        code = code * 16 + static_cast<uint32_t>(hexValue(advanceChar()));
                        ++read;
                    }
                    if (read == 0) {
                        value += escaped;
                        break;
                    }
                    if (code >= 0xD800 && code < 0xDC00) {
                        pendingHigh = code;
                        break;
                    }
                    if (code >= 0xDC00 && code < 0xE000 && pendingHigh) {
                        code = 0x10000 + ((pendingHigh - 0xD800) << 10) + (code - 0xDC00);
                    }
                    pendingHigh = 0;
                    appendUtf8(value, code);
                    break;
                }
                default:
                    value += escaped;
                    break;
            }
        } else if (isNewLine(c)) {
            addToken(makeError("Unterminated string"));
            break;
        } else {
            value += advanceChar();
        }
    }

    if (peekChar() == quote) {
        advanceChar();
    } else {
        addToken(makeError("Unterminated string"));
    }

    Token t = makeString(value);
    t.text = value;
    t.start = start;
    t.end = makeLocation();
    addToken(t);
}

// Whether a / here starts a regular expression rather than dividing: it
// does where a value is expected, after an operator, a keyword or nothing.
bool Lexer::regexAllowed() const {
    if (_tokens.empty()) {
        return true;
    }
    const Token& last = _tokens.back();
    switch (last.type) {
        case TokenType::Identifier:
        case TokenType::Number:
        case TokenType::String:
        case TokenType::Boolean:
        case TokenType::Null:
        case TokenType::RegularExpression:
        case TokenType::XmlLiteral:
        case TokenType::RParen:
        case TokenType::RBracket:
        case TokenType::RBrace:
        case TokenType::Increment:
        case TokenType::Decrement:
            return false;
        case TokenType::Keyword:
            return last.keyword != KeywordType::This && last.keyword != KeywordType::Super;
        default:
            return true;
    }
}

bool Lexer::xmlAllowed() const {
    if (!regexAllowed()) {
        return false;
    }
    if (_tokens.empty()) {
        return true;
    }
    const Token& last = _tokens.back();
    if (last.type == TokenType::Dot) {
        return false;
    }
    return !(last.type == TokenType::Keyword && last.keyword == KeywordType::New);
}

// An XML literal, to its end: its tags balanced, with what is in braces
// (code), quotes, comments and CDATA read past.
void Lexer::scanXml() {
    SourceLocation start = makeLocation();
    const size_t from = _position;
    int depth = 0;
    const auto startsWith = [this](const char* text) {
        return _source.compare(_position, std::char_traits<char>::length(text), text) == 0;
    };
    const auto skipTo = [this](const char* end) {
        const size_t found = _source.find(end, _position);
        const size_t stop = found == std::string::npos ? _source.size() : found + std::char_traits<char>::length(end);
        while (_position < stop) advanceChar();
    };
    // Code in braces, its strings and nested braces read past.
    const auto skipBraces = [this]() {
        int braces = 0;
        while (hasMore()) {
            const char c = advanceChar();
            if (c == '{') {
                ++braces;
            } else if (c == '}') {
                if (--braces == 0) return;
            } else if (c == '"' || c == '\'') {
                while (hasMore() && peekChar() != c) {
                    if (peekChar() == '\\') advanceChar();
                    advanceChar();
                }
                if (hasMore()) advanceChar();
            }
        }
    };
    bool ended = false;
    while (hasMore() && !ended) {
        if (peekChar() == '{') {
            skipBraces();
            continue;
        }
        if (peekChar() != '<') {
            advanceChar();
            continue;
        }
        if (startsWith("<!--")) {
            skipTo("-->");
        } else if (startsWith("<![CDATA[")) {
            skipTo("]]>");
        } else if (startsWith("<?")) {
            skipTo("?>");
        } else if (startsWith("</")) {
            // A closing tag (or </> closing a list).
            while (hasMore() && peekChar() != '>') {
                if (peekChar() == '{') {
                    skipBraces();
                } else {
                    advanceChar();
                }
            }
            if (hasMore()) advanceChar();
            if (--depth <= 0) ended = true;
        } else {
            // An opening tag: to its >, past quoted values and braces.
            advanceChar();
            bool selfClosing = false;
            while (hasMore() && peekChar() != '>') {
                const char c = peekChar();
                if (c == '{') {
                    skipBraces();
                    continue;
                }
                if (c == '"' || c == '\'') {
                    advanceChar();
                    while (hasMore() && peekChar() != c) advanceChar();
                    if (hasMore()) advanceChar();
                    continue;
                }
                selfClosing = c == '/';
                advanceChar();
            }
            if (hasMore()) advanceChar();
            if (selfClosing) {
                if (depth == 0) ended = true;
            } else {
                ++depth;
            }
        }
    }
    if (!ended) {
        addToken(makeError("Unterminated XML literal"));
        return;
    }
    Token t = makeToken(TokenType::XmlLiteral);
    t.text = _source.substr(from, _position - from);
    t.start = start;
    t.end = makeLocation();
    addToken(t);
}

void Lexer::scanRegExp() {
    SourceLocation start = makeLocation();
    advanceChar();  // the opening /
    std::string pattern;
    bool inClass = false;
    while (hasMore() && !isNewLine(peekChar())) {
        char c = peekChar();
        if (c == '\\') {
            pattern += advanceChar();
            if (hasMore()) pattern += advanceChar();
            continue;
        }
        if (c == '[') inClass = true;
        else if (c == ']') inClass = false;
        else if (c == '/' && !inClass) break;
        pattern += advanceChar();
    }
    if (peekChar() != '/') {
        addToken(makeError("Unterminated regular expression"));
        return;
    }
    advanceChar();
    std::string flags;
    while (hasMore() && isAlphaNumeric(peekChar())) {
        flags += advanceChar();
    }
    Token t = makeToken(TokenType::RegularExpression);
    t.text = pattern;
    t.stringValue = flags;
    t.start = start;
    t.end = makeLocation();
    addToken(t);
}

void Lexer::scanNumber() {
    std::string numStr;
    SourceLocation start = makeLocation();

    if (peekChar() == '0' && (peekChar(1) == 'x' || peekChar(1) == 'X')) {
        advanceChar();
        advanceChar();
        // As a double: 0xFF000000 and larger are more than an int holds.
        double value = 0;
        while (isHexDigit(peekChar())) {
            value = value * 16 + hexValue(advanceChar());
        }
        Token t = makeNumber(value);
        t.start = start;
        t.end = makeLocation();
        addToken(t);
        return;
    }

    while (isDigit(peekChar()) || peekChar() == '.') {
        if (peekChar() == '.') {
            // One point only, and not the first of two (a range or 1..x).
            if (numStr.find('.') != std::string::npos) break;
            if (peekChar(1) == '.') break;
        }
        numStr += advanceChar();
    }

    if ((peekChar() == 'e' || peekChar() == 'E') &&
        (isDigit(peekChar(1)) || ((peekChar(1) == '+' || peekChar(1) == '-') && isDigit(peekChar(2))))) {
        numStr += advanceChar();
        if (peekChar() == '+' || peekChar() == '-') {
            numStr += advanceChar();
        }
        while (isDigit(peekChar())) {
            numStr += advanceChar();
        }
    }

    double value = std::strtod(numStr.c_str(), nullptr);
    Token t = makeNumber(value);
    t.text = numStr;
    t.start = start;
    t.end = makeLocation();
    addToken(t);
}

KeywordType Lexer::getKeywordType(const std::string& text) const {
    if (text == "as") return KeywordType::As;
    if (text == "break") return KeywordType::Break;
    if (text == "case") return KeywordType::Case;
    if (text == "catch") return KeywordType::Catch;
    if (text == "class") return KeywordType::Class;
    if (text == "const") return KeywordType::Const;
    if (text == "continue") return KeywordType::Continue;
    if (text == "default") return KeywordType::Default;
    if (text == "delete") return KeywordType::Delete;
    if (text == "do") return KeywordType::Do;
    if (text == "else") return KeywordType::Else;
    if (text == "extends") return KeywordType::Extends;
    if (text == "false") return KeywordType::False;
    if (text == "finally") return KeywordType::Finally;
    if (text == "for") return KeywordType::For;
    if (text == "function") return KeywordType::Function;
    if (text == "get") return KeywordType::Get;
    if (text == "if") return KeywordType::If;
    if (text == "implements") return KeywordType::Implements;
    if (text == "import") return KeywordType::Import;
    if (text == "in") return KeywordType::In;
    if (text == "instanceof") return KeywordType::Instanceof;
    if (text == "interface") return KeywordType::Interface;
    if (text == "internal") return KeywordType::Internal;
    if (text == "is") return KeywordType::Is;
    if (text == "let") return KeywordType::Let;
    if (text == "native") return KeywordType::Native;
    if (text == "new") return KeywordType::New;
    if (text == "null") return KeywordType::Null;
    if (text == "of") return KeywordType::Of;
    if (text == "package") return KeywordType::Package;
    if (text == "private") return KeywordType::Private;
    if (text == "protected") return KeywordType::Protected;
    if (text == "public") return KeywordType::Public;
    if (text == "return") return KeywordType::Return;
    if (text == "set") return KeywordType::Set;
    if (text == "static") return KeywordType::Static;
    if (text == "super") return KeywordType::Super;
    if (text == "switch") return KeywordType::Switch;
    if (text == "this") return KeywordType::This;
    if (text == "throw") return KeywordType::Throw;
    if (text == "true") return KeywordType::True;
    if (text == "try") return KeywordType::Try;
    if (text == "typeof") return KeywordType::Typeof;
    if (text == "use") return KeywordType::Use;
    if (text == "var") return KeywordType::Var;
    if (text == "void") return KeywordType::Void;
    if (text == "while") return KeywordType::While;
    if (text == "with") return KeywordType::With;
    if (text == "yield") return KeywordType::Yield;
    if (text == "async") return KeywordType::Async;
    if (text == "await") return KeywordType::Await;
    if (text == "each") return KeywordType::Each;
    if (text == "enum") return KeywordType::Enum;
    if (text == "namespace") return KeywordType::Namespace;
    if (text == "override") return KeywordType::Override;
    if (text == "dynamic") return KeywordType::Dynamic;
    if (text == "final") return KeywordType::Final;
    return KeywordType::None;
}

} // namespace as3
