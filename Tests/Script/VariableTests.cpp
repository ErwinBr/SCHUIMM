// Global and instance variables (Language.md §7): declarations, initial values, lexical
// resolution and shadowing, compound assignment, the None value, bytecode and execution.

#include "ScriptTestSupport.h"

#include "Script/Bytecode/BytecodeFormat.h"
#include "Script/Bytecode/Crc32.h"
#include "Script/Bytecode/Disassembler.h"
#include "Script/Runtime/ScriptState.h"

using namespace schuimm;
using namespace schuimm::script;
using namespace schuimm::script::test;

namespace {

// A game session over one compiled module: its global state and a scheduler.
struct Session {
    std::vector<std::string> output;
    std::vector<ScriptError> errors;
    NativeRegistry natives;
    std::shared_ptr<const Program> program;
    std::shared_ptr<GlobalState> globals;
    ScriptScheduler scheduler;

    explicit Session(const Module& module)
    {
        REQUIRE(registerStandardNatives(natives, [this](std::string_view, std::string_view text) {
                    output.emplace_back(text);
                }).has_value());
        auto bytes = writeBytecode(module);
        REQUIRE(bytes.has_value());
        auto loaded = Program::load(*bytes, natives, "game");
        if (!loaded) std::fprintf(stderr, "  load error: %s\n", loaded.error().message.c_str());
        REQUIRE(loaded.has_value());
        program = *loaded;
        globals = GlobalState::create(program);
        scheduler.setEventCallback([this](const CoroutineEvent& event) {
            if (event.kind == CoroutineEvent::Kind::Failed && event.error != nullptr) errors.push_back(*event.error);
        });
    }

    void start(std::string_view function) { REQUIRE(scheduler.start(globals, function).has_value()); }
    void run(int updates = 200)
    {
        for (int update = 0; update < updates && scheduler.activeCount() > 0; ++update) scheduler.update(0.1);
        scheduler.releaseFinished();
    }
    [[nodiscard]] std::string joined() const
    {
        std::string text;
        for (const std::string& line : output) text += line + "\n";
        for (const ScriptError& error : errors) text += describe(error) + "\n";
        return text;
    }
};

Project compileOne(const std::string& text, bool optimize = true)
{
    return compileFiles({{"Rooms/Test.sch", text, ScriptRole::Room}}, CompileOptions{.optimize = optimize, .interfaces = {}});
}

std::string runMain(const std::string& text, bool optimize = true)
{
    const Project project = compileOne(text, optimize);
    if (!project.result.success) return project.diagnostics();
    Session session(project.result.module);
    session.start("main");
    session.run();
    return session.joined();
}

bool hasWarning(const Project& project, std::string_view text)
{
    for (const Diagnostic& d : project.result.diagnostics.all()) {
        if (d.severity == Severity::Warning && d.message.find(text) != std::string::npos) return true;
    }
    return false;
}

} // namespace

SCHUIMM_TEST(Variables_GlobalsAreSharedAcrossFiles)
{
    const Project project = compileFiles({
        {"Scripts/Global.sch", "var score: int = 0\n\nfunc award(points: int):\n\tscore += points\n"},
        {"Rooms/Kitchen.sch",
         "var oven_repaired: bool = False\n\nfunc main():\n\taward(10)\n\taward(5)\n\toven_repaired = True\n"
         "\tprint(score)\n\tprint(oven_repaired)\n\tprint(cook_greeted)\n",
         ScriptRole::Room},
        {"Actors/Cook.sch", "var cook_greeted = \"not yet\"\n", ScriptRole::Actor},
    });
    if (!project.result.success) std::fprintf(stderr, "%s", project.diagnostics().c_str());
    REQUIRE(project.result.success);
    REQUIRE(project.result.module.globals.size() == 3);
    Session session(project.result.module);
    session.start("main");
    session.run();
    CHECK_EQ(session.joined(), std::string("15\nTrue\nnot yet\n"));
    // The state belongs to the session: a new one starts from the initial values.
    CHECK_EQ(session.globals->find("score")->asInt(), 15);
    const auto fresh = GlobalState::create(session.program);
    CHECK_EQ(fresh->find("score")->asInt(), 0);
    CHECK(!fresh->find("oven_repaired")->asBool());
    CHECK(fresh->find("missing") == nullptr);
}

