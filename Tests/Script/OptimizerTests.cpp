#include "ScriptTestSupport.h"

#include "Script/Compiler/Optimizer.h"

using namespace schuimm;
using namespace schuimm::script;
using namespace schuimm::script::test;

namespace {

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

struct Optimized {
    Compiled compiled;
    std::string before;
    std::string after;
};

Optimized optimizeSource(const std::string& source)
{
    Optimized o{compileSource(source, true), {}, {}};
    if (o.compiled.result.success) {
        o.before = dumpIr(o.compiled.result.ir);
        o.after = dumpIr(o.compiled.result.optimizedIr);
    } else {
        o.after = o.compiled.diagnostics();
    }
    return o;
}

std::size_t blockCount(const Optimized& o, std::size_t function = 0)
{
    return o.compiled.result.optimizedIr.functions[function].blocks.size();
}

} // namespace

SCHUIMM_TEST(Optimizer_ConstantFolding)
{
    const Optimized o = optimizeSource("func main():\n\tresult = 10 + 20\n\tprint(result)\n");
    CHECK(contains(o.before, "AddInt"));
    CHECK(!contains(o.after, "AddInt"));
    CHECK(contains(o.after, "LoadInt 30"));
    const Optimized f = optimizeSource("func main():\n\tprint(1.5 * 4 - 1 // 2)\n\tprint(\"a\" + \"b\" + str(3))\n\tprint(not (1 < 2))\n");
    CHECK(contains(f.after, "LoadFloat 6.0"));
    CHECK(contains(f.after, "LoadString \"ab3\""));
    CHECK(contains(f.after, "LoadBool False"));
}

SCHUIMM_TEST(Optimizer_ConstantPropagationAcrossBlocks)
{
    const Optimized o = optimizeSource("func main():\n\ta = 4\n\tif a > 1:\n\t\tb = a * 2\n\t\tprint(b)\n");
    CHECK(contains(o.after, "LoadInt 8"));
    CHECK(!contains(o.after, "MulInt"));
    CHECK(!contains(o.after, "Branch"));
    // A variable changed in a loop is not constant.
    const Optimized loop = optimizeSource("func main():\n\tx = 0\n\twhile x < 3:\n\t\tx = x + 1\n\tprint(x)\n");
    CHECK(contains(loop.after, "AddInt"));
    CHECK(contains(loop.after, "Branch"));
}

SCHUIMM_TEST(Optimizer_DeadCodeElimination)
{
    const Optimized o = optimizeSource("func main():\n\tunused = 5.0 * 3.0\n\talso = \"x\" + \"y\"\n\tprint(1)\n");
    CHECK(!contains(o.after, "15.0"));
    CHECK(!contains(o.after, "\"xy\""));
    CHECK(contains(o.after, "CallNative print"));
    // Pass by pass: eliminateDeadCode alone removes the unused pure computation.
    Compiled c = compileSource("func main(a: int):\n\tx = a < 3\n\tprint(a)\n", false);
    REQUIRE(c.result.success);
    IrModule module = c.result.ir;
    CHECK(contains(dumpIr(module), "LtInt"));
    CHECK(eliminateDeadCode(module.functions[0], module));
    CHECK(!contains(dumpIr(module), "LtInt"));
    CHECK(!eliminateDeadCode(module.functions[0], module)); // fixed point
}

SCHUIMM_TEST(Optimizer_ConstantBranchesAndUnreachableBlocks)
{
    const Optimized o = optimizeSource("func main():\n\tif 1 < 2:\n\t\tprint(\"yes\")\n\telse:\n\t\tprint(\"no\")\n"
                                       "\twhile False:\n\t\tprint(\"never\")\n");
    CHECK(contains(o.after, "\"yes\""));
    CHECK(!contains(o.after, "\"no\""));
    CHECK(!contains(o.after, "never"));
    CHECK(!contains(o.after, "Branch"));
    CHECK_EQ(blockCount(o), std::size_t{1}); // Straight-line code merges into one block.
    // The constant step of range() removes the step check and the descending test.
    const Optimized loop = optimizeSource("func main():\n\tfor i in range(3):\n\t\tprint(i)\n");
    CHECK(!contains(loop.after, "CheckRangeStep"));
    CHECK(!contains(loop.after, "GtInt"));
    CHECK(contains(loop.after, "LtInt"));
}

