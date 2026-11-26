#include "ScriptTestSupport.h"

#include "Platform/FileSystem/FileSystem.h"
#include "Platform/FileSystem/Path.h"

using namespace schuimm;
using namespace schuimm::script;
using namespace schuimm::script::test;

namespace {

std::string readText(const std::string& path)
{
    auto bytes = fs::readFile(path);
    REQUIRE(bytes.has_value());
    return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}

// Natives the examples declare, recording calls.
void addExampleNatives(NativeRegistry& natives, std::vector<std::string>& calls)
{
    REQUIRE(natives
                .add({"play_sound", {ScriptType::String, ScriptType::Float}, ScriptType::None,
                      [&calls](NativeContext&, std::span<const Value> arguments) {
                          calls.push_back("play_sound " + std::string(arguments[0].asString()) + " " +
                                          toDisplayString(arguments[1]));
                          return NativeResult::returnValue();
                      },
                      {}})
                .has_value());
    REQUIRE(natives
                .add({"play_chime", {}, ScriptType::None,
                      [&calls](NativeContext&, std::span<const Value>) {
                          calls.push_back("play_chime");
                          return NativeResult::returnValue();
                      },
                      {}})
                .has_value());
}

} // namespace

SCHUIMM_TEST(EndToEnd_CompileWriteLoadAndRun)
{
    // Source -> bytecode file on disk -> load (validate, link) -> run on the scheduler.
    const std::string source = readText(std::string(SCHUIMM_SCRIPT_EXAMPLES_DIR) + "/counter.sch");
    const Compiled c = compileSource(source, true, "counter.sch");
    REQUIRE(c.result.success);
    auto bytes = writeBytecode(c.result.module);
    REQUIRE(bytes.has_value());
    const std::string path = path::join(SCHUIMM_TEST_OUTPUT_DIR, "end_to_end_counter.schb");
    REQUIRE(fs::writeFile(path, *bytes).has_value());

    auto loadedBytes = fs::readFile(path);
    REQUIRE(loadedBytes.has_value());
    std::vector<std::string> output;
    NativeRegistry natives;
    REQUIRE(registerStandardNatives(natives, [&](std::string_view, std::string_view text) {
                output.emplace_back(text);
            }).has_value());
    auto program = Program::load(*loadedBytes, natives, "counter.schb");
    REQUIRE(program.has_value());
    ScriptScheduler scheduler;
    const auto handle = scheduler.start(*program, "main");
    REQUIRE(handle.has_value());
    int frames = 0;
    std::vector<int> printFrames;
    while (scheduler.activeCount() > 0 && frames < 1000) {
        const std::size_t before = output.size();
        scheduler.update(frames == 0 ? 0.0 : 1.0 / 60.0);
        if (output.size() != before) printFrames.push_back(frames);
        ++frames;
    }
    CHECK(output == std::vector<std::string>({"Welcome to SCHUIMM!", "0", "1", "2", "Finished!"}));
    CHECK(scheduler.state(*handle) == std::optional<CoroutineState>(CoroutineState::Finished));
    // One second (60 frames) between the counter lines: waiting never blocked a frame.
    REQUIRE(printFrames.size() == 4);
    CHECK_EQ(printFrames[0], 0);
    CHECK_EQ(printFrames[1], 60);
    CHECK_EQ(printFrames[2], 120);
    CHECK_EQ(printFrames[3], 180);
}

SCHUIMM_TEST(EndToEnd_ExamplesAndDemoScriptsBehaveIdenticallyOptimizedAndUnoptimized)
{
    const std::vector<std::pair<std::string, std::vector<std::string>>> scripts{
        {std::string(SCHUIMM_SCRIPT_EXAMPLES_DIR) + "/counter.sch", {"main"}},
        {std::string(SCHUIMM_SCRIPT_EXAMPLES_DIR) + "/types.sch", {"main"}},
        {std::string(SCHUIMM_SCRIPT_EXAMPLES_DIR) + "/functions.sch", {"main"}},
        {std::string(SCHUIMM_SCRIPT_EXAMPLES_DIR) + "/coroutines.sch", {"narrator", "clock", "main"}},
        {std::string(SCHUIMM_SCRIPT_EXAMPLES_DIR) + "/natives.sch", {"main"}},
        {std::string(SCHUIMM_DEMO_SCRIPTS_DIR) + "/demo.sch", {"main", "ticker"}},
    };
    for (const auto& [path, entries] : scripts) {
        std::string results[2];
        for (int optimize = 0; optimize < 2; ++optimize) {
            std::vector<std::string> calls;
            NativeRegistry natives;
            std::vector<std::string> output;
            REQUIRE(registerStandardNatives(natives, [&](std::string_view, std::string_view text) {
                        output.emplace_back(text);
                    }).has_value());
            addExampleNatives(natives, calls);
            RunConfig config;
            config.optimize = optimize == 1;
            config.entries = entries;
            config.dt = 0.25;
            const ScriptRun run = runScript(readText(path), config, &natives);
            if (!run.compiled) std::fprintf(stderr, "  %s:\n%s", path.c_str(), run.diagnostics.c_str());
            REQUIRE(run.compiled);
            CHECK(run.errors.empty());
            CHECK_EQ(run.stillActive, std::size_t{0});
            std::string text;
            for (const std::string& line : output) text += line + "\n";
            for (const std::string& call : calls) text += call + "\n";
            results[optimize] = text + "updates=" + std::to_string(run.updates);
        }
        if (results[0] != results[1]) {
            std::fprintf(stderr, "  %s differs:\n-O0:\n%s\n-O1:\n%s\n", path.c_str(), results[0].c_str(), results[1].c_str());
            CHECK(false);
        }
        CHECK(!results[0].empty());
    }
}