SCHUIMM_TEST(Variables_InitialAndDefaultValues)
{
    const std::string source =
        "enum Verb:\n\tWALK\n\tLOOK\n\n"
        "var i = -3\nvar f: float = 2\nvar b = True\nvar s = \"hi\"\nvar v = vec2(1, -2.5)\nvar e = Verb.LOOK\n"
        "var a: actor\nvar o: object = None\nvar c: coroutine\nvar op: operation\n"
        "var di: int\nvar df: float\nvar db: bool\nvar ds: string\nvar dv: vec2\nvar de: Verb\n\n"
        "func main():\n\tprint(i)\n\tprint(f)\n\tprint(b)\n\tprint(s)\n\tprint(v)\n\tprint(e)\n"
        "\tprint(a == None)\n\tprint(None == o)\n\tprint(a != None)\n"
        "\tprint(di)\n\tprint(df)\n\tprint(db)\n\tprint(ds + \"|\")\n\tprint(dv)\n\tprint(de)\n"
        "\tvar local_v: vec2\n\tvar local_e: Verb\n\tvar local_a: entity\n\tprint(local_v)\n\tprint(local_e)\n"
        "\tprint(local_a == None)\n";
    const std::string expected = "-3\n2.0\nTrue\nhi\nvec2(1.0, -2.5)\nVerb.LOOK\nTrue\nTrue\nFalse\n"
                                 "0\n0.0\nFalse\n|\nvec2(0.0, 0.0)\nVerb.WALK\nvec2(0.0, 0.0)\nVerb.WALK\nTrue\n";
    CHECK_EQ(runMain(source, true), expected);
    CHECK_EQ(runMain(source, false), expected);
}

SCHUIMM_TEST(Variables_InitialValuesMustBeConstants)
{
    const Project sum = compileOne("var x = 1 + 2\n");
    CHECK(sum.find("The initial value of global variable 'x' must be a constant") != nullptr);
    CHECK(sum.hasNote("init()"));
    CHECK(compileOne("func f() -> int:\n\treturn 1\nvar y = f()\n").find("must be a constant") != nullptr);
    CHECK(compileOne("var a = 1\nvar b = a\n").find("must be a constant") != nullptr);
    CHECK(compileOne("object o:\n\tvar n = int(2.5)\n\tfunc f():\n\t\tpass\n").find("instance variable 'n' must be a constant") !=
          nullptr);
    CHECK(compileOne("var n = None\n").find("cannot be inferred from None") != nullptr);
    CHECK(compileOne("var n: int = \"x\"\n").find("Cannot assign a value of type 'string' to global variable 'n'") != nullptr);
    CHECK(compileOne("var n: int = 2.5\n").find("Cannot assign") != nullptr);
    CHECK(compileOne("var p: vec2 = vec2(-1, 2.5)\nvar m = -9223372036854775808\n").result.success);
}

