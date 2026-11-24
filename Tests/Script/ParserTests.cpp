#include "ScriptTestSupport.h"

#include "Script/Compiler/AstPrinter.h"
#include "Script/Compiler/Lexer.h"
#include "Script/Compiler/Parser.h"

using namespace schuimm;
using namespace schuimm::script;
using namespace schuimm::script::test;

namespace {

struct Parsed {
    std::unique_ptr<SourceFile> source;
    Diagnostics diagnostics;
    std::vector<Token> tokens;
    std::unique_ptr<SourceUnit> unit;

    [[nodiscard]] bool hasError(std::string_view text, std::uint32_t line = 0) const
    {
        for (const Diagnostic& d : diagnostics.all()) {
            if (d.message.find(text) != std::string::npos && (line == 0 || d.range.begin.line == line)) return true;
        }
        return false;
    }
};

Parsed parseSource(std::string text)
{
    Parsed parsed;
    parsed.source = std::make_unique<SourceFile>("parse.sch", std::move(text));
    parsed.tokens = tokenize(*parsed.source, parsed.diagnostics);
    parsed.unit = parse(parsed.tokens, parsed.diagnostics);
    return parsed;
}

// The expression of "func f():\n\tx = <expression>".
std::string expressionOf(const std::string& expression)
{
    const Parsed p = parseSource("func f():\n\tx = " + expression + "\n");
    if (p.diagnostics.hasErrors() || p.unit->functions.empty()) return "<parse error>";
    return formatExpression(*p.unit->functions[0]->body->statements[0]->value);
}

} // namespace

SCHUIMM_TEST(Parser_FunctionDeclarations)
{
    const Parsed p = parseSource("func main():\n\tpass\n\nfunc damage(amount: int, scale: float) -> float:\n\treturn amount * scale\n"
                                 "extern func play_sound(name: string, volume: float)\nextern func now() -> float\n");
    REQUIRE(!p.diagnostics.hasErrors());
    REQUIRE(p.unit->functions.size() == 4);
    const FunctionDecl& damage = *p.unit->functions[1];
    CHECK_EQ(damage.name, std::string("damage"));
    REQUIRE(damage.parameters.size() == 2);
    CHECK(damage.parameters[0].type == Type::Int);
    CHECK(damage.parameters[1].type == Type::Float);
    CHECK(damage.returnType == Type::Float);
    CHECK(!damage.isExtern);
    CHECK(p.unit->functions[0]->returnType == Type::None);
    CHECK(p.unit->functions[2]->isExtern);
    CHECK(p.unit->functions[2]->body == nullptr);
    CHECK(p.unit->functions[3]->returnType == Type::Float);
}

SCHUIMM_TEST(Parser_PrecedenceAndAssociativity)
{
    CHECK_EQ(expressionOf("1 + 2 * 3"), std::string("(1 + (2 * 3))"));
    CHECK_EQ(expressionOf("1 - 2 - 3"), std::string("((1 - 2) - 3)"));
    CHECK_EQ(expressionOf("8 // 2 % 3 / 1"), std::string("(((8 // 2) % 3) / 1)"));
    CHECK_EQ(expressionOf("-a * b"), std::string("((-a) * b)"));
    CHECK_EQ(expressionOf("- -a"), std::string("(-(-a))"));
    CHECK_EQ(expressionOf("a + b < c * d"), std::string("((a + b) < (c * d))"));
    CHECK_EQ(expressionOf("not a == b"), std::string("(not (a == b))"));
    CHECK_EQ(expressionOf("a or b and c"), std::string("(a or (b and c))"));
    CHECK_EQ(expressionOf("not a and not b or c"), std::string("(((not a) and (not b)) or c)"));
    CHECK_EQ(expressionOf("(1 + 2) * 3"), std::string("((1 + 2) * 3)"));
    CHECK_EQ(expressionOf("f(1, g(2) + 3)"), std::string("f(1, (g(2) + 3))"));
    CHECK_EQ(expressionOf("-9223372036854775808"), std::string("-9223372036854775808"));
    CHECK_EQ(expressionOf("True and None == None"), std::string("(True and (None == None))"));
}

SCHUIMM_TEST(Parser_ChainedComparisonsAreRejected)
{
    const Parsed p = parseSource("func f():\n\tx = a < b < c\n");
    CHECK(p.hasError("cannot be chained", 2));
}

