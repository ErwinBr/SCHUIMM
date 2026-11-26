#include "Script/Compiler/Lexer.h"

#include "Core/Common/StringBuilder.h"

#include <cmath>
#include <limits>
#include <locale>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace schuimm::script {

namespace {

const std::unordered_map<std::string_view, TokenKind>& keywords()
{
    static const std::unordered_map<std::string_view, TokenKind> table{
        {"func", TokenKind::KwFunc},     {"extern", TokenKind::KwExtern}, {"if", TokenKind::KwIf},
        {"elif", TokenKind::KwElif},     {"else", TokenKind::KwElse},     {"while", TokenKind::KwWhile},
        {"for", TokenKind::KwFor},       {"in", TokenKind::KwIn},         {"return", TokenKind::KwReturn},
        {"pass", TokenKind::KwPass},     {"and", TokenKind::KwAnd},       {"or", TokenKind::KwOr},
        {"not", TokenKind::KwNot},       {"True", TokenKind::KwTrue},     {"False", TokenKind::KwFalse},
        {"None", TokenKind::KwNone},     {"object", TokenKind::KwObject}, {"self", TokenKind::KwSelf},
        {"var", TokenKind::KwVar},       {"enum", TokenKind::KwEnum},     {"spawn", TokenKind::KwSpawn},
    };
    return table;
}

bool isIdentifierStart(char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool isDigit(char c) noexcept { return c >= '0' && c <= '9'; }
bool isIdentifierPart(char c) noexcept { return isIdentifierStart(c) || isDigit(c); }

// Length of the well-formed UTF-8 sequence starting at text[i], or 0.
std::size_t utf8SequenceLength(std::string_view text, std::size_t i) noexcept
{
    const auto lead = static_cast<unsigned char>(text[i]);
    std::size_t length = 0;
    char32_t codepoint = 0;
    char32_t minimum = 0;
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) {
        length = 2; codepoint = lead & 0x1F; minimum = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3; codepoint = lead & 0x0F; minimum = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4; codepoint = lead & 0x07; minimum = 0x10000;
    } else {
        return 0;
    }
    if (text.size() - i < length) return 0;
    for (std::size_t k = 1; k < length; ++k) {
        const auto c = static_cast<unsigned char>(text[i + k]);
        if ((c & 0xC0) != 0x80) return 0;
        codepoint = (codepoint << 6) | (c & 0x3F);
    }
    if (codepoint < minimum || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) return 0;
    return length;
}

class Lexer {
public:
    Lexer(const SourceFile& source, Diagnostics& diagnostics)
        : source_(source)
        , text_(source.text())
        , diagnostics_(diagnostics)
    {
    }

    std::vector<Token> run()
    {
        while (true) {
            if (atLineStart_ && parenDepth_ == 0) {
                handleIndentation();
                atLineStart_ = false;
            }
            if (atEnd()) {
                break;
            }
            const char c = peek();
            if (c == ' ') {
                advance();
            } else if (c == '\t') {
                const SourceLocation at = location();
                const bool leading = onlyWhitespaceBefore();
                advance();
                // Inside parentheses a continuation line may be indented with tabs as well.
                if (!(parenDepth_ > 0 && leading)) {
                    error(at, 1, "Tab characters are only allowed for indentation at the start of a line.");
                }
            } else if (c == '#') {
                skipComment();
            } else if (c == '\n' || c == '\r') {
                const SourceLocation at = location();
                consumeNewline();
                if (parenDepth_ == 0) {
                    emitNewline(at);
                    atLineStart_ = true;
                }
            } else {
                lexToken();
            }
        }
        const SourceLocation end = location();
        for (const SourceLocation& open : openParens_) {
            error(open, 1, "Unclosed '('.");
        }
        emitNewline(end);
        for (; indentLevel_ > 0; --indentLevel_) {
            emit(TokenKind::Dedent, end, end.offset);
        }
        emit(TokenKind::EndOfFile, end, end.offset);
        return std::move(tokens_);
    }

private:
    [[nodiscard]] bool atEnd() const noexcept { return position_ >= text_.size(); }
    [[nodiscard]] char peek(std::size_t ahead = 0) const noexcept
    {
        return position_ + ahead < text_.size() ? text_[position_ + ahead] : '\0';
    }
    [[nodiscard]] SourceLocation location() const noexcept
    {
        return {static_cast<std::uint32_t>(position_), line_, static_cast<std::uint32_t>(position_ - lineStart_ + 1)};
    }
    void advance() noexcept { ++position_; }