SCHUIMM_TEST(Variables_NameConflicts)
{
    const Project twice = compileFiles({{"A.sch", "var score = 0\n"}, {"B.sch", "\nvar score = 1\n"}});
    CHECK(twice.find("Duplicate declaration of global variable 'score'") != nullptr);
    CHECK(twice.hasNote("first declared in 'A.sch' on line 1"));
    const Project function = compileFiles({{"A.sch", "func score():\n\tpass\n"}, {"B.sch", "var score = 1\n"}});
    CHECK(function.find("Global variable 'score' has the same name as a function") != nullptr);
    CHECK(compileOne("enum Light:\n\tRED\nvar Light = 1\n").find("has the same name as an enum") != nullptr);
    CHECK(compileOne("var wait = 1\n").find("same name as a function") != nullptr);
    CHECK(compileOne("var int = 1\n").find("built-in name") != nullptr);
    const Project instance = compileOne("object o:\n\tvar n = 1\n\tvar n = 2\n\tfunc f():\n\t\tpass\n");
    CHECK(instance.find("Duplicate declaration of variable 'n'") != nullptr);
    CHECK(compileOne("func n():\n\tpass\nobject o:\n\tvar n = 1\n\tfunc f():\n\t\tpass\n").find("name of a function") != nullptr);
    // Different objects may use the same instance variable names.
    CHECK(compileOne("object a:\n\tvar n = 1\n\tfunc f():\n\t\tpass\nobject b:\n\tvar n = 2\n\tfunc f():\n\t\tpass\n")
              .result.success);
}

SCHUIMM_TEST(Variables_NearestDeclarationWinsWithAWarning)
{
    const std::string source = "var count = 1\n\n"
                               "func shadow():\n\tvar count = 100\n\tcount += 1\n\tprint(count)\n\n"
                               "func parameter(count: int):\n\tprint(count)\n\n"
                               "func main():\n\tshadow()\n\tparameter(7)\n\tcount = count + 1\n\tprint(count)\n";
    const Project project = compileOne(source);
    REQUIRE(project.result.success);
    CHECK(hasWarning(project, "Variable 'count' shadows the global variable 'count'"));
    CHECK(hasWarning(project, "Parameter 'count' shadows the global variable 'count'"));
    CHECK(project.hasNote("The global variable is declared on line 1"));
    CHECK_EQ(runMain(source), std::string("101\n7\n2\n"));

    const std::string instance = "var n = 1\n\nobject o:\n\tvar n = 10\n\tfunc use():\n\t\tvar n = 20\n\t\tprint(n)\n";
    const Project shadowed = compileOne(instance);
    REQUIRE(shadowed.result.success);
    CHECK(hasWarning(shadowed, "Instance variable 'n' shadows the global variable 'n'"));
    CHECK(hasWarning(shadowed, "Variable 'n' shadows the instance variable 'n'"));
    // Locals still cannot shadow locals.
    CHECK(compileOne("func f(x: int):\n\tvar x = 1\n").find("already declared") != nullptr);
    // Warnings name the other file.
    const Project files = compileFiles({{"A.sch", "var hp = 1\n"}, {"B.sch", "func f():\n\tvar hp = 2\n"}});
    CHECK(files.hasNote("declared in 'A.sch' on line 1"));
}

SCHUIMM_TEST(Variables_CompoundAssignment)
{
    const std::string source = "var total = 10\nvar where = vec2(1, 2)\n\n"
                               "func main():\n\ttotal += 5\n\ttotal -= 3\n\ttotal *= 2\n\tprint(total)\n"
                               "\tvar half: float = 9\n\thalf /= 2\n\tprint(half)\n"
                               "\twhere.x += 0.5\n\twhere.y -= 4\n\tprint(where)\n"
                               "\tvar local = vec2(0, 0)\n\tlocal.y += 1\n\tprint(local)\n"
                               "\tvar words = \"a\"\n\twords += \"b\"\n\tprint(words)\n";
    CHECK_EQ(runMain(source, true), std::string("24\n4.5\nvec2(1.5, -2.0)\nvec2(0.0, 1.0)\nab\n"));
    CHECK_EQ(runMain(source, false), std::string("24\n4.5\nvec2(1.5, -2.0)\nvec2(0.0, 1.0)\nab\n"));
    CHECK(compileOne("func f():\n\tvar n = 1\n\tn += \"x\"\n").find("Operator '+'") != nullptr);
    CHECK(compileOne("func f():\n\tmissing += 1\n").find("Undefined variable 'missing'") != nullptr);
    CHECK(compileOne("func f():\n\tvar n = 1\n\tn //= 2\n").find("Expected") != nullptr);
}

