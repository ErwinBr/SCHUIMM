#pragma once

#include "Script/Compiler/SourceFile.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace schuimm::script {

#define SCHUIMM_SCRIPT_TOKENS(X)                                                                    \
    X(EndOfFile, "end of file")                                                                     \
    X(Newline, "newline")                                                                           \
    X(Indent, "indent")                                                                             \
    X(Dedent, "dedent")                                                                             \
    X(Identifier, "identifier")                                                                     \
    X(Integer, "integer")                                                                           \
    X(Float, "float")                                                                               \
    X(String, "string")                                                                             \
    X(KwFunc, "'func'")                                                                             \
    X(KwExtern, "'extern'")                                                                         \
    X(KwIf, "'if'")                                                                                 \
    X(KwElif, "'elif'")                                                                             \
    X(KwElse, "'else'")                                                                             \
    X(KwWhile, "'while'")                                                                           \
    X(KwFor, "'for'")                                                                               \
    X(KwIn, "'in'")                                                                                 \
    X(KwReturn, "'return'")                                                                         \
    X(KwPass, "'pass'")                                                                             \
    X(KwAnd, "'and'")                                                                               \
    X(KwOr, "'or'")                                                                                 \
    X(KwNot, "'not'")                                                                               \
    X(KwTrue, "'True'")                                                                             \
    X(KwFalse, "'False'")                                                                           \
    X(KwNone, "'None'")                                                                             \
    X(KwObject, "'object'")                                                                         \
    X(KwSelf, "'self'")                                                                             \
    X(KwVar, "'var'")                                                                               \
    X(KwEnum, "'enum'")                                                                             \
    X(KwSpawn, "'spawn'")                                                                           \
    X(Dot, "'.'")                                                                                   \
    X(LeftParen, "'('")                                                                             \
    X(RightParen, "')'")                                                                            \
    X(Comma, "','")                                                                                 \
    X(Colon, "':'")                                                                                 \
    X(Arrow, "'->'")                                                                                \
    X(Plus, "'+'")                                                                                  \
    X(Minus, "'-'")                                                                                 \
    X(Star, "'*'")                                                                                  \
    X(Slash, "'/'")                                                                                 \
    X(SlashSlash, "'//'")                                                                           \
    X(Percent, "'%'")                                                                               \
    X(Assign, "'='")                                                                                \
    X(PlusAssign, "'+='")                                                                           \
    X(MinusAssign, "'-='")                                                                          \
    X(StarAssign, "'*='")                                                                           \
    X(SlashAssign, "'/='")                                                                          \
    X(EqualEqual, "'=='")                                                                           \
    X(NotEqual, "'!='")                                                                             \
    X(Less, "'<'")                                                                                  \
    X(LessEqual, "'<='")                                                                            \
    X(Greater, "'>'")                                                                               \
    X(GreaterEqual, "'>='")                                                                         \
    X(Invalid, "invalid token")

enum class TokenKind : std::uint8_t {
#define SCHUIMM_X(name, text) name,
    SCHUIMM_SCRIPT_TOKENS(SCHUIMM_X)
#undef SCHUIMM_X
};

// Kind name for dumps ("KwIf") and the human-readable spelling for diagnostics ("'if'").
[[nodiscard]] const char* toString(TokenKind kind) noexcept;
[[nodiscard]] const char* describe(TokenKind kind) noexcept;

struct Token {
    TokenKind kind = TokenKind::EndOfFile;
    SourceRange range;
    std::string_view lexeme;   // The source text (empty for Indent/Dedent/EndOfFile).
    std::string stringValue;   // String: the decoded value.
    std::int64_t intValue = 0; // Integer.
    double floatValue = 0.0;   // Float.
    // Integer literal 9223372036854775808 (= 2^63): only valid as the operand of unary minus.
    bool integerIsMinMagnitude = false;
};

// One token per line: "3:5 Identifier 'actor_name'".
[[nodiscard]] std::string dumpTokens(const std::vector<Token>& tokens);

} // namespace schuimm::script
