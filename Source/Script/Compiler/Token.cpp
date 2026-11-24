#include "Script/Compiler/Token.h"

#include "Core/Common/StringBuilder.h"
#include "Script/Bytecode/Disassembler.h"
#include "Script/Bytecode/Semantics.h"

namespace schuimm::script {

const char* toString(TokenKind kind) noexcept
{
    switch (kind) {
#define SCHUIMM_X(name, text)                                                                       \
    case TokenKind::name: return #name;
        SCHUIMM_SCRIPT_TOKENS(SCHUIMM_X)
#undef SCHUIMM_X
    }
    return "?";
}

const char* describe(TokenKind kind) noexcept
{
    switch (kind) {
#define SCHUIMM_X(name, text)                                                                       \
    case TokenKind::name: return text;
        SCHUIMM_SCRIPT_TOKENS(SCHUIMM_X)
#undef SCHUIMM_X
    }
    return "?";
}

std::string dumpTokens(const std::vector<Token>& tokens)
{
    std::string out;
    for (const Token& token : tokens) {
        out += concat(token.range.begin.line, ":", token.range.begin.column, " ", toString(token.kind));
        switch (token.kind) {
        case TokenKind::Identifier: out += concat(" ", token.lexeme); break;
        case TokenKind::Integer:
            out += " " + (token.integerIsMinMagnitude ? std::string("9223372036854775808") : formatInt(token.intValue));
            break;
        case TokenKind::Float: out += " " + formatFloat(token.floatValue); break;
        case TokenKind::String: out += " " + quoteString(token.stringValue); break;
        default: break;
        }
        out += "\n";
    }
    return out;
}

} // namespace schuimm::script