SCHUIMM_TEST(Variables_ReadsAreNeverStale)
{
    // A global changed by another coroutine while this one waits, and by a call: the loop and
    // the statement after the call read the current value, with and without the optimizer.
    const std::string source = "var completed = False\nvar counter = 0\n\n"
                               "func wait_for_completion():\n\tvar rounds = 0\n\twhile not completed:\n\t\twait(0.1)\n"
                               "\t\trounds += 1\n\tprint(\"completed after \" + str(rounds))\n\n"
                               "func complete():\n\twait(0.35)\n\tcompleted = True\n\n"
                               "func bump():\n\tcounter += 1\n\n"
                               "func main():\n\tspawn wait_for_completion()\n\tspawn complete()\n\tcounter = 1\n\tbump()\n"
                               "\tprint(counter)\n\tbump()\n\tbump()\n\tprint(counter)\n";
    for (const bool optimize : {true, false}) {
        CHECK_EQ(runMain(source, optimize), std::string("2\n4\ncompleted after 5\n"));
    }
    // The optimized IR keeps the read inside the loop.
    const Project project = compileOne(source);
    REQUIRE(project.result.success);
    const std::string ir = dumpIr(project.result.optimizedIr);
    const std::size_t loop = ir.find("func wait_for_completion");
    REQUIRE(loop != std::string::npos);
    CHECK(ir.find("LoadGlobal completed", loop) != std::string::npos);
}

SCHUIMM_TEST(Variables_InstanceVariablesBelongToTheirInstance)
{
    const std::string source = "var uses = 0\n\nobject lamp:\n\tvar on = False\n\tvar switched: int\n"
                               "\tfunc use():\n\t\ton = not on\n\t\tswitched += 1\n\t\tuses += 1\n"
                               "\t\tprint(str(on) + \" \" + str(switched) + \" \" + str(uses))\n";
    const Project project = compileOne(source);
    if (!project.result.success) std::fprintf(stderr, "%s", project.diagnostics().c_str());
    REQUIRE(project.result.success);
    REQUIRE(project.result.module.behaviours.size() == 1);
    CHECK_EQ(project.result.module.behaviours[0].variables.size(), std::size_t{2});
    Session session(project.result.module);
    const auto behaviour = session.program->findBehaviour(BehaviourKind::Object, {}, "lamp");
    REQUIRE(behaviour.has_value());
    auto first = BehaviourInstance::create(session.globals, *behaviour, Value::fromObject(ObjectRef{1}));
    auto second = BehaviourInstance::create(session.globals, *behaviour, Value::fromObject(ObjectRef{2}));
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    for (const auto& instance : {*first, *first, *second}) {
        REQUIRE(session.scheduler.dispatch(instance, "use").has_value());
        session.run();
    }
    // Each lamp has its own variables; the global is shared.
    CHECK_EQ(session.joined(), std::string("True 1 1\nFalse 2 2\nTrue 1 3\n"));
    CHECK(!(*first)->find("on")->asBool());
    CHECK_EQ((*second)->find("switched")->asInt(), 1);
    // An expired instance takes no more events.
    (*first)->expire();
    auto refused = session.scheduler.dispatch(*first, "use");
    REQUIRE(!refused.has_value());
    CHECK(refused.error().code == ErrorCode::InvalidState);
    CHECK(!session.scheduler.dispatch(std::shared_ptr<BehaviourInstance>(), "use").has_value());
    // The instance needs the right self.
    CHECK(!BehaviourInstance::create(session.globals, *behaviour, Value::fromActor(ObjectRef{3})).has_value());
    CHECK(!BehaviourInstance::create(session.globals, *behaviour, Value::fromObject(ObjectRef{})).has_value());
    CHECK(!BehaviourInstance::create(session.globals, 7, Value::fromObject(ObjectRef{1})).has_value());
    // Dispatch by name gives each call a fresh state of its own.
    REQUIRE(session.scheduler.dispatch(session.program, "lamp", "use", ObjectRef{1}).has_value());
    session.run();
    CHECK_EQ(session.output.back(), std::string("True 1 1"));
}