    [[nodiscard]] bool onlyWhitespaceBefore() const noexcept
    {
        for (std::size_t i = lineStart_; i < position_; ++i) {
            if (text_[i] != ' ' && text_[i] != '\t') return false;
        }
        return true;
    }

    void consumeNewline() noexcept
    {
        if (peek() == '\r' && peek(1) == '\n') {
            ++position_;
        }
        ++position_;
        ++line_;
        lineStart_ = position_;
    }

    void error(SourceLocation at, std::size_t length, std::string message, std::vector<std::string> notes = {})
    {
        SourceRange range{source_.id(), at, static_cast<std::uint32_t>(at.offset + length)};
        diagnostics_.error(DiagnosticPhase::Lexer, range, std::move(message), std::move(notes));
    }

    Token& emit(TokenKind kind, SourceLocation begin, std::size_t endOffset)
    {
        Token token;
        token.kind = kind;
        token.range = {source_.id(), begin, static_cast<std::uint32_t>(endOffset)};
        if (endOffset > begin.offset && kind != TokenKind::Indent) {
            token.lexeme = std::string_view(text_).substr(begin.offset, endOffset - begin.offset);
        }
        tokens_.push_back(std::move(token));
        return tokens_.back();
    }

    void emitNewline(SourceLocation at)
    {
        // A Newline ends a logical line; none is needed before the first token or after another
        // Newline (or after the Dedents that follow one).
        if (tokens_.empty()) return;
        const TokenKind last = tokens_.back().kind;
        if (last == TokenKind::Newline || last == TokenKind::Dedent || last == TokenKind::Indent) return;
        emit(TokenKind::Newline, at, at.offset);
    }

    void skipComment() noexcept
    {
        while (!atEnd() && peek() != '\n' && peek() != '\r') {
            advance();
        }
    }

    // At the start of a physical line: skips blank and comment-only lines and emits
    // Indent/Dedent tokens for the first line with content.
    void handleIndentation()
    {
        while (!atEnd()) {
            const SourceLocation start = location();
            std::uint32_t tabs = 0;
            bool sawSpace = false;
            bool sawTab = false;
            SourceLocation firstSpace{};
            while (peek() == '\t' || peek() == ' ') {
                if (peek() == '\t') {
                    ++tabs;
                    sawTab = true;
                } else if (!sawSpace) {
                    sawSpace = true;
                    firstSpace = location();
                }
                advance();
            }
            const char c = peek();
            if (atEnd() || c == '\n' || c == '\r' || c == '#') {
                skipComment();
                if (!atEnd()) {
                    consumeNewline();
                }
                continue; // Blank or comment-only line: indentation is irrelevant.
            }
            std::uint32_t level = tabs;
            if (sawSpace) {
                if (sawTab) {
                    error(firstSpace, 1, "Inconsistent indentation: tabs and spaces are mixed.",
                          {"Indent blocks with tabs only; one tab is one level."});
                } else {
                    error(firstSpace, 1, "Indentation must use tabs, not spaces.",
                          {"Indent blocks with tabs only; one tab is one level."});
                }
                level = indentLevel_; // Recovery: keep the current block.
            } else if (level > indentLevel_ + 1) {
                error(start, level, "Unexpected indentation: the line is indented by more than one level.");
            }
            while (indentLevel_ < level) {
                ++indentLevel_;
                emit(TokenKind::Indent, start, position_);
            }
            while (indentLevel_ > level) {
                --indentLevel_;
                emit(TokenKind::Dedent, location(), position_);
            }
            return;
        }
    }

