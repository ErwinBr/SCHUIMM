#include "ScriptTestSupport.h"

#include "Script/Compiler/Lexer.h"

using namespace schuimm;
using namespace schuimm::script;
using namespace schuimm::script::test;

namespace {

struct Lexed {
    std::unique_ptr<SourceFile> source;
    Diagnostics diagnostics;
    std::vector<Token> tokens;

    [[nodiscard]] std::vector<TokenKind> kinds() const
    {
        std::vector<TokenKind> result;
        for (const Token& token : tokens) result.push_back(token.kind);
        return result;
    }
    [[nodiscard]] bool hasError(std::string_view text) const
    {
        for (const Diagnostic& d : diagnostics.all()) {
            if (d.message.find(text) != std::string::npos) return true;
        }
        return false;
    }
};

Lexed lex(std::string text)
{
    Lexed lexed;
    lexed.source = std::make_unique<SourceFile>("lex.sch", std::move(text));
    lexed.tokens = tokenize(*lexed.source, lexed.diagnostics);
    return lexed;
}

using K = TokenKind;

} // namespace

SCHUIMM_TEST(Lexer_KeywordsIdentifiersAndOperators)
{
    const Lexed l = lex("func if elif else while for in return pass and or not True False None extern my_var _x9 "
                        "( ) , : -> + - * / // % = == != < <= > >=");
    CHECK(!l.diagnostics.hasErrors());
    const std::vector<K> expected{K::KwFunc, K::KwIf, K::KwElif, K::KwElse, K::KwWhile, K::KwFor, K::KwIn,
                                  K::KwReturn, K::KwPass, K::KwAnd, K::KwOr, K::KwNot, K::KwTrue, K::KwFalse,
                                  K::KwNone, K::KwExtern, K::Identifier, K::Identifier, K::LeftParen, K::RightParen,
                                  K::Comma, K::Colon, K::Arrow, K::Plus, K::Minus, K::Star, K::Slash, K::SlashSlash,
                                  K::Percent, K::Assign, K::EqualEqual, K::NotEqual, K::Less, K::LessEqual,
                                  K::Greater, K::GreaterEqual, K::Newline, K::EndOfFile};
    CHECK(l.kinds() == expected);
    CHECK_EQ(l.tokens[16].lexeme, std::string_view("my_var"));
    // "true"/"range" are not keywords.
    const Lexed l2 = lex("true range");
    CHECK(l2.tokens[0].kind == K::Identifier);
    CHECK(l2.tokens[1].kind == K::Identifier);
}

SCHUIMM_TEST(Lexer_Numbers)
{
    const Lexed l = lex("0 42 9223372036854775807 3.5 1e3 2.5E-2 0.0");
    REQUIRE(!l.diagnostics.hasErrors());
    CHECK(l.tokens[0].kind == K::Integer);
    CHECK_EQ(l.tokens[1].intValue, std::int64_t{42});
    CHECK_EQ(l.tokens[2].intValue, std::int64_t{9223372036854775807});
    CHECK(l.tokens[3].kind == K::Float);
    CHECK_EQ(l.tokens[3].floatValue, 3.5);
    CHECK_EQ(l.tokens[4].floatValue, 1000.0);
    CHECK_EQ(l.tokens[5].floatValue, 0.025);
    // 2^63 is only valid after a minus sign; the lexer flags it for the parser.
    const Lexed big = lex("9223372036854775808");
    CHECK(!big.diagnostics.hasErrors());
    CHECK(big.tokens[0].integerIsMinMagnitude);
    CHECK(lex("9223372036854775809").hasError("too large"));
    CHECK(lex("007").hasError("Leading zeros"));
    CHECK(lex("1.").hasError("digit must follow"));
    CHECK(lex("1e").hasError("exponent"));
    CHECK(lex("12abc").hasError("Invalid number"));
    CHECK(lex("1e999").hasError("out of range"));
}

SCHUIMM_TEST(Lexer_StringsAndEscapes)
{
    const Lexed l = lex(R"("a\tb\n" 'it\'s' "q\"q" "\\" "\x41\0" "héllo")");
    REQUIRE(!l.diagnostics.hasErrors());
    CHECK_EQ(l.tokens[0].stringValue, std::string("a\tb\n"));
    CHECK_EQ(l.tokens[1].stringValue, std::string("it's"));
    CHECK_EQ(l.tokens[2].stringValue, std::string("q\"q"));
    CHECK_EQ(l.tokens[3].stringValue, std::string("\\"));
    CHECK_EQ(l.tokens[4].stringValue, std::string("A\0", 2));
    CHECK_EQ(l.tokens[5].stringValue, std::string("h\xC3\xA9llo"));
    CHECK(lex("\"abc").hasError("Unterminated"));
    CHECK(lex("\"abc\ndef\"").hasError("Unterminated"));
    // No raw string literals inside CHECK(): MSVC re-scans the stringized macro argument and
    // rejects escapes such as \q in it (C4129, C2153).
    CHECK(lex("\"\\q\"").hasError("Unknown escape"));
    CHECK(lex("\"\\xZZ\"").hasError("two hexadecimal"));
    CHECK(lex("\"\\xFF\"").hasError("ASCII"));
    CHECK(lex("\"\xFF\"").hasError("Invalid UTF-8"));
}

