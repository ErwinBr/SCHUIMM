#include "ScriptTestSupport.h"

#include "Script/Compiler/AstPrinter.h"

using namespace schuimm;
using namespace schuimm::script;
using namespace schuimm::script::test;

namespace {

Compiled check(const std::string& body, const std::string& extra = {})
{
    return compileSource(extra + "func main():\n" + body);
}

// The type of variable `name` in main().
Type variableType(const Compiled& c, const std::string& name)
{
    for (const FunctionSemantics& f : c.result.semantics.functions()) {
        if (f.declaration->name != "main") continue;
        for (const VariableInfo& v : f.variables) {
            if (v.name == name) return v.type;
        }
    }
    return Type::Error;
}

} // namespace

SCHUIMM_TEST(TypeChecker_InfersTypes)
{
    const Compiled c = check("\tvar size = 100\n\tvar speed = 3.5\n\tvar has_key = True\n\tvar actor_name = \"Alex\"\n\tvar mixed = size * speed\n"
                             "\tvar q = size / 2\n\tvar fd = size // 3\n\tvar label = \"n=\" + str(size)\n\tvar back = int(speed)\n");
    REQUIRE(c.result.success);
    CHECK(variableType(c, "size") == Type::Int);
    CHECK(variableType(c, "speed") == Type::Float);
    CHECK(variableType(c, "has_key") == Type::Bool);
    CHECK(variableType(c, "actor_name") == Type::String);
    CHECK(variableType(c, "mixed") == Type::Float);
    CHECK(variableType(c, "q") == Type::Float);
    CHECK(variableType(c, "fd") == Type::Int);
    CHECK(variableType(c, "label") == Type::String);
    CHECK(variableType(c, "back") == Type::Int);
    const std::string dump = dumpAst(*c.result.units[0].ast, &c.result.semantics);
    CHECK(dump.find("Name size : int -> float") != std::string::npos); // widened in size * speed
}

SCHUIMM_TEST(TypeChecker_ExplicitAnnotations)
{
    const Compiled ok = check("\tvar size: int = 100\n\tvar speed: float = 3\n\tspeed = 4\n\tvar name: string = \"x\"\n\tvar flag: bool = False\n");
    CHECK(ok.result.success);
    CHECK(variableType(ok, "speed") == Type::Float);
    const Compiled bad = check("\tvar count: int = 3.5\n");
    CHECK(bad.has("Cannot assign a value of type 'float' to variable 'count' of type 'int'", 2));
    const Compiled bad2 = check("\tvar name: string = 1\n");
    CHECK(bad2.has("Cannot assign a value of type 'int' to variable 'name' of type 'string'"));
}

SCHUIMM_TEST(TypeChecker_InvalidAssignments)
{
    const Compiled c = check("\tvar lives = 3\n\tlives = \"many\"\n");
    CHECK(c.has("Cannot assign a value of type 'string' to variable 'lives' of type 'int'", 3));
    CHECK(c.hasNote("declared as 'int' on line 2"));
    const Compiled narrowing = check("\tvar n = 1\n\tn = 2.5\n");
    CHECK(narrowing.has("type 'float' to variable 'n' of type 'int'"));
    const Compiled none = check("\tvar x = print(1)\n");
    CHECK(none.has("has no value"));
    const Compiled noneLiteral = check("\tvar x = None\n");
    CHECK(noneLiteral.has("Cannot assign a value of type 'None'"));
}

SCHUIMM_TEST(TypeChecker_UndefinedVariablesAndHints)
{
    const Compiled c = check("\tprint(player_speed)\n");
    CHECK(c.has("Undefined variable 'player_speed'.", 2));
    const Compiled b = check("\tif x == false:\n\t\tpass\n", "");
    CHECK(b.has("Undefined variable 'x'"));
    const Compiled hint = check("\tvar flag = true\n");
    CHECK(hint.has("Undefined variable 'true'"));
    CHECK(hint.hasNote("Did you mean 'True'?"));
    const Compiled scope = check("\tif True:\n\t\tvar inner = 1\n\tprint(inner)\n");
    CHECK(scope.has("Undefined variable 'inner'", 4)); // block scoped
    const Compiled function = check("\tvar x = helper\n", "func helper():\n\tpass\n");
    CHECK(function.has("is a function"));
    const Compiled call = check("\tmissing()\n");
    CHECK(call.has("Undefined function 'missing'"));
    const Compiled variableCall = check("\tvar v = 1\n\tv()\n");
    CHECK(variableCall.has("is a variable, not a function"));
}

SCHUIMM_TEST(TypeChecker_DuplicateDeclarationsAndShadowing)
{
    CHECK(check("\tvar x: int = 1\n\tvar x: int = 2\n").has("Variable 'x' is already declared"));
    CHECK(check("\tvar x = 1\n\tif True:\n\t\tvar x: int = 2\n").has("already declared"));
    CHECK(compileSource("func f(a: int):\n\tvar a: int = 2\n").has("Parameter 'a' is already declared"));
    CHECK(compileSource("func f(a: int, a: int):\n\tpass\n").has("Duplicate parameter 'a'"));
    CHECK(compileSource("func f():\n\tpass\nfunc f():\n\tpass\n").has("Duplicate declaration of function 'f'"));
    CHECK(compileSource("func print():\n\tpass\n").has("built-in function"));
    CHECK(compileSource("func str():\n\tpass\n").has("built-in name"));
    CHECK(compileSource("extern func wait(t: float)\n").has("built-in function"));
    CHECK(check("\tvar i = 0\n\tfor i in range(3):\n\t\tpass\n").has("Loop variable 'i' shadows"));
    CHECK(check("\tvar print = 3\n").has("cannot be used as a variable name"));
    CHECK(check("\tvar int = 3\n").has("cannot be used as a variable name"));
    // Assigning (not declaring) an outer variable inside a block is allowed, and sibling blocks
    // may reuse a name.
    CHECK(check("\tvar x = 1\n\tif True:\n\t\tx = 2\n\tif True:\n\t\tvar y = 1\n\telse:\n\t\tvar y = 2\n").result.success);
    CHECK(check("\tfor i in range(2):\n\t\tpass\n\tfor i in range(3):\n\t\tpass\n").result.success);
}

