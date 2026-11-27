// Project compilation: several source files compiled into one program with one game-wide
// namespace (Compiler.md, "Project compilation").

#include "ScriptTestSupport.h"

#include "Script/Bytecode/Disassembler.h"

#include <algorithm>

using namespace schuimm;
using namespace schuimm::script;
using namespace schuimm::script::test;

namespace {

struct Execution {
    std::vector<std::string> output;
    std::vector<ScriptError> errors;
};

// Links the project's module and runs `entry` (a global function) to its end.
Execution run(const Project& project, std::string_view entry)
{
    Execution execution;
    NativeRegistry natives;
    REQUIRE(registerStandardNatives(natives, [&](std::string_view, std::string_view text) {
                execution.output.emplace_back(text);
            }).has_value());
    auto bytes = writeBytecode(project.result.module);
    REQUIRE(bytes.has_value());
    auto program = Program::load(*bytes, natives, "game");
    REQUIRE(program.has_value());
    ScriptScheduler scheduler;
    scheduler.setEventCallback([&](const CoroutineEvent& event) {
        if (event.kind == CoroutineEvent::Kind::Failed && event.error != nullptr) execution.errors.push_back(*event.error);
    });
    REQUIRE(scheduler.start(*program, entry).has_value());
    for (int update = 0; update < 100 && scheduler.activeCount() > 0; ++update) scheduler.update(0.1);
    return execution;
}

const std::vector<FileSpec> crossFile = {
    {"Scripts/First.sch", "func first():\n\tsecond()\n\tprint(triple(2))\n\tprint(describe(favourite()))\n"},
    {"Scripts/Second.sch", "func second():\n\tprint(\"Hello\")\n\nfunc triple(x: int) -> int:\n\treturn x * 3\n"},
    {"Rooms/Kitchen.sch",
     "func describe(verb: Verb) -> string:\n\tif verb == Verb.LOOK:\n\t\treturn \"look\"\n\treturn \"other\"\n\n"
     "object oven:\n\tfunc use(verb: Verb):\n\t\tprint(describe(verb))\n",
     ScriptRole::Room},
    {"Scripts/Interface.sch", "enum Verb:\n\tWALK\n\tLOOK\n\nfunc favourite() -> Verb:\n\treturn Verb.LOOK\n"},
};

} // namespace

SCHUIMM_TEST(Project_CrossFileCallsAndForwardReferences)
{
    const Project project = compileFiles(crossFile);
    if (!project.result.success) std::fprintf(stderr, "%s", project.diagnostics().c_str());
    REQUIRE(project.result.success);
    // Global functions of every file, in source-name order, then the handlers.
    const Module& module = project.result.module;
    REQUIRE(module.sources.size() == 4);
    CHECK_EQ(module.sourcePath(0), std::string("Rooms/Kitchen.sch"));
    CHECK(module.sources[0].role == ScriptRole::Room);
    CHECK_EQ(module.sourcePath(3), std::string("Scripts/Second.sch"));
    const Execution execution = run(project, "first");
    CHECK(execution.errors.empty());
    CHECK(execution.output == std::vector<std::string>({"Hello", "6", "look"}));
}

SCHUIMM_TEST(Project_OutputDoesNotDependOnSourceOrder)
{
    std::vector<FileSpec> files = crossFile;
    const Project first = compileFiles(files);
    REQUIRE(first.result.success);
    const auto reference = writeBytecode(first.result.module);
    REQUIRE(reference.has_value());
    std::reverse(files.begin(), files.end());
    for (int permutation = 0; permutation < 4; ++permutation) {
        std::rotate(files.begin(), files.begin() + 1, files.end());
        const Project other = compileFiles(files);
        REQUIRE(other.result.success);
        const auto bytes = writeBytecode(other.result.module);
        REQUIRE(bytes.has_value());
        CHECK(*bytes == *reference);
    }
}