SCHUIMM_TEST(EndToEnd_ExampleOutputs)
{
    std::vector<std::string> calls;
    NativeRegistry natives;
    std::vector<std::string> output;
    REQUIRE(registerStandardNatives(natives, [&](std::string_view, std::string_view text) { output.emplace_back(text); }).has_value());
    addExampleNatives(natives, calls);
    RunConfig config;
    config.entries = {"narrator", "clock"};
    config.dt = 0.25;
    const ScriptRun run = runScript(readText(std::string(SCHUIMM_SCRIPT_EXAMPLES_DIR) + "/coroutines.sch"), config, &natives);
    REQUIRE(run.errors.empty());
    // Interleaved by time: the clock ticks every 0.5 s, the narrator speaks every second.
    CHECK(output == std::vector<std::string>({"Narrator: it is a quiet night.", "Clock: tick 1",
                                              "Narrator: suddenly, a knock on the door.", "Clock: tick 2", "Clock: tick 3",
                                              "Narrator: the end.", "Clock: tick 4"}));
    output.clear();
    config.entries = {"main"};
    const ScriptRun types = runScript(readText(std::string(SCHUIMM_SCRIPT_EXAMPLES_DIR) + "/types.sch"), config, &natives);
    REQUIRE(types.errors.empty());
    CHECK(output == std::vector<std::string>({"Welcome, Alex", "We have the key!", "Distance: 42.0", "Whole steps: 2",
                                              "Half: 6.0, floor: 2, rest: 2"}));
}

SCHUIMM_TEST(EndToEnd_ProgramsAgreeWithAndWithoutOptimization)
{
    // A battery of small programs, including ones that fail: output, error message and error
    // location must not depend on the optimizer.
    const std::vector<std::string> programs{
        "func main():\n\tvar x = 3\n\tvar y = x * 4 - 2\n\tif y > 5 and not (x == 2):\n\t\tprint(y)\n",
        "func main():\n\tvar s = \"\"\n\tfor i in range(5):\n\t\ts = s + str(i)\n\tprint(s)\n",
        "func main():\n\tvar k = 0\n\twhile k < 10:\n\t\tk = k + 3\n\tprint(k)\n\tprint(k // 4)\n\tprint(k % 4)\n",
        "func main():\n\tvar big = 4611686018427387904\n\tprint(big + big)\n",
        "func main():\n\tprint(\"start\")\n\tvar x = 10 // (5 - 5)\n",
        "func main():\n\tvar a = 1.5\n\tvar b = 2\n\tprint(a * b)\n\tprint(b / 4)\n\tprint(int(a * 3))\n",
        "func f(n: int) -> int:\n\treturn n * n\nfunc main():\n\tvar t = 0\n\tfor i in range(1, 4):\n\t\tt = t + f(i)\n\tprint(t)\n",
        "func main():\n\tvar flag = 1 < 2\n\tif flag:\n\t\tprint(\"a\")\n\telif 2 < 3:\n\t\tprint(\"b\")\n\telse:\n\t\tprint(\"c\")\n",
        "func main():\n\tfor i in range(3, 0, -1):\n\t\tprint(i)\n\t\twait(0.5)\n\tprint(\"go\")\n",
        "func main():\n\tprint(int(1e300))\n",
    };
    for (const std::string& source : programs) {
        RunConfig a;
        RunConfig b;
        b.optimize = false;
        const std::string optimized = describeRun(runScript(source, a));
        const std::string unoptimized = describeRun(runScript(source, b));
        if (optimized != unoptimized) {
            std::fprintf(stderr, "  program:\n%s-O1:\n%s-O0:\n%s", source.c_str(), optimized.c_str(), unoptimized.c_str());
            CHECK(false);
        }
    }
}

SCHUIMM_TEST(EndToEnd_DiagnosticsFormat)
{
    const Compiled c = compileSource("func main():\n\tvar player_speed = 2\n\tprint(player_sped)\n", true, "scripts/main.sch");
    REQUIRE(!c.result.success);
    const std::string text = c.diagnostics();
    CHECK(text.find("scripts/main.sch:3:8: error: Undefined variable 'player_sped'.") != std::string::npos);
    CHECK(text.find("\tprint(player_sped)") != std::string::npos);
    CHECK(text.find("^^^^^^^^^^^") != std::string::npos);
    const Compiled d = compileSource("func main():\n\tvar count: int = 1\n\tcount = \"many\"\n", true, "scripts/player.sch");
    CHECK(d.diagnostics().find("scripts/player.sch:3:10: error: Cannot assign a value of type 'string' to variable 'count' of type 'int'.") !=
          std::string::npos);
}
