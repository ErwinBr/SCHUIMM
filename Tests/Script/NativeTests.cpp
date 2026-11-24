#include "ScriptTestSupport.h"

using namespace schuimm;
using namespace schuimm::script;
using namespace schuimm::script::test;

namespace {

NativeFunction native(std::string name, std::vector<ScriptType> parameters, ScriptType returnType,
                      NativeImplementation implementation = [](NativeContext&, std::span<const Value>) {
                          return NativeResult::returnValue();
                      })
{
    return {std::move(name), std::move(parameters), returnType, std::move(implementation), {}};
}

} // namespace

SCHUIMM_TEST(Native_RegistrationIsValidated)
{
    NativeRegistry registry;
    CHECK(registry.add(native("play_sound", {ScriptType::String, ScriptType::Float}, ScriptType::None)).has_value());
    CHECK(registry.find("play_sound") != nullptr);
    CHECK(registry.find("missing") == nullptr);
    CHECK(!registry.add(native("play_sound", {}, ScriptType::None)).has_value()); // duplicate
    CHECK(!registry.add(native("", {}, ScriptType::None)).has_value());
    CHECK(!registry.add(native("9lives", {}, ScriptType::None)).has_value());
    CHECK(!registry.add(native("has space", {}, ScriptType::None)).has_value());
    CHECK(!registry.add(native("bad", {ScriptType::None}, ScriptType::None)).has_value());
    CHECK(!registry.add(native("bad", {}, ScriptType::Any)).has_value());
    CHECK(!registry.add({"bad", {}, ScriptType::None, {}, {}}).has_value()); // no implementation
    CHECK(registry.add(native("log_any", {ScriptType::Any}, ScriptType::None)).has_value());
    CHECK(registry.names() == std::vector<std::string>({"log_any", "play_sound"}));
    CHECK_EQ(formatSignature("wait", {ScriptType::Float}, ScriptType::None), std::string("wait(float) -> None"));
}

SCHUIMM_TEST(Native_ExternFunctionsAreCalledWithTypedArguments)
{
    NativeRegistry registry;
    std::vector<std::string> calls;
    REQUIRE(registry
                .add(native("move_character", {ScriptType::String, ScriptType::Int, ScriptType::Float}, ScriptType::Bool,
                            [&](NativeContext& context, std::span<const Value> arguments) {
                                calls.push_back(std::string(arguments[0].asString()) + "," +
                                                std::to_string(arguments[1].asInt()) + "," +
                                                std::to_string(arguments[2].asFloat()) + " from " +
                                                std::string(context.script));
                                return NativeResult::returnValue(Value::fromBool(arguments[1].asInt() > 0));
                            }))
                .has_value());
    const ScriptRun run = runScript("extern func move_character(name: string, x: int, speed: float) -> bool\n"
                                    "func main():\n\tok = move_character(\"Alex\", 3, 2)\n\tprint(ok)\n"
                                    "\tprint(move_character(\"Bo\", -1, 0.5))\n",
                                    {}, &registry);
    REQUIRE(run.errors.empty());
    CHECK_EQ(run.joined(), std::string("True\nFalse\n"));
    REQUIRE(calls.size() == 2);
    CHECK_EQ(calls[0], std::string("Alex,3,2.000000 from test.sch")); // int 2 widened to float
}

SCHUIMM_TEST(Native_LinkingChecksPresenceAndSignature)
{
    const Compiled c = compileSource("extern func say(text: string) -> int\nfunc main():\n\tsay(\"hi\")\n");
    REQUIRE(c.result.success);
    NativeRegistry empty;
    REQUIRE(registerStandardNatives(empty, {}).has_value());
    const auto missing = Program::link(c.result.module, empty);
    REQUIRE(!missing.has_value());
    CHECK(missing.error().code == ErrorCode::NotFound);
    CHECK(missing.error().message.find("say(string) -> int") != std::string::npos);

    NativeRegistry wrong;
    REQUIRE(wrong.add(native("say", {ScriptType::String}, ScriptType::None)).has_value());
    const auto mismatch = Program::link(c.result.module, wrong);
    REQUIRE(!mismatch.has_value());
    CHECK(mismatch.error().code == ErrorCode::InvalidData);
    CHECK(mismatch.error().message.find("engine provides say(string) -> None") != std::string::npos);

    // Unused extern declarations are not imported, so they need not exist at run time.
    const Compiled unused = compileSource("extern func later(x: int)\nfunc main():\n\tprint(1)\n");
    REQUIRE(unused.result.success);
    CHECK(Program::link(unused.result.module, empty).has_value());
}

SCHUIMM_TEST(Native_ErrorsAndWrongResultsAreReported)
{
    NativeRegistry registry;
    REQUIRE(registry
                .add(native("fail", {}, ScriptType::None,
                            [](NativeContext&, std::span<const Value>) { return NativeResult::error("sound not found"); }))
                .has_value());
    REQUIRE(registry
                .add(native("liar", {}, ScriptType::Int,
                            [](NativeContext&, std::span<const Value>) {
                                return NativeResult::returnValue(Value::fromString("not an int"));
                            }))
                .has_value());
    const ScriptRun a = runScript("extern func fail()\nfunc main():\n\tprint(\"a\")\n\tfail()\n\tprint(\"b\")\n", {}, &registry);
    REQUIRE(a.errors.size() == 1);
    CHECK(a.errors[0].kind == ScriptErrorKind::Native);
    CHECK_EQ(a.errors[0].message, std::string("fail(): sound not found"));
    CHECK_EQ(a.errors[0].position.line, std::uint32_t{4});
    CHECK_EQ(a.joined(), std::string("a\n"));
    NativeRegistry registry2;
    REQUIRE(registry2
                .add(native("liar", {}, ScriptType::Int,
                            [](NativeContext&, std::span<const Value>) { return NativeResult::returnValue(Value::fromString("x")); }))
                .has_value());
    const ScriptRun b = runScript("extern func liar() -> int\nfunc main():\n\tprint(liar())\n", {}, &registry2);
    REQUIRE(b.errors.size() == 1);
    CHECK(b.errors[0].message.find("returned string instead of int") != std::string::npos);
}

SCHUIMM_TEST(Native_PrintAcceptsEveryType)
{
    const ScriptRun run = runScript("func main():\n\tprint(1)\n\tprint(2.5)\n\tprint(True)\n\tprint(\"text\")\n");
    CHECK_EQ(run.joined(), std::string("1\n2.5\nTrue\ntext\n"));
    CHECK_EQ(toDisplayString(Value::none()), std::string("None"));
}

SCHUIMM_TEST(Native_NativesCanSuspend)
{
    NativeRegistry registry;
    REQUIRE(registry
                .add(native("pause", {ScriptType::Int}, ScriptType::None,
                            [](NativeContext&, std::span<const Value> arguments) {
                                return NativeResult::waitFor(arguments[0].asInt() * 1'000'000'000);
                            }))
                .has_value());
    RunConfig config;
    config.dt = 0.5;
    const ScriptRun run = runScript("extern func pause(seconds: int)\nfunc main():\n\tpause(2)\n\tprint(\"done\")\n", config, &registry);
    CHECK_EQ(run.joined(), std::string("done\n"));
    CHECK_EQ(run.updates, 5); // t = 0, 0.5, 1.0, 1.5, 2.0
}
