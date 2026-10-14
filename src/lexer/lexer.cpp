#include "lexer.h"
#include <cctype>
#include <cmath>
#include <sstream>

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
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
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
        if (peekChar(1) == '=') {
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

void Lexer::scanString() {
    char quote = peekChar();
    advanceChar();
    std::string value;
    SourceLocation start = makeLocation();

    while (hasMore() && peekChar() != quote) {
        char c = peekChar();
        if (c == '\\') {
            advanceChar();
            char escaped = peekChar();
            switch (escaped) {
                case 'n': value += '\n'; break;
                case 'r': value += '\r'; break;
                case 't': value += '\t'; break;
                case '0': value += '\0'; break;
                case '\\': value += '\\'; break;
                case '\'': value += '\''; break;
                case '"': value += '"'; break;
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
    t.start = start;
    t.end = makeLocation();
    addToken(t);
}

void Lexer::scanNumber() {
    std::string numStr;
    SourceLocation start = makeLocation();

    if (peekChar() == '0') {
        numStr += advanceChar();
        if (peekChar() == 'x' || peekChar() == 'X') {
            numStr += advanceChar();
            while (isHexDigit(peekChar())) {
                numStr += advanceChar();
            }
            double value = static_cast<double>(std::stoi(numStr.substr(2), nullptr, 16));
            Token t = makeNumber(value);
            t.start = start;
            t.end = makeLocation();
            addToken(t);
            return;
        }
    }

    while (isDigit(peekChar()) || peekChar() == '.') {
        if (peekChar() == '.') {
            if (numStr.find('.') != std::string::npos) break;
        }
        numStr += advanceChar();
    }

    if (peekChar() == 'e' || peekChar() == 'E') {
        numStr += advanceChar();
        if (peekChar() == '+' || peekChar() == '-') {
            numStr += advanceChar();
        }
        while (isDigit(peekChar())) {
            numStr += advanceChar();
        }
    }

    double value = std::stod(numStr);
    Token t = makeNumber(value);
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