SCHUIMM_TEST(Variables_SpawnedCoroutinesShareTheSession)
{
    const std::string source = "var ticks = 0\n\nfunc tick():\n\twait(0.1)\n\tticks += 1\n\n"
                               "func main():\n\tspawn tick()\n\tspawn tick()\n\twait(0.5)\n\tprint(ticks)\n";
    CHECK_EQ(runMain(source), std::string("2\n"));
    // Two sessions of one program do not share variables.
    const Project project = compileOne(source);
    REQUIRE(project.result.success);
    Session session(project.result.module);
    const auto other = GlobalState::create(session.program);
    REQUIRE(session.scheduler.start(session.globals, "main").has_value());
    REQUIRE(session.scheduler.start(other, "tick").has_value());
    session.run();
    CHECK_EQ(session.globals->find("ticks")->asInt(), 2);
    CHECK_EQ(other->find("ticks")->asInt(), 1);
}

SCHUIMM_TEST(Variables_BytecodeRoundTripAndDisassembly)
{
    const Project project = compileOne("enum Mode:\n\tA\n\tB\nvar name = \"x\"\nvar spot = vec2(1, 2)\nvar mode = Mode.B\n"
                                       "var who: actor\n\nobject box:\n\tvar open = True\n\tfunc use():\n\t\topen = False\n"
                                       "\t\tname = \"y\"\n");
    REQUIRE(project.result.success);
    const Module& module = project.result.module;
    auto bytes = writeBytecode(module);
    REQUIRE(bytes.has_value());
    auto read = readBytecode(*bytes);
    REQUIRE(read.has_value());
    REQUIRE(read->globals.size() == 4);
    CHECK(read->globals[1].type == TypeRef(ScriptType::Vec2));
    CHECK_EQ(read->globals[1].initial.y, 2.0);
    CHECK_EQ(read->globals[2].initial.ordinal, 1u);
    REQUIRE(read->behaviours[0].variables.size() == 1);
    CHECK(read->behaviours[0].variables[0].initial.boolValue);
    CHECK(*writeBytecode(*read) == *bytes);
    const std::string listing = disassemble(*read);
    CHECK(listing.find("globals:\n  g0 name: string = \"x\"\n  g1 spot: vec2 = vec2(1.0, 2.0)\n  g2 mode: Mode = Mode.B\n"
                       "  g3 who: actor = None\n") != std::string::npos);
    CHECK(listing.find("    i0 open: bool = True\n") != std::string::npos);
    CHECK(listing.find("StoreInstance i0, r") != std::string::npos);
    CHECK(listing.find("StoreGlobal g0 (name), r") != std::string::npos);
}