SCHUIMM_TEST(Project_EnumsHaveOneIdentityAcrossFiles)
{
    const Project project = compileFiles(crossFile);
    REQUIRE(project.result.success);
    const Module& module = project.result.module;
    const auto verbs = std::count_if(module.enums.begin(), module.enums.end(),
                                     [&](const EnumEntry& entry) { return module.string(entry.nameIndex) == "Verb"; });
    CHECK_EQ(verbs, 1);
    // A Verb is not interchangeable with another enum of the same shape.
    const Project mixed = compileFiles({
        {"A.sch", "enum Verb:\n\tWALK\n\tLOOK\n"},
        {"B.sch", "enum Mode:\n\tWALK\n\tLOOK\n\nfunc f(verb: Verb) -> bool:\n\treturn verb == Mode.LOOK\n"},
    });
    CHECK(!mixed.result.success);
    CHECK(mixed.find("Operator '=='") != nullptr);
}

SCHUIMM_TEST(Project_DuplicateDeclarationsNameBothFiles)
{
    const Project functions = compileFiles({
        {"Scripts/Global.sch", "func award(points: int):\n\tpass\n"},
        {"Rooms/Kitchen.sch", "\n\nfunc award(points: int):\n\tpass\n", ScriptRole::Room},
    });
    CHECK(!functions.result.success);
    // Rooms/Kitchen.sch comes first in source order, so Scripts/Global.sch holds the duplicate.
    const Diagnostic* duplicate = functions.find("Duplicate declaration of function 'award'");
    REQUIRE(duplicate != nullptr);
    CHECK_EQ(duplicate->range.file, 0u);
    CHECK(functions.hasNote("first declared in 'Rooms/Kitchen.sch' on line 3"));
    CHECK(functions.diagnostics().find("Scripts/Global.sch:1:6: error: Duplicate declaration of function 'award'") !=
          std::string::npos);

    const Project enums = compileFiles({{"A.sch", "enum Light:\n\tRED\n"}, {"B.sch", "enum Light:\n\tGREEN\n"}});
    CHECK(enums.find("Duplicate declaration of 'Light'") != nullptr);
    CHECK(enums.hasNote("An enum 'Light' was declared in 'A.sch' on line 1"));

    const Project clash = compileFiles({{"A.sch", "enum Light:\n\tRED\n"}, {"B.sch", "func Light():\n\tpass\n"}});
    CHECK(clash.find("Function 'Light' has the same name as an enum") != nullptr);
    CHECK(clash.hasNote("in 'A.sch' on line 1"));

    const Project native = compileFiles({{"A.sch", "func wait(seconds: float):\n\tpass\n"}});
    CHECK(native.find("'wait' is a built-in function") != nullptr);

    NativeInterface engine;
    engine.name = "engine";
    engine.enums.push_back({"Facing", {"LEFT", "RIGHT"}});
    const Project engineEnum = compileFiles({{"A.sch", "enum Facing:\n\tUP\n"}}, CompileOptions{.optimize = true, .interfaces = {&engine}});
    CHECK(engineEnum.find("Duplicate declaration of 'Facing'") != nullptr);
    CHECK(engineEnum.hasNote("built-in enum"));
}

SCHUIMM_TEST(Project_ExternDeclarationsMayRepeat)
{
    const Project same = compileFiles({
        {"A.sch", "extern func chime(volume: float)\n\nfunc a():\n\tchime(1.0)\n"},
        {"B.sch", "extern func chime(volume: float)\n\nfunc b():\n\tchime(0.5)\n"},
    });
    if (!same.result.success) std::fprintf(stderr, "%s", same.diagnostics().c_str());
    REQUIRE(same.result.success);
    CHECK_EQ(same.result.module.imports.size(), std::size_t{1});
    const Project conflicting = compileFiles({
        {"A.sch", "extern func chime(volume: float)\n"},
        {"B.sch", "extern func chime(volume: int)\n"},
    });
    CHECK(conflicting.find("Conflicting declaration of native function 'chime'") != nullptr);
    CHECK(conflicting.hasNote("declared with another signature in 'A.sch' on line 1"));
}