    void lexToken()
    {
        const SourceLocation start = location();
        const char c = peek();
        if (isIdentifierStart(c)) {
            while (isIdentifierPart(peek())) {
                advance();
            }
            const std::string_view word = std::string_view(text_).substr(start.offset, position_ - start.offset);
            const auto keyword = keywords().find(word);
            emit(keyword != keywords().end() ? keyword->second : TokenKind::Identifier, start, position_);
            return;
        }
        if (isDigit(c)) {
            lexNumber(start);
            return;
        }
        if (c == '"' || c == '\'') {
            lexString(start, c);
            return;
        }
        advance();
        const auto two = [&](char next, TokenKind ifNext, TokenKind otherwise) {
            if (peek() == next) {
                advance();
                emit(ifNext, start, position_);
            } else {
                emit(otherwise, start, position_);
            }
        };
        switch (c) {
        case '(':
            ++parenDepth_;
            openParens_.push_back(start);
            emit(TokenKind::LeftParen, start, position_);
            return;
        case ')':
            if (parenDepth_ == 0) {
                error(start, 1, "Unmatched ')'.");
                emit(TokenKind::Invalid, start, position_);
                return;
            }
            --parenDepth_;
            openParens_.pop_back();
            emit(TokenKind::RightParen, start, position_);
            return;
        case ',': emit(TokenKind::Comma, start, position_); return;
        case '.': emit(TokenKind::Dot, start, position_); return;
        case ':': emit(TokenKind::Colon, start, position_); return;
        case '+': emit(TokenKind::Plus, start, position_); return;
        case '-': two('>', TokenKind::Arrow, TokenKind::Minus); return;
        case '*': emit(TokenKind::Star, start, position_); return;
        case '/': two('/', TokenKind::SlashSlash, TokenKind::Slash); return;
        case '%': emit(TokenKind::Percent, start, position_); return;
        case '=': two('=', TokenKind::EqualEqual, TokenKind::Assign); return;
        case '<': two('=', TokenKind::LessEqual, TokenKind::Less); return;
        case '>': two('=', TokenKind::GreaterEqual, TokenKind::Greater); return;
        case '!':
            if (peek() == '=') {
                advance();
                emit(TokenKind::NotEqual, start, position_);
                return;
            }
            error(start, 1, "Unexpected character '!'.", {"Use 'not' for logical negation."});
            emit(TokenKind::Invalid, start, position_);
            return;
        default: break;
        }
        // Invalid character: report it once (a whole UTF-8 sequence counts as one character).
        --position_;
        std::size_t length = utf8SequenceLength(text_, position_);
        std::vector<std::string> notes;
        std::string shown;
        if (length == 0) {
            length = 1;
            error(start, 1, "Invalid UTF-8 byte in the source file.");
        } else {
            shown = text_.substr(position_, length);
            if (c == ';') {
                notes.push_back("Statements end at the end of the line; semicolons are not used.");
            }
            error(start, length, concat("Unexpected character '", shown, "'."), std::move(notes));
        }
        position_ += length;
        emit(TokenKind::Invalid, start, position_);
    }

