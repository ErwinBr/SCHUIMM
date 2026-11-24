#include "ScriptTestSupport.h"

#include "Script/Compiler/Ir.h"

using namespace schuimm;
using namespace schuimm::script;
using namespace schuimm::script::test;

namespace {

// Unoptimised IR of a program.
std::string irOf(const std::string& source)
{
    const Compiled c = compileSource(source, false);
    if (!c.result.success) return "<error>\n" + c.diagnostics();
    return dumpIr(c.result.ir);
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

} // namespace

SCHUIMM_TEST(Ir_ThreeAddressCodeForAssignments)
{
    const std::string ir = irOf("func main():\n\ta = 10\n\tb = 20\n\tc = a + b\n");
    CHECK(contains(ir, "func main() -> None"));
    CHECK(contains(ir, "= LoadInt 10"));
    CHECK(contains(ir, "= LoadInt 20"));
    CHECK(contains(ir, "= AddInt r0, r1")); // a and b live in r0 and r1
    CHECK(contains(ir, "r2 = Move"));       // c
    CHECK(contains(ir, "  Return\n"));
}

SCHUIMM_TEST(Ir_ParametersAndReturnValues)
{
    const std::string ir = irOf("func scale(amount: int, factor: float) -> float:\n\treturn amount * factor\n");
    CHECK(contains(ir, "func scale(r0: int, r1: float) -> float"));
    CHECK(contains(ir, "= IntToFloat r0"));
    CHECK(contains(ir, "= MulFloat"));
    CHECK(contains(ir, "Return r"));
}

SCHUIMM_TEST(Ir_BranchesAndLoops)
{
    const std::string ir = irOf("func main():\n\tx = 0\n\twhile x < 3:\n\t\tx = x + 1\n\tif x == 3:\n\t\tprint(\"three\")\n"
                                "\telif x > 3:\n\t\tprint(\"more\")\n\telse:\n\t\tprint(\"less\")\n");
    CHECK(contains(ir, "LtInt"));
    CHECK(contains(ir, "Branch r"));
    CHECK(contains(ir, "Jump bb1")); // the loop's back edge to its condition block
    CHECK(contains(ir, "EqInt"));
    CHECK(contains(ir, "GtInt"));
    const Compiled c = compileSource("func main():\n\tfor i in range(0, 10, 2):\n\t\tprint(i)\n", false);
    REQUIRE(c.result.success);
    const std::string loop = dumpIr(c.result.ir);
    CHECK(contains(loop, "CheckRangeStep"));
    CHECK(contains(loop, "AddIntSat"));
    CHECK(verifyIr(c.result.ir.functions[0]).empty());
}

SCHUIMM_TEST(Ir_ShortCircuitUsesControlFlow)
{
    const Compiled c = compileSource("func t() -> bool:\n\treturn True\nfunc main():\n\tx = t() and t()\n\ty = t() or t()\n", false);
    REQUIRE(c.result.success);
    const IrFunction& main = c.result.ir.functions[1];
    int branches = 0;
    for (const IrBlock& block : main.blocks) branches += block.terminator.kind == TerminatorKind::Branch ? 1 : 0;
    CHECK_EQ(branches, 2);
}

SCHUIMM_TEST(Ir_FunctionAndNativeCalls)
{
    const std::string ir =
        irOf("extern func play(name: string) -> bool\nfunc helper(n: int) -> int:\n\treturn n\n"
             "func main():\n\tv = helper(2)\n\tok = play(\"x\")\n\tprint(v)\n\twait(1)\n\tyield()\n");
    CHECK(contains(ir, "native play(string) -> bool"));
    CHECK(contains(ir, "= Call helper r"));
    CHECK(contains(ir, "= CallNative play r"));
    CHECK(contains(ir, "CallNative print r"));
    CHECK(contains(ir, "CallNative yield\n"));
    CHECK(contains(ir, "IntToFloat")); // wait(1): int widened to float
}

SCHUIMM_TEST(Ir_SourcePositionsAndVerification)
{
    const Compiled c = compileSource("func main():\n\tx = 1\n\n\ty = x * 2\n", false);
    REQUIRE(c.result.success);
    bool sawLine4 = false;
    for (const IrBlock& block : c.result.ir.functions[0].blocks) {
        for (const IrInstruction& instruction : block.instructions) {
            if (instruction.op == IrOp::MulInt) sawLine4 = instruction.position.line == 4 && instruction.position.column == 6;
        }
    }
    CHECK(sawLine4);
    for (const IrFunction& function : c.result.ir.functions) CHECK(verifyIr(function).empty());
    IrFunction broken = c.result.ir.functions[0];
    broken.blocks[0].terminator.kind = TerminatorKind::Jump;
    broken.blocks[0].terminator.target = 99;
    CHECK(!verifyIr(broken).empty());
}