SCHUIMM_TEST(Variables_MalformedBytecodeIsRejected)
{
    const Project project = compileOne("enum Mode:\n\tA\nvar n = 1\nvar s = \"x\"\nvar m = Mode.A\n\n"
                                       "func main():\n\tn = n + 1\n\nobject box:\n\tvar open = True\n\tfunc use():\n\t\topen = False\n");
    REQUIRE(project.result.success);
    const Module good = project.result.module;
    REQUIRE(validateModule(good).has_value());
    const auto invalid = [](const Module& module, std::string_view text) {
        auto status = validateModule(module);
        if (status) return false;
        if (status.error().message.find(text) == std::string::npos) {
            std::fprintf(stderr, "  got: %s\n", status.error().message.c_str());
            return false;
        }
        return true;
    };
    const auto find = [](Module& module, Opcode opcode) -> Instruction& {
        for (Function& function : module.functions) {
            for (Instruction& instruction : function.code) {
                if (instruction.opcode == opcode) return instruction;
            }
        }
        REQUIRE(false);
        return module.functions[0].code[0];
    };
    Module m = good;
    find(m, Opcode::LoadGlobal).b = 9;
    CHECK(invalid(m, "global variable 9 does not exist"));
    m = good;
    find(m, Opcode::StoreGlobal).a = 1; // The int register into the string global.
    CHECK(invalid(m, "in global variable 1 of type string"));
    m = good;
    find(m, Opcode::StoreInstance).a = 3;
    CHECK(invalid(m, "instance variable 3 does not exist"));
    m = good;
    {
        Instruction& load = find(m, Opcode::LoadGlobal);
        load.opcode = Opcode::LoadInstance;
        load.b = 0;
    }
    CHECK(invalid(m, "outside a behaviour (by a global function)"));
    m = good;
    m.globals[1].initial.stringIndex = 9999;
    CHECK(invalid(m, "starts with missing string 9999"));
    m = good;
    m.globals[2].initial.ordinal = 5;
    CHECK(invalid(m, "starts with member 5"));
    m = good;
    m.globals.push_back(m.globals[0]);
    CHECK(invalid(m, "duplicate global variable 'n'"));
    m = good;
    m.globals[0].type = ScriptType::Any;
    CHECK(invalid(m, "global variable 'n' has type any"));
    m = good;
    m.behaviours[0].variables[0].nameIndex = 9999;
    CHECK(invalid(m, "variable 0 refers to a missing name"));
    m = good;
    find(m, Opcode::LoadGlobal).opcode = Opcode::LoadNull;
    find(m, Opcode::LoadNull).b = 0;
    CHECK(invalid(m, "has no null value"));
    // A truncated global table.
    auto bytes = writeBytecode(good);
    REQUIRE(bytes.has_value());
    const auto sectionEntry = format::headerSize + (static_cast<std::size_t>(format::SectionId::Globals) - 1) * format::sectionEntrySize;
    std::uint32_t offset = 0;
    for (int i = 0; i < 4; ++i) offset |= static_cast<std::uint32_t>((*bytes)[sectionEntry + 4 + i]) << (8 * i);
    for (int i = 0; i < 4; ++i) (*bytes)[offset + i] = std::byte{0xFF};
    std::uint32_t crc = crc32(std::span<const std::byte>(bytes->data() + format::headerSize, bytes->size() - format::headerSize));
    for (int i = 0; i < 4; ++i) (*bytes)[28 + i] = static_cast<std::byte>((crc >> (8 * i)) & 0xFF);
    auto rejected = readBytecode(*bytes);
    REQUIRE(!rejected.has_value());
    CHECK(rejected.error().message.find("truncated global table") != std::string::npos);
}

SCHUIMM_TEST(Variables_InstanceCodeNeedsItsInstance)
{
    // A handler run without its behaviour instance (only possible from C++) fails cleanly.
    const Project project = compileOne("object box:\n\tvar open = True\n\tfunc use():\n\t\topen = False\n");
    REQUIRE(project.result.success);
    NativeRegistry natives;
    REQUIRE(registerStandardNatives(natives, {}).has_value());
    auto program = Program::link(project.result.module, natives);
    REQUIRE(program.has_value());
    const auto behaviour = (*program)->findBehaviour(BehaviourKind::Object, {}, "box");
    REQUIRE(behaviour.has_value());
    auto coroutine = Coroutine::createCall(*program, *(*program)->findHandler(*behaviour, "use"), {});
    REQUIRE(coroutine.has_value());
    NativeContext context;
    const RunResult result = runCoroutine(**coroutine, 1000, context);
    CHECK(result.outcome == RunOutcome::Failed);
    CHECK(result.error.kind == ScriptErrorKind::InvalidBytecode);
    CHECK(result.error.message.find("no instance of this behaviour") != std::string::npos);
}