SCHUIMM_TEST(Lexer_CommentsAndBlankLines)
{
    const Lexed l = lex("# leading comment\n\nfunc main():  # trailing\n\n\t# indented comment\n\tpass\n\n\n# end\n");
    REQUIRE(!l.diagnostics.hasErrors());
    const std::vector<K> expected{K::KwFunc, K::Identifier, K::LeftParen, K::RightParen, K::Colon, K::Newline,
                                  K::Indent, K::KwPass, K::Newline, K::Dedent, K::EndOfFile};
    CHECK(l.kinds() == expected);
    CHECK_EQ(l.tokens[0].range.begin.line, std::uint32_t{3});
    CHECK_EQ(l.tokens[7].range.begin.line, std::uint32_t{6});
    CHECK_EQ(l.tokens[7].range.begin.column, std::uint32_t{2});
}

SCHUIMM_TEST(Lexer_NestedBlocksAndDedentAtEndOfFile)
{
    // No final newline, three levels open at the end of the file.
    const Lexed l = lex("func f():\n\tif a:\n\t\twhile b:\n\t\t\tpass\n\tpass\nfunc g():\n\tif x:\n\t\tif y:\n\t\t\tpass");
    REQUIRE(!l.diagnostics.hasErrors());
    int indents = 0;
    int dedents = 0;
    for (const Token& t : l.tokens) {
        indents += t.kind == K::Indent ? 1 : 0;
        dedents += t.kind == K::Dedent ? 1 : 0;
    }
    CHECK_EQ(indents, 6);
    CHECK_EQ(dedents, 6);
    const std::size_t n = l.tokens.size();
    CHECK(l.tokens[n - 5].kind == K::Newline);
    CHECK(l.tokens[n - 4].kind == K::Dedent);
    CHECK(l.tokens[n - 2].kind == K::Dedent);
    CHECK(l.tokens[n - 1].kind == K::EndOfFile);
}

SCHUIMM_TEST(Lexer_IndentationErrors)
{
    CHECK(lex("func f():\n    pass\n").hasError("must use tabs"));
    CHECK(lex("func f():\n\t pass\n").hasError("tabs and spaces are mixed"));
    CHECK(lex("func f():\n \tpass\n").hasError("tabs and spaces are mixed"));
    CHECK(lex("func f():\n\t\tpass\n").hasError("more than one level"));
    CHECK(lex("x = 1\tbad\n").hasError("only allowed for indentation"));
    // A space-indented blank or comment-only line is not an error.
    CHECK(!lex("func f():\n\tpass\n   \n  # note\n").diagnostics.hasErrors());
}

SCHUIMM_TEST(Lexer_ParenthesesJoinLines)
{
    const Lexed l = lex("func f():\n\tx = g(1,\n  2,\n\t\t\t3)\n\tpass\n");
    REQUIRE(!l.diagnostics.hasErrors());
    int newlines = 0;
    for (const Token& t : l.tokens) newlines += t.kind == K::Newline ? 1 : 0;
    CHECK_EQ(newlines, 3);
    CHECK(lex("x = (1\n").hasError("Unclosed"));
    CHECK(lex("x = 1)\n").hasError("Unmatched"));
}

SCHUIMM_TEST(Lexer_InvalidCharactersAreReportedAndSkipped)
{
    const Lexed l = lex("a ; b $ c ! d\n");
    CHECK_EQ(l.diagnostics.errorCount(), std::size_t{3});
    CHECK(l.hasError("Unexpected character ';'"));
    CHECK(l.hasError("Unexpected character '$'"));
    CHECK(l.hasError("Unexpected character '!'"));
    // Lexing continues: all four identifiers are present.
    int identifiers = 0;
    for (const Token& t : l.tokens) identifiers += t.kind == K::Identifier ? 1 : 0;
    CHECK_EQ(identifiers, 4);
    CHECK(lex("é = 1").hasError("Unexpected character 'é'"));
}

SCHUIMM_TEST(Lexer_SourceLocations)
{
    const Lexed l = lex("func main():\n\tname = \"Alex\"\n");
    const Token& name = l.tokens[7];
    REQUIRE(name.kind == K::Identifier);
    CHECK_EQ(name.range.begin.line, std::uint32_t{2});
    CHECK_EQ(name.range.begin.column, std::uint32_t{2});
    CHECK_EQ(name.range.begin.offset, std::uint32_t{14});
    CHECK_EQ(name.range.end, std::uint32_t{18});
    CHECK_EQ(l.tokens[9].range.begin.column, std::uint32_t{9});
    // \r\n line endings count as one line break.
    const Lexed crlf = lex("a\r\nb\r\n");
    CHECK_EQ(crlf.tokens[2].range.begin.line, std::uint32_t{2});
    CHECK(dumpTokens(l.tokens).find("2:2 Identifier name") != std::string::npos);
}
