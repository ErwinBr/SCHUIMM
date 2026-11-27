#include "ScriptTestSupport.h"

#include "Script/Runtime/Coroutine.h"
#include "Script/Runtime/Interpreter.h"

using namespace schuimm;
using namespace schuimm::script;
using namespace schuimm::script::test;

namespace {

// Runs main() and returns its output (and errors), optimized and unoptimized; both must agree.
std::string output(const std::string& body, const std::string& extra = {})
{
    const std::string source = extra + "func main():\n" + body;
    RunConfig optimized;
    RunConfig unoptimized;
    unoptimized.optimize = false;
    const ScriptRun a = runScript(source, optimized);
    const ScriptRun b = runScript(source, unoptimized);
    const std::string textA = describeRun(a);
    const std::string textB = describeRun(b);
    if (textA != textB) {
        std::fprintf(stderr, "  optimized:\n%s  unoptimized:\n%s", textA.c_str(), textB.c_str());
        CHECK(false);
    }
    return textA;
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

} // namespace

SCHUIMM_TEST(Vm_IntegerArithmetic)
{
    CHECK_EQ(output("\tprint(7 + 5 * 2)\n\tprint(7 - 10)\n\tprint(7 // 2)\n\tprint(-7 // 2)\n\tprint(7 % 3)\n"
                    "\tprint(-7 % 3)\n\tprint(7 % -3)\n\tprint(-9223372036854775808)\n\tprint(9223372036854775807)\n"),
             std::string("17\n-3\n3\n-4\n1\n2\n-2\n-9223372036854775808\n9223372036854775807\n"));
}

SCHUIMM_TEST(Vm_FloatArithmeticAndFormatting)
{
    CHECK_EQ(output("\tprint(7 / 2)\n\tprint(1.5 * 2)\n\tprint(0.1 + 0.2)\n\tprint(-7.5 // 2)\n\tprint(5.5 % 2)\n"
                    "\tprint(-0.0)\n\tprint(1e20)\n\tprint(1.0 / 3)\n\tprint(123456789.0)\n\tprint(1e300 * 1e300)\n"
                    "\tprint(-1e300 * 1e300)\n\tprint(2.5e-7)\n"),
             std::string("3.5\n3.0\n0.30000000000000004\n-4.0\n1.5\n-0.0\n1e+20\n0.3333333333333333\n123456789.0\ninf\n-inf\n2.5e-07\n"));
    CHECK_EQ(output("\tvar inf = 1e300 * 1e300\n\tvar nan = inf - inf\n\tprint(nan)\n\tprint(nan == nan)\n\tprint(nan != nan)\n"),
             std::string("nan\nFalse\nTrue\n"));
}

SCHUIMM_TEST(Vm_FloatsUsePositionalNotationForModerateExponents)
{
    // The shortest digits, written positionally for decimal exponents -4..15 (as Python does).
    CHECK_EQ(output("\tprint(10.0)\n\tprint(-250.0)\n\tprint(1e15)\n\tprint(1e16)\n\tprint(0.0001)\n\tprint(0.00001)\n"
                    "\tprint(1234.5)\n\tprint(str(100.0))\n"),
             std::string("10.0\n-250.0\n1000000000000000.0\n1e+16\n0.0001\n1e-05\n1234.5\n100.0\n"));
}

SCHUIMM_TEST(Vm_ConversionsAndStrings)
{
    CHECK_EQ(output("\tprint(int(3.9))\n\tprint(int(-3.9))\n\tprint(int(True))\n\tprint(float(2))\n\tprint(str(42) + \"!\")\n"
                    "\tprint(str(2.0))\n\tprint(str(False))\n\tvar name = \"Alex\"\n\tprint(\"Welcome, \" + name)\n"
                    "\tprint(\"abc\" < \"abd\")\n\tprint(\"b\" > \"abc\")\n\tprint(\"x\" == \"x\")\n\tprint(\"\" + \"\")\n"
                    "\tprint(\"Z\" < \"a\")\n\tprint(\"é\" > \"z\")\n"),
             std::string("3\n-3\n1\n2.0\n42!\n2.0\nFalse\nWelcome, Alex\nTrue\nTrue\nTrue\n\nTrue\nTrue\n"));
}

SCHUIMM_TEST(Vm_ComparisonsAndLogic)
{
    CHECK_EQ(output("\tprint(1 < 2)\n\tprint(2 <= 2)\n\tprint(3 > 4)\n\tprint(3 >= 4)\n\tprint(1 == 1.0)\n\tprint(1 != 2)\n"
                    "\tprint(True == False)\n\tprint(not True)\n\tprint(True and False)\n\tprint(False or True)\n"
                    "\tprint(1 < 2 and 2 < 3 or False)\n"),
             std::string("True\nTrue\nFalse\nFalse\nTrue\nTrue\nFalse\nFalse\nFalse\nTrue\nTrue\n"));
}

SCHUIMM_TEST(Vm_ShortCircuitAndEvaluationOrder)
{
    const std::string helpers = "func side(name: string, result: bool) -> bool:\n\tprint(name)\n\treturn result\n"
                                "func value(name: string, v: int) -> int:\n\tprint(name)\n\treturn v\n"
                                "func pair(a: int, b: int) -> int:\n\treturn a - b\n";
    CHECK_EQ(output("\tvar x = side(\"a\", False) and side(\"b\", True)\n\tvar y = side(\"c\", True) or side(\"d\", True)\n"
                    "\tvar z = side(\"e\", True) and side(\"f\", False)\n\tprint(x or y and not z)\n",
                    helpers),
             std::string("a\nc\ne\nf\nTrue\n"));
    CHECK_EQ(output("\tprint(pair(value(\"first\", 10), value(\"second\", 3)))\n\tprint(value(\"l\", 1) + value(\"r\", 2) * value(\"m\", 3))\n",
                    helpers),
             std::string("first\nsecond\n7\nl\nr\nm\n7\n"));
}

SCHUIMM_TEST(Vm_BranchesAndLoops)
{
    CHECK_EQ(output("\tfor n in range(4):\n\t\tif n == 0:\n\t\t\tprint(\"zero\")\n\t\telif n == 1:\n\t\t\tprint(\"one\")\n"
                    "\t\telse:\n\t\t\tprint(n)\n\tvar count = 0\n\twhile count < 3:\n\t\tcount = count + 1\n\tprint(count)\n"),
             std::string("zero\none\n2\n3\n3\n"));
    CHECK_EQ(output("\tfor i in range(2, 5):\n\t\tprint(i)\n\tfor j in range(10, 0, -4):\n\t\tprint(j)\n"
                    "\tfor k in range(0):\n\t\tprint(\"never\")\n\tfor m in range(5, 1):\n\t\tprint(\"never\")\n"),
             std::string("2\n3\n4\n10\n6\n2\n"));
    // Assigning the loop variable does not change the iteration (the counter is hidden).
    CHECK_EQ(output("\tfor i in range(3):\n\t\tprint(i)\n\t\ti = 100\n"), std::string("0\n1\n2\n"));
    // Bounds are evaluated once; the counter does not overflow near the int limit.
    CHECK_EQ(output("\tvar m = 9223372036854775807\n\tfor i in range(m - 3, m, 2):\n\t\tprint(i)\n"
                    "\tvar low = -9223372036854775807 - 1\n\tfor j in range(low + 2, low, -2):\n\t\tprint(j)\n"),
             std::string("9223372036854775804\n9223372036854775806\n-9223372036854775806\n"));
    CHECK_EQ(output("\tvar end = 3\n\tfor i in range(end):\n\t\tend = 1\n\t\tprint(i)\n"), std::string("0\n1\n2\n"));
}

SCHUIMM_TEST(Vm_FunctionsParametersAndReturnValues)
{
    const std::string functions =
        "func fib(n: int) -> int:\n\tif n < 2:\n\t\treturn n\n\treturn fib(n - 1) + fib(n - 2)\n"
        "func greet(name: string, times: int) -> string:\n\tvar result = \"\"\n\tfor i in range(times):\n\t\tresult = result + name\n\treturn result\n"
        "func half(x: float) -> float:\n\treturn x / 2\n"
        "func nothing():\n\treturn\n"
        "func swap_order(a: int, b: int, c: int) -> int:\n\treturn a * 100 + b * 10 + c\n";
    CHECK_EQ(output("\tprint(fib(15))\n\tprint(greet(\"ab\", 3))\n\tprint(half(5))\n\tnothing()\n"
                    "\tvar x = 1\n\tvar y = 2\n\tvar z = 3\n\tprint(swap_order(z, x, y))\n\tprint(swap_order(1, 2, 3))\n",
                    functions),
             std::string("610\nababab\n2.5\n312\n123\n"));
    // Arguments are passed by value.
    CHECK_EQ(output("\tvar v = 5\n\tbump(v)\n\tprint(v)\n", "func bump(v: int):\n\tv = v + 1\n\tprint(v)\n"), std::string("6\n5\n"));
}

SCHUIMM_TEST(Vm_RuntimeErrorsHaveLocations)
{
    const std::string overflow = output("\tvar big = 9223372036854775807\n\tvar one = 1\n\tprint(\"before\")\n\tvar x = big + one\n\tprint(\"after\")\n");
    CHECK(contains(overflow, "before\n"));
    CHECK(!contains(overflow, "after"));
    CHECK(contains(overflow, "test.sch:5:10: Runtime error in 'main': Integer overflow"));
    CHECK(contains(output("\tvar zero = 0\n\tprint(10 // zero)\n"), "test.sch:3:8: Runtime error in 'main': Division by zero"));
    CHECK(contains(output("\tvar zero = 0\n\tprint(10 % zero)\n"), "Division by zero"));
    CHECK(contains(output("\tvar zero = 0.0\n\tprint(1.0 / zero)\n"), "Division by zero"));
    CHECK(contains(output("\tvar zero = 0\n\tprint(1 / zero)\n"), "Division by zero"));
    CHECK(contains(output("\tvar m = -9223372036854775807 - 1\n\tprint(-m)\n"), "Integer overflow"));
    CHECK(contains(output("\tvar m = -9223372036854775807 - 1\n\tprint(m // -1)\n"), "Integer overflow"));
    CHECK(contains(output("\tvar m = 3037000500\n\tprint(m * m)\n"), "Integer overflow"));
    CHECK(contains(output("\tvar inf = 1e300 * 1e300\n\tprint(int(inf - inf))\n"), "Cannot convert float nan to int"));
    CHECK(contains(output("\tprint(int(1e19))\n"), "Cannot convert float 1e+19 to int"));
    CHECK(contains(output("\tvar step = 0\n\tfor i in range(0, 3, step):\n\t\tpass\n"), "range() step must not be zero"));
    CHECK(contains(output("\twait(-1.0)\n"), "Native function error in 'main': wait(): the duration must be"));
    // The call stack names the callers.
    const std::string nested = output("\touter()\n", "func outer():\n\tinner(0)\nfunc inner(d: int):\n\tprint(1 // d)\n");
    CHECK(contains(nested, "test.sch:4:8: Runtime error in 'inner': Division by zero"));
    CHECK(contains(nested, "called from 'outer' (line 2)"));
    CHECK(contains(nested, "called from 'main' (line 6)"));
}

SCHUIMM_TEST(Vm_RecursionDepthIsLimited)
{
    const std::string text = output("\tprint(down(100000))\n", "func down(n: int) -> int:\n\tif n == 0:\n\t\treturn 0\n\treturn down(n - 1)\n");
    CHECK(contains(text, "Call depth limit of 200 exceeded"));
    CHECK_EQ(output("\tprint(down(150))\n", "func down(n: int) -> int:\n\tif n == 0:\n\t\treturn 0\n\treturn down(n - 1)\n"),
             std::string("0\n"));
}

SCHUIMM_TEST(Vm_StringsAreBoundedAndReleased)
{
    const std::int64_t before = StringObject::liveCount();
    const std::string text = output("\tvar s = \"0123456789abcdef\"\n\twhile True:\n\t\ts = s + s\n");
    CHECK(contains(text, "String too long"));
    CHECK_EQ(StringObject::liveCount(), before); // Everything released, including program constants.
}

SCHUIMM_TEST(Vm_TypeMismatchInHandWrittenBytecodeIsAControlledError)
{
    // AddInt on a bool register: impossible from the compiler, but bytecode is untrusted. The
    // validator rejects it statically from the register types.
    Module module;
    module.strings = {"bad.sch", "main"};
    module.sources = {{0, ScriptRole::Room}};
    Function main;
    main.nameIndex = 1;
    main.registerTypes = {ScriptType::Bool, ScriptType::Int};
    main.code = {{Opcode::LoadTrue, 0, 0, 0}, {Opcode::AddInt, 1, 0, 0}, {Opcode::Return, noRegister, 0, 0}};
    main.positions = {{1, 1}, {2, 1}, {3, 1}};
    module.functions.push_back(main);
    NativeRegistry natives;
    auto rejected = Program::link(module, natives);
    REQUIRE(!rejected.has_value());
    CHECK(rejected.error().message.find("has type bool, expected int") != std::string::npos);
    // Statically well-typed, but reading an uninitialised register (None): the VM's tag check
    // turns it into a controlled error.
    module.functions[0].registerTypes = {ScriptType::Int, ScriptType::Int};
    module.functions[0].code[0] = {Opcode::Nop, 0, 0, 0};
    auto program = Program::link(module, natives);
    REQUIRE(program.has_value());
    auto coroutine = Coroutine::create(*program, "main", {});
    REQUIRE(coroutine.has_value());
    NativeContext context;
    const RunResult result = runCoroutine(**coroutine, 100, context);
    CHECK(result.outcome == RunOutcome::Failed);
    CHECK(result.error.kind == ScriptErrorKind::InvalidBytecode);
    CHECK_EQ(result.error.position.line, std::uint32_t{2});
}

SCHUIMM_TEST(Vm_TraceShowsInstructions)
{
    const Compiled c = compileSource("func main():\n\tvar x = 2\n\tprint(x * 3)\n", false);
    REQUIRE(c.result.success);
    NativeRegistry natives;
    REQUIRE(registerStandardNatives(natives, {}).has_value());
    auto program = Program::link(c.result.module, natives);
    REQUIRE(program.has_value());
    auto coroutine = Coroutine::create(*program, "main", {});
    REQUIRE(coroutine.has_value());
    std::vector<std::string> lines;
    const TraceCallback trace = [&](const TraceEvent& event) { lines.push_back(formatTrace(event)); };
    NativeContext context;
    const RunResult result = runCoroutine(**coroutine, 1000, context, &trace);
    CHECK(result.outcome == RunOutcome::Finished);
    CHECK_EQ(lines.size(), static_cast<std::size_t>(result.instructions));
    bool sawMul = false;
    for (const std::string& line : lines) sawMul = sawMul || line.find("MulInt") != std::string::npos;
    CHECK(sawMul);
    CHECK(lines[0].find("main:0 (line 2)") == 0);
}

SCHUIMM_TEST(Vm_EntryFunctionResultAndArguments)
{
    const Compiled c = compileSource("func area(w: int, h: float) -> float:\n\treturn w * h\n");
    REQUIRE(c.result.success);
    NativeRegistry natives;
    auto program = Program::link(c.result.module, natives);
    REQUIRE(program.has_value());
    auto coroutine = Coroutine::create(*program, "area", {Value::fromInt(3), Value::fromFloat(1.5)});
    REQUIRE(coroutine.has_value());
    NativeContext context;
    CHECK(runCoroutine(**coroutine, 100, context).outcome == RunOutcome::Finished);
    CHECK((*coroutine)->result().is(ScriptType::Float));
    CHECK_EQ((*coroutine)->result().asFloat(), 4.5);
    CHECK(!Coroutine::create(*program, "area", {Value::fromInt(3)}).has_value());
    CHECK(!Coroutine::create(*program, "area", {Value::fromInt(3), Value::fromInt(1)}).has_value());
    CHECK(Coroutine::create(*program, "missing", {}).error().code == ErrorCode::NotFound);
}