    void lexNumber(SourceLocation start)
    {
        while (isDigit(peek())) {
            advance();
        }
        bool isFloat = false;
        if (peek() == '.') {
            if (isDigit(peek(1))) {
                isFloat = true;
                advance();
                while (isDigit(peek())) {
                    advance();
                }
            } else {
                advance();
                error(start, position_ - start.offset, "Invalid number: a digit must follow the decimal point.",
                      {"Write '1.0' instead of '1.'."});
                emit(TokenKind::Invalid, start, position_);
                return;
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            const std::size_t mark = position_;
            advance();
            if (peek() == '+' || peek() == '-') {
                advance();
            }
            if (!isDigit(peek())) {
                position_ = mark + 1;
                error(start, position_ - start.offset, "Invalid number: the exponent has no digits.");
                emit(TokenKind::Invalid, start, position_);
                return;
            }
            isFloat = true;
            while (isDigit(peek())) {
                advance();
            }
        }
        if (isIdentifierPart(peek())) {
            while (isIdentifierPart(peek())) {
                advance();
            }
            error(start, position_ - start.offset, "Invalid number literal.");
            emit(TokenKind::Invalid, start, position_);
            return;
        }
        const std::string_view digits = std::string_view(text_).substr(start.offset, position_ - start.offset);
        if (isFloat) {
            std::istringstream stream{std::string(digits)};
            stream.imbue(std::locale::classic());
            double value = 0.0;
            stream >> value;
            if (stream.fail() || !std::isfinite(value)) {
                error(start, digits.size(), "Float literal is out of range.");
                emit(TokenKind::Invalid, start, position_);
                return;
            }
            emit(TokenKind::Float, start, position_).floatValue = value;
            return;
        }
        if (digits.size() > 1 && digits[0] == '0') {
            error(start, digits.size(), "Leading zeros are not allowed in integer literals.");
            emit(TokenKind::Invalid, start, position_);
            return;
        }
        constexpr std::uint64_t limit = 9223372036854775808ull; // 2^63
        std::uint64_t value = 0;
        bool tooLarge = false;
        for (const char d : digits) {
            const auto digit = static_cast<std::uint64_t>(d - '0');
            if (value > (limit - digit) / 10) {
                tooLarge = true;
                break;
            }
            value = value * 10 + digit;
        }
        if (tooLarge) {
            error(start, digits.size(), "Integer literal is too large for int (64-bit).");
            emit(TokenKind::Invalid, start, position_);
            return;
        }
        Token& token = emit(TokenKind::Integer, start, position_);
        if (value == limit) {
            token.integerIsMinMagnitude = true;
            token.intValue = std::numeric_limits<std::int64_t>::min();
        } else {
            token.intValue = static_cast<std::int64_t>(value);
        }
    }

    static int hexValue(char c) noexcept
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    void lexString(SourceLocation start, char quote)
    {
        advance();
        std::string value;
        bool valid = true;
        while (true) {
            if (atEnd() || peek() == '\n' || peek() == '\r') {
                error(start, position_ - start.offset, "Unterminated string literal.",
                      {"Strings must end on the line they start."});
                valid = false;
                break;
            }
            const char c = peek();
            if (c == quote) {
                advance();
                break;
            }
            if (c == '\\') {
                const SourceLocation escapeAt = location();
                advance();
                const char e = peek();
                switch (e) {
                case 'n': value += '\n'; advance(); continue;
                case 't': value += '\t'; advance(); continue;
                case 'r': value += '\r'; advance(); continue;
                case '0': value += '\0'; advance(); continue;
                case '\\': value += '\\'; advance(); continue;
                case '\'': value += '\''; advance(); continue;
                case '"': value += '"'; advance(); continue;
                case 'x': {
                    const int high = hexValue(peek(1));
                    const int low = hexValue(peek(2));
                    if (high < 0 || low < 0) {
                        advance();
                        error(escapeAt, 2, "Invalid \\x escape: two hexadecimal digits are required.");
                        valid = false;
                        continue;
                    }
                    const int byte = high * 16 + low;
                    position_ += 3;
                    if (byte > 0x7F) {
                        error(escapeAt, 4, "\\x escapes are limited to ASCII (\\x00 to \\x7F).",
                              {"Write non-ASCII characters directly; source files are UTF-8."});
                        valid = false;
                        continue;
                    }
                    value += static_cast<char>(byte);
                    continue;
                }
                default:
                    if (atEnd() || e == '\n' || e == '\r') {
                        continue; // Reported as unterminated.
                    }
                    advance();
                    error(escapeAt, 2, concat("Unknown escape sequence '\\", e, "'."),
                          {"Supported escapes: \\n \\t \\r \\0 \\\\ \\' \\\" \\xHH."});
                    valid = false;
                    continue;
                }
            }
            const std::size_t length = utf8SequenceLength(text_, position_);
            if (length == 0) {
                error(location(), 1, "Invalid UTF-8 byte in string literal.");
                valid = false;
                advance();
                continue;
            }
            value.append(text_, position_, length);
            position_ += length;
        }
        Token& token = emit(valid ? TokenKind::String : TokenKind::Invalid, start, position_);
        token.stringValue = std::move(value);
    }

    const SourceFile& source_;
    const std::string& text_;
    Diagnostics& diagnostics_;
    std::vector<Token> tokens_;
    std::vector<SourceLocation> openParens_;
    std::size_t position_ = 0;
    std::size_t lineStart_ = 0;
    std::uint32_t line_ = 1;
    std::uint32_t indentLevel_ = 0;
    int parenDepth_ = 0;
    bool atLineStart_ = true;
};

} // namespace

std::vector<Token> tokenize(const SourceFile& source, Diagnostics& diagnostics)
{
    return Lexer(source, diagnostics).run();
}

} // namespace schuimm::script