SCHUIMM_TEST(TypeChecker_FunctionCalls)
{
    const std::string helpers = "func damage(amount: int, scale: float) -> float:\n\treturn amount * scale\n"
                                "extern func play_sound(name: string)\n";
    CHECK(check("\tvar d = damage(3, 2)\n\tplay_sound(\"x\")\n", helpers).result.success);
    CHECK(check("\tvar d = damage(3)\n", helpers).has("takes 2 arguments, got 1"));
    CHECK(check("\tvar d = damage(3.5, 1.0)\n", helpers).has("Argument 1 of 'damage' must be of type 'int', got 'float'"));
    CHECK(check("\tplay_sound(3)\n", helpers).has("Argument 1 of 'play_sound' must be of type 'string'"));
    CHECK(check("\tprint(print(1))\n").has("has no value"));
    CHECK(check("\twait(1)\n").result.success); // int widens to float
    CHECK(check("\twait(\"1\")\n").has("must be of type 'float'"));
    CHECK(check("\tvar x = int(\"5\")\n").has("cannot convert a value of type 'string'"));
    CHECK(check("\tvar x = float(True)\n").has("cannot convert"));
    CHECK(check("\tvar x = str(1, 2)\n").has("exactly 1 argument"));
    CHECK(check("\tvar x = range(3)\n").has("only be used in a for loop"));
    CHECK(check("\t1 + 2\n").has("only function calls and spawn can be used as statements"));
    CHECK(check("\tvar x = 1\n\tx == 2\n").hasNote("Use '=' to assign"));
    // Functions can be called before their declaration and recursively.
    CHECK(compileSource("func main():\n\tprint(fact(5))\nfunc fact(n: int) -> int:\n\tif n <= 1:\n\t\treturn 1\n"
                        "\treturn n * fact(n - 1)\n")
              .result.success);
}

SCHUIMM_TEST(TypeChecker_ReturnTypes)
{
    CHECK(compileSource("func f() -> int:\n\treturn 1.5\n").has("Cannot return a value of type 'float' from function 'f'"));
    CHECK(compileSource("func f() -> float:\n\treturn 1\n").result.success);
    CHECK(compileSource("func f():\n\treturn 1\n").has("has no return type and cannot return a value"));
    CHECK(compileSource("func f() -> int:\n\treturn\n").has("must return a value of type 'int'"));
    CHECK(compileSource("func f(a: bool) -> int:\n\tif a:\n\t\treturn 1\n").has("on every path"));
    CHECK(compileSource("func f(a: bool) -> int:\n\tif a:\n\t\treturn 1\n\telse:\n\t\treturn 2\n").result.success);
    CHECK(compileSource("func f() -> int:\n\twhile True:\n\t\tpass\n").result.success);
    CHECK(compileSource("func f() -> int:\n\twhile 1 < 2:\n\t\treturn 1\n").has("on every path"));
    CHECK(compileSource("func f():\n\treturn None\n").result.success);
}

SCHUIMM_TEST(TypeChecker_OperatorsAndConditions)
{
    CHECK(check("\tvar x = \"a\" + 1\n").has("Operator '+' cannot be applied to 'string' and 'int'"));
    CHECK(check("\tvar x = \"a\" + 1\n").hasNote("str(...)"));
    CHECK(check("\tvar x = \"a\" * 2\n").has("Operator '*' cannot be applied"));
    CHECK(check("\tvar x = True + 1\n").has("Operator '+' cannot be applied to 'bool' and 'int'"));
    CHECK(check("\tvar x = -\"a\"\n").has("Operator '-' cannot be applied to 'string'"));
    CHECK(check("\tvar x = not 1\n").has("requires a 'bool' operand"));
    CHECK(check("\tvar x = 1 and True\n").has("requires 'bool' operands"));
    CHECK(check("\tvar x = \"a\" < 1\n").has("Operator '<'"));
    CHECK(check("\tvar x = True < False\n").has("Operator '<'"));
    CHECK(check("\tvar x = 1 == \"1\"\n").has("Operator '=='"));
    CHECK(check("\tif 1:\n\t\tpass\n").has("condition must be of type 'bool', got 'int'"));
    CHECK(check("\twhile \"yes\":\n\t\tpass\n").has("loop condition must be of type 'bool'"));
    CHECK(check("\tfor i in range(1.5):\n\t\tpass\n").has("range() arguments must be of type 'int'"));
    CHECK(check("\tvar x = \"a\" < \"b\" and 1 < 2.5 and True == False and 1 != 1.0\n").result.success);
}

SCHUIMM_TEST(TypeChecker_ReportsSeveralErrorsWithLocations)
{
    const Compiled c = check("\tvar a: int = \"x\"\n\tvar b = unknown\n\tvar c = 1 + \"y\"\n");
    CHECK_EQ(c.result.diagnostics.errorCount(), std::size_t{3});
    CHECK(c.has("Cannot assign", 2));
    CHECK(c.has("Undefined variable 'unknown'", 3));
    CHECK(c.has("Operator '+'", 4));
    const std::string text = c.diagnostics();
    CHECK(text.find("test.sch:3:10: error: Undefined variable 'unknown'.") != std::string::npos);
}