SCHUIMM_TEST(Parser_StatementsAndNestedBlocks)
{
    const Parsed p = parseSource(
        "func main():\n"
        "\tcount: int = 0\n"
        "\tif count == 0:\n"
        "\t\tprint(\"zero\")\n"
        "\telif count > 0:\n"
        "\t\twhile count < 10:\n"
        "\t\t\tcount = count + 1\n"
        "\telse:\n"
        "\t\tpass\n"
        "\tfor i in range(1, 10, 2):\n"
        "\t\tprint(i)\n"
        "\treturn\n");
    REQUIRE(!p.diagnostics.hasErrors());
    const Block& body = *p.unit->functions[0]->body;
    REQUIRE(body.statements.size() == 4);
    CHECK(body.statements[0]->kind == StmtKind::Assign);
    CHECK(body.statements[0]->annotation == std::optional<Type>(Type::Int));
    const Stmt& ifStatement = *body.statements[1];
    CHECK(ifStatement.kind == StmtKind::If);
    CHECK_EQ(ifStatement.branches.size(), std::size_t{2});
    REQUIRE(ifStatement.elseBlock != nullptr);
    CHECK(ifStatement.branches[1].body->statements[0]->kind == StmtKind::While);
    const Stmt& forStatement = *body.statements[2];
    CHECK(forStatement.kind == StmtKind::For);
    CHECK_EQ(forStatement.name, std::string("i"));
    CHECK_EQ(forStatement.rangeArguments.size(), std::size_t{3});
    CHECK(body.statements[3]->kind == StmtKind::Return);
    CHECK(body.statements[3]->value == nullptr);
    const std::string dump = dumpAst(*p.unit);
    CHECK(dump.find("For i in range") != std::string::npos);
    CHECK(dump.find("Elif") != std::string::npos);
}

SCHUIMM_TEST(Parser_SyntaxErrorsAreReportedWithRecovery)
{
    // Independent errors in one file: the parser resynchronises after each.
    const Parsed p = parseSource(
        "func main():\n"
        "\ty = 1 +\n"
        "\tz = 2\n"
        "\tif z:\n"
        "\tprint(z)\n"
        "\tw = = 3\n"
        "func broken(a):\n"
        "\tpass\n"
        "func fine():\n"
        "\tpass\n"
        "func last():\n"
        "\tx = (1 +\n");
    CHECK(p.hasError("Expected an expression before the end of the line", 2));
    CHECK(p.hasError("Expected an indented block", 5));
    CHECK(p.hasError("Expected an expression, found '='", 6));
    CHECK(p.hasError("needs a type annotation", 7));
    CHECK(p.hasError("Unclosed '('", 12));
    CHECK(p.diagnostics.errorCount() >= 5);
    bool sawFine = false;
    for (const auto& function : p.unit->functions) sawFine = sawFine || function->name == "fine";
    CHECK(sawFine); // A well-formed function after errors is still parsed.
    // Statements after an error in the same block are parsed too.
    const Parsed q = parseSource("func a():\n\tx = )\n\ty = 2\n");
    REQUIRE(q.unit->functions.size() == 1);
    CHECK_EQ(q.unit->functions[0]->body->statements.size(), std::size_t{1});
}

SCHUIMM_TEST(Parser_MoreSyntaxErrors)
{
    CHECK(parseSource("x = 1\n").hasError("at the top level"));
    CHECK(parseSource("\tfunc f():\n\t\tpass\n").hasError("Unexpected indentation"));
    CHECK(parseSource("func f():\n\tpass\n\t\tpass\n").hasError("Unexpected indentation"));
    CHECK(parseSource("func f():\n\tpass\n\t\t\tpass\n").hasError("more than one level"));
    CHECK(parseSource("func f()\n\tpass\n").hasError("Expected ':'"));
    CHECK(parseSource("func f(): pass\n").hasError("Expected a new line"));
    CHECK(parseSource("func f():\n\telse:\n\t\tpass\n").hasError("without a matching 'if'"));
    CHECK(parseSource("func f():\n\tfor i in items:\n\t\tpass\n").hasError("iterates over range"));
    CHECK(parseSource("func f():\n\tfor i in range():\n\t\tpass\n").hasError("1 to 3 arguments"));
    CHECK(parseSource("func f():\n\tx: int\n").hasError("must be initialised"));
    CHECK(parseSource("func f():\n\tx: vector = 1\n").hasError("Unknown type 'vector'"));
    CHECK(parseSource("func f(a: None):\n\tpass\n").hasError("cannot have type None"));
    CHECK(parseSource("func f():\n\tf() = 3\n").hasError("Only a variable name"));
    CHECK(parseSource("func f():\n\tx = 1 2\n").hasError("Expected the end of the line"));
    CHECK(parseSource("func f():\n\tfunc g():\n\t\tpass\n").hasError("inside other functions"));
    CHECK(parseSource("extern func f():\n\tpass\n").hasError("no body"));
    CHECK(parseSource("func f():\n\tx = 9223372036854775808\n").hasError("too large"));
    CHECK(parseSource("func f():\n\tx = f()(1)\n").hasError("Only named functions"));
}