SCHUIMM_TEST(Optimizer_JumpThreadingAndBlockMerging)
{
    Compiled c = compileSource("func main(a: bool):\n\tif a:\n\t\tpass\n\tprint(1)\n", false);
    REQUIRE(c.result.success);
    IrFunction function = c.result.ir.functions[0];
    const std::size_t before = function.blocks.size();
    CHECK(simplifyControlFlow(function));
    removeUnreachableBlocks(function);
    CHECK(function.blocks.size() < before);
    CHECK(verifyIr(function).empty());
    // The empty "then" block is threaded away; both edges go to the join, so the branch disappears.
    CHECK(!contains(dumpIr(function, c.result.ir), "Branch"));
}

SCHUIMM_TEST(Optimizer_PreservesSideEffectsAndTraps)
{
    // Calls are kept even when their results are unused.
    const Optimized calls = optimizeSource("func side() -> int:\n\tprint(\"side\")\n\treturn 1\n"
                                           "func main():\n\tunused = side()\n\twait(0.5)\n");
    CHECK(contains(calls.after, "Call side"));
    CHECK(contains(calls.after, "CallNative wait"));
    // Operations that would trap are neither folded nor removed: the error still happens at run time.
    const Optimized traps = optimizeSource("func main():\n\ta = 9223372036854775807 + 1\n\tb = 1 // 0\n\tc = 1.0 % 0.0\n"
                                           "\td = int(1e300)\n\tfor i in range(0, 5, 0):\n\t\tpass\n");
    CHECK(contains(traps.after, "AddInt"));
    CHECK(contains(traps.after, "DivInt"));
    CHECK(contains(traps.after, "ModFloat"));
    CHECK(contains(traps.after, "FloatToInt"));
    CHECK(contains(traps.after, "CheckRangeStep"));
    const ScriptRun run = runScript("func main():\n\ta = 9223372036854775807 + 1\n\tprint(\"unreachable\")\n");
    REQUIRE(run.errors.size() == 1);
    CHECK(run.errors[0].message.find("Integer overflow") != std::string::npos);
    CHECK(run.output.empty());
}

SCHUIMM_TEST(Optimizer_CanBeDisabledAndIsDeterministic)
{
    const std::string source = "func main():\n\tresult = 10 + 20\n\tprint(result)\n";
    const Compiled off = compileSource(source, false);
    REQUIRE(off.result.success);
    CHECK_EQ(dumpIr(off.result.ir), dumpIr(off.result.optimizedIr));
    CHECK(contains(dumpIr(off.result.optimizedIr), "AddInt"));
    const Compiled on1 = compileSource(source, true);
    const Compiled on2 = compileSource(source, true);
    CHECK_EQ(dumpIr(on1.result.optimizedIr), dumpIr(on2.result.optimizedIr));
    CHECK(on1.result.module.functions[0].code.size() < off.result.module.functions[0].code.size());
}

SCHUIMM_TEST(Optimizer_FoldingMatchesRuntimeSemantics)
{
    // The same expressions folded at compile time and evaluated by the VM (through parameters,
    // which the optimizer cannot see) must print identical text.
    const std::string folded = "func main():\n\tprint(-7 // 2)\n\tprint(-7 % 3)\n\tprint(7 % -3)\n\tprint(-7.5 // 2.0)\n"
                               "\tprint(-7.5 % 2.0)\n\tprint(1.0 / 3.0)\n\tprint(0.1 + 0.2)\n";
    const std::string runtime = "func main():\n\tcompute(-7, 2, 3, -3, -7.5, 2.0, 3.0, 0.1, 0.2)\n"
                                "func compute(a: int, b: int, c: int, d: int, x: float, y: float, z: float, p: float, q: float):\n"
                                "\tprint(a // b)\n\tprint(a % c)\n\tprint(7 % d)\n\tprint(x // y)\n\tprint(x % y)\n"
                                "\tprint(1.0 / z)\n\tprint(p + q)\n";
    const Optimized check = optimizeSource(folded);
    CHECK(!contains(check.after, "DivInt")); // really folded
    const ScriptRun a = runScript(folded);
    const ScriptRun b = runScript(runtime);
    REQUIRE(a.errors.empty());
    REQUIRE(b.errors.empty());
    CHECK_EQ(a.joined(), b.joined());
    CHECK_EQ(a.joined(), std::string("-4\n2\n-2\n-4.0\n0.5\n0.3333333333333333\n0.30000000000000004\n"));
}