SCHUIMM_TEST(Project_ScriptRolesLimitDeclarations)
{
    const std::string object = "object door:\n\tfunc use():\n\t\tpass\n";
    CHECK(compileFiles({{"Rooms/Hall.sch", object, ScriptRole::Room}}).result.success);
    const Project global = compileFiles({{"Scripts/Global.sch", object, ScriptRole::Global}});
    CHECK(global.find("Object declarations belong in room scripts; this is a global script") != nullptr);
    const Project actor = compileFiles({{"Actors/Cook.sch", object, ScriptRole::Actor}});
    CHECK(actor.find("this is an actor script") != nullptr);
}

SCHUIMM_TEST(Project_RoomsMayDeclareTheSameObject)
{
    const Project project = compileFiles({
        {"Rooms/Kitchen.sch", "object door:\n\tfunc use():\n\t\tprint(\"kitchen door\")\n", ScriptRole::Room},
        {"Rooms/Garden.sch", "object door:\n\tfunc use():\n\t\tprint(\"garden gate\")\n", ScriptRole::Room},
    });
    if (!project.result.success) std::fprintf(stderr, "%s", project.diagnostics().c_str());
    REQUIRE(project.result.success);
    NativeRegistry natives;
    std::vector<std::string> output;
    REQUIRE(registerStandardNatives(natives, [&](std::string_view, std::string_view text) { output.emplace_back(text); })
                .has_value());
    auto program = Program::link(project.result.module, natives, "game");
    REQUIRE(program.has_value());
    const auto kitchen = (*program)->findBehaviour(BehaviourKind::Object, "Rooms/Kitchen.sch", "door");
    const auto garden = (*program)->findBehaviour(BehaviourKind::Object, "Rooms/Garden.sch", "door");
    REQUIRE(kitchen.has_value());
    REQUIRE(garden.has_value());
    CHECK(*kitchen != *garden);
    CHECK(!(*program)->findBehaviour(BehaviourKind::Object, "Rooms/Hall.sch", "door").has_value());
    CHECK(!(*program)->findBehaviour(BehaviourKind::Room, "Rooms/Kitchen.sch").has_value());
    const auto handler = (*program)->findHandler(*garden, "use");
    REQUIRE(handler.has_value());
    CHECK_EQ((*program)->functionSource(*handler), std::string("Rooms/Garden.sch"));
    CHECK_EQ((*program)->functionDisplayName(*handler), std::string("door.use"));
    CHECK(!(*program)->findHandler(*garden, "open").has_value());

    const std::string listing = disassemble(project.result.module);
    CHECK(listing.find("sources:\n  s0 room \"Rooms/Garden.sch\"\n  s1 room \"Rooms/Kitchen.sch\"\n") != std::string::npos);
    CHECK(listing.find("  object door (s1): use -> f1\n") != std::string::npos);
}

SCHUIMM_TEST(Project_RuntimeErrorsNameTheFileOfEachFrame)
{
    const Project project = compileFiles({
        {"Scripts/Global.sch", "func divide(a: int, b: int) -> int:\n\treturn a // b\n"},
        {"Rooms/Kitchen.sch", "func main():\n\tprint(divide(1, 0))\n", ScriptRole::Room},
    });
    REQUIRE(project.result.success);
    const Execution execution = run(project, "main");
    REQUIRE(execution.errors.size() == 1);
    const ScriptError& error = execution.errors[0];
    CHECK_EQ(error.script, std::string("Scripts/Global.sch"));
    CHECK_EQ(error.function, std::string("divide"));
    CHECK_EQ(error.position.line, 2u);
    const std::string text = describe(error);
    CHECK(text.find("Scripts/Global.sch:2:") == 0);
    CHECK(text.find("called from 'main' (Rooms/Kitchen.sch:2)") != std::string::npos);
}

SCHUIMM_TEST(Project_SourcesMustBeDistinct)
{
    const Project twice = compileFiles({{"A.sch", "func a():\n\tpass\n"}, {"A.sch", "func b():\n\tpass\n"}});
    CHECK(twice.find("The source 'A.sch' is given twice") != nullptr);
    const Project none = compileFiles({});
    CHECK(none.find("No source files to compile") != nullptr);
}
