#pragma once

#include <string>
#include <cstdint>

namespace as3 {

enum class TokenType {
    EndOfFile,
    Error,
    Identifier,
    String,
    Number,
    Boolean,
    Null,
    RegularExpression,
    LBrace,
    RBrace,
    LParen,
    RParen,
    LBracket,
    RBracket,
    SemiColon,
    Comma,
    Dot,
    DotDotDot,
    QuestionDot,
    Colon,
    DoubleColon,
    At,
    LessThan,
    GreaterThan,
    LessThanOrEqual,
    GreaterThanOrEqual,
    LeftShift,
    UnsignedLeftShift,
    RightShift,
    UnsignedRightShift,
    LeftShiftAssign,
    UnsignedLeftShiftAssign,
    RightShiftAssign,
    RightTrippleShiftAssign,
    UnsignedRightShiftAssign,
    Equals,
    NotEquals,
    StrictEquals,
    StrictNotEquals,
    Plus,
    Minus,
    Multiply,
    Divide,
    Remainder,
    PlusAssign,
    MinusAssign,
    MultiplyAssign,
    DivideAssign,
    RemainderAssign,
    Power,
    PowerAssign,
    NullCoalescing,
    NullCoalescingAssign,
    Ampersand,
    Pipe,
    Caret,
    Tilde,
    AmpersandAssign,
    PipeAssign,
    CaretAssign,
    TildeAssign,
    And,
    Or,
    AndAssign,
    OrAssign,
    LogicalAnd,
    LogicalOr,
    LogicalXor,
    Question,
    Exclamation,
    Assign,
    Increment,
    Decrement,
    PreIncrement,
    PreDecrement,
    Keyword,
};

enum class KeywordType {
    None,
    As,
    Break,
    Case,
    Catch,
    Class,
    Const,
    Continue,
    Default,
    Delete,
    Do,
    Else,
    Extends,
    False,
    Finally,
    For,
    Function,
    Get,
    If,
    Implements,
    Import,
    In,
    Instanceof,
    Interface,
    Internal,
    Is,
    Let,
    Native,
    New,
    Null,
    Of,
    Package,
    Private,
    Protected,
    Public,
    Return,
    Set,
    Static,
    Super,
    Switch,
    This,
    Throw,
    True,
    Try,
    Typeof,
    Use,
    Var,
    Void,
    While,
    With,
    Yield,
    Async,
    Await,
    Each,
    Enum,
    Namespace,
    Override,
    Dynamic,
    Final,
};

struct SourceLocation {
    uint32_t line;
    uint32_t column;
    uint32_t offset;
};

struct Token {
    TokenType type;
    KeywordType keyword;
    std::string text;
    std::string stringValue;
    double numericValue;
    SourceLocation start;
    SourceLocation end;
    // A line break comes before it, where a statement may end without a
    // semicolon.
    bool newLineBefore = false;

    Token()
        : type(TokenType::EndOfFile)
        , keyword(KeywordType::None)
        , numericValue(0) {
    }
};

} // namespace as3

