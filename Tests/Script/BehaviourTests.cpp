// Behaviour declarations (Language.md §8): "room:", "actor:" and "object NAME:", their handlers
// calling each other, self as the instance's entity, and coroutines running for an instance.

#include "ScriptTestSupport.h"

#include "Script/Bytecode/Disassembler.h"
#include "Script/Compiler/AstPrinter.h"
#include "Script/Runtime/ScriptState.h"

using namespace schuimm;
using namespace schuimm::script;
using namespace schuimm::script::test;

namespace {

// The game session of one compiled project: its globals, a scheduler, print output and errors.
struct Game {
    std::vector<std::string> output;
    std::vector<ScriptError> errors;
    NativeRegistry natives;
    std::shared_ptr<const Program> program;
    std::shared_ptr<GlobalState> globals;
    ScriptScheduler scheduler;

    explicit Game(const Project& project)
    {
        if (!project.result.success) std::fprintf(stderr, "%s", project.diagnostics().c_str());
        REQUIRE(project.result.success);
        REQUIRE(registerStandardNatives(natives, [this](std::string_view, std::string_view text) {
                    output.emplace_back(text);
                }).has_value());
        auto bytes = writeBytecode(project.result.module);
        REQUIRE(bytes.has_value());
        auto loaded = Program::load(*bytes, natives, "game");
        REQUIRE(loaded.has_value());
        program = *loaded;
        globals = GlobalState::create(program);
        scheduler.setEventCallback([this](const CoroutineEvent& event) {
            if (event.kind == CoroutineEvent::Kind::Failed && event.error != nullptr) errors.push_back(*event.error);
        });
    }

    std::shared_ptr<BehaviourInstance> instance(BehaviourKind kind, std::string_view source, std::string_view name,
                                                Value self)
    {
        const auto behaviour = program->findBehaviour(kind, source, name);
        REQUIRE(behaviour.has_value());
        auto created = BehaviourInstance::create(globals, *behaviour, std::move(self));
        REQUIRE(created.has_value());
        return *created;
    }

    void dispatch(const std::shared_ptr<BehaviourInstance>& instance, std::string_view event, ExecutionOwner owner = 0)
    {
        auto result = scheduler.dispatch(instance, event, {}, owner);
        REQUIRE(result.has_value());
        REQUIRE(result->handled);
    }

    void run(int updates = 100)
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

bool hasWarning(const Project& project, std::string_view text)
{
    for (const Diagnostic& d : project.result.diagnostics.all()) {
        if (d.severity == Severity::Warning && d.message.find(text) != std::string::npos) return true;
    }
    return false;
}

const std::vector<FileSpec> kitchenGame = {
    {"Scripts/Global.sch",
     "var score: int = 0\n\nfunc award_points(amount: int):\n\tscore += amount\n\n"
     "func announce(character: actor):\n\twait(0.2)\n\tprint(\"announce \" + str(character == None))\n"},
    {"Rooms/Kitchen.sch",
     "var kitchen_visited: bool = False\n\n"
     "room:\n\tvar visits: int = 0\n\n\tfunc ready():\n\t\tvisits += 1\n\t\tgreet()\n\n"
     "\tfunc greet():\n\t\tprint(\"kitchen visit \" + str(visits))\n\t\tkitchen_visited = True\n\n"
     "object oven:\n\tvar inspections: int = 0\n\n\tfunc look_at():\n\t\tinspect()\n\t\tprint(inspections)\n\n"
     "\tfunc inspect():\n\t\tinspections += 1\n\n"
     "\tfunc use():\n\t\twait(0.1)\n\t\taward_points(10)\n\t\tprint(self == self)\n\n"
     "\tfunc ready():\n\t\tprint(\"oven ready\")\n",
     ScriptRole::Room},
    {"Actors/Cook.sch",
     "var cook_greeted: bool = False\n\n"
     "actor:\n\tvar conversations: int = 0\n\n\tfunc talk_to():\n\t\tconversations += 1\n\t\tgreet_player()\n"
     "\t\tannounce(self)\n\t\tprint(\"conversations \" + str(conversations))\n\n"
     "\tfunc greet_player():\n\t\tif not cook_greeted:\n\t\t\tprint(\"Good morning!\")\n\t\t\tcook_greeted = True\n"
     "\t\telse:\n\t\t\tprint(\"Busy.\")\n\n"
     "\tfunc ready():\n\t\tprint(\"cook ready\")\n\n"
     "\tfunc patrol():\n\t\tspawn walk_round()\n\n"
     "\tfunc walk_round():\n\t\twait(0.1)\n\t\tconversations += 100\n\t\tprint(\"round for \" + str(self != None))\n",
     ScriptRole::Actor},
};

} // namespace

SCHUIMM_TEST(Behaviours_RoomActorAndObjectDeclarations)
{
    const Project project = compileFiles(kitchenGame);
    if (!project.result.success) std::fprintf(stderr, "%s", project.diagnostics().c_str());
    REQUIRE(project.result.success);
    const Module& module = project.result.module;
    // Actors/Cook.sch, Rooms/Kitchen.sch (room, oven), in source order.
    REQUIRE(module.behaviours.size() == 3);
    CHECK(module.behaviours[0].kind == BehaviourKind::Actor);
    CHECK(module.behaviours[1].kind == BehaviourKind::Room);
    CHECK(module.behaviours[2].kind == BehaviourKind::Object);
    // Every behaviour may have its own "ready".
    const std::string listing = disassemble(module);
    CHECK(listing.find("  actor (s0): talk_to -> f") != std::string::npos);
    CHECK(listing.find("  room (s1): ready -> f") != std::string::npos);
    CHECK(listing.find("function f") != std::string::npos);
    CHECK(listing.find("actor.greet_player() -> None") != std::string::npos);
    CHECK(listing.find("LoadSelf r") != std::string::npos);
    CHECK(dumpAst(*project.result.units[0].ast).find("Actor\n  Var conversations: int") != std::string::npos);
}

SCHUIMM_TEST(Behaviours_HandlersRunForTheirInstance)
{
    Game game(compileFiles(kitchenGame));
    const auto room = game.instance(BehaviourKind::Room, "Rooms/Kitchen.sch", {}, {});
    const auto oven = game.instance(BehaviourKind::Object, "Rooms/Kitchen.sch", "oven", Value::fromObject(ObjectRef{7}));
    const auto cook = game.instance(BehaviourKind::Actor, "Actors/Cook.sch", {}, Value::fromActor(ObjectRef{8}));
    const auto guard = game.instance(BehaviourKind::Actor, "Actors/Cook.sch", {}, Value::fromActor(ObjectRef{9}));
    // Room: room variables and an internal call.
    game.dispatch(room, "ready");
    game.dispatch(room, "ready");
    game.run();
    // Object: an internal call changes the object's variable; a suspending handler calls a
    // global function and still has self afterwards.
    game.dispatch(oven, "look_at");
    game.dispatch(oven, "look_at");
    game.dispatch(oven, "use");
    game.run();
    // Actors: two instances of one actor script have their own variables and share the globals;
    // a global function that waits returns to the handler's context.
    game.dispatch(cook, "talk_to");
    game.run();
    game.dispatch(cook, "talk_to");
    game.run();
    game.dispatch(guard, "talk_to");
    game.run();
    CHECK_EQ(game.joined(), std::string("kitchen visit 1\nkitchen visit 2\n1\n2\nTrue\n"
                                        "Good morning!\nannounce False\nconversations 1\n"
                                        "Busy.\nannounce False\nconversations 2\n"
                                        "Busy.\nannounce False\nconversations 1\n"));
    CHECK_EQ(room->find("visits")->asInt(), 2);
    CHECK_EQ(cook->find("conversations")->asInt(), 2);
    CHECK_EQ(guard->find("conversations")->asInt(), 1);
    CHECK_EQ(game.globals->find("score")->asInt(), 10);
    CHECK(game.globals->find("kitchen_visited")->asBool());
    CHECK(game.globals->find("cook_greeted")->asBool());
    // A room behaviour instance has no self; actor and object ones need theirs.
    CHECK(!BehaviourInstance::create(game.globals, room->behaviour(), Value::fromActor(ObjectRef{1})).has_value());
    CHECK(!BehaviourInstance::create(game.globals, cook->behaviour(), Value::fromObject(ObjectRef{1})).has_value());
}

SCHUIMM_TEST(Behaviours_SpawnedBehaviourFunctionsKeepTheirContext)
{
    Game game(compileFiles(kitchenGame));
    const auto cook = game.instance(BehaviourKind::Actor, "Actors/Cook.sch", {}, Value::fromActor(ObjectRef{8}));
    const ExecutionOwner owner = game.scheduler.createOwner();
    game.dispatch(cook, "patrol", owner);
    game.scheduler.update(0.0); // patrol runs and spawns walk_round, owned by the same owner.
    CHECK_EQ(game.scheduler.activeCount(owner), std::size_t{1});
    game.run();
    CHECK_EQ(game.joined(), std::string("round for True\n"));
    CHECK_EQ(cook->find("conversations")->asInt(), 100);
    // Cancelling the owner cancels the spawned behaviour function too.
    game.dispatch(cook, "patrol", owner);
    game.scheduler.update(0.0);
    CHECK_EQ(game.scheduler.cancelOwner(owner), std::size_t{1});
    game.run();
    CHECK_EQ(cook->find("conversations")->asInt(), 100);
}

SCHUIMM_TEST(Behaviours_SelfIsTypedByItsBehaviour)
{
    const auto check = [](std::string text, ScriptRole role) { return compileFiles({{"A.sch", std::move(text), role}}); };
    CHECK(check("actor:\n\tfunc f() -> actor:\n\t\treturn self\n", ScriptRole::Actor).result.success);
    CHECK(check("actor:\n\tfunc f() -> object:\n\t\treturn self\n", ScriptRole::Actor).find("Cannot return a value of type 'actor'") !=
          nullptr);
    CHECK(check("object o:\n\tfunc f() -> entity:\n\t\treturn self\n", ScriptRole::Room).result.success);
    const Project room = check("room:\n\tfunc ready():\n\t\tprint(self)\n", ScriptRole::Room);
    CHECK(room.find("'self' is not available in room handlers") != nullptr);
    const Project global = check("func f():\n\tprint(self)\n", ScriptRole::Global);
    CHECK(global.find("'self' is only available inside actor and object handlers") != nullptr);
    CHECK(global.hasNote("pass the actor or object it needs as a parameter"));
}

SCHUIMM_TEST(Behaviours_DeclarationContexts)
{
    const auto check = [](std::string text, ScriptRole role) { return compileFiles({{"A.sch", std::move(text), role}}); };
    const std::string room = "room:\n\tfunc ready():\n\t\tpass\n";
    const std::string actor = "actor:\n\tfunc talk_to():\n\t\tpass\n";
    CHECK(check(room, ScriptRole::Room).result.success);
    CHECK(check(actor, ScriptRole::Actor).result.success);
    CHECK(check(room, ScriptRole::Global).find("'room:' declarations belong in room scripts; this is a global script") != nullptr);
    CHECK(check(room, ScriptRole::Actor).find("'room:' declarations belong in room scripts; this is an actor script") != nullptr);
    const Project actorInRoom = check(actor, ScriptRole::Room);
    CHECK(actorInRoom.find("'actor:' declarations belong in actor scripts; this is a room script") != nullptr);
    CHECK(actorInRoom.hasNote("room scripts cannot declare actor handlers"));
    const Project twoRooms = check(room + room, ScriptRole::Room);
    CHECK(twoRooms.find("A script has at most one 'room:' declaration") != nullptr);
    CHECK(twoRooms.hasNote("The first one is on line 1"));
    CHECK(check(actor + actor, ScriptRole::Actor).find("at most one 'actor:' declaration") != nullptr);
    CHECK(check("room:\n\tpass\n", ScriptRole::Room).find("Only functions and variables are allowed in the room declaration") !=
          nullptr);
    CHECK(check("room:\n\troom:\n\t\tfunc f():\n\t\t\tpass\n", ScriptRole::Room)
              .find("Room and actor declarations are only allowed at the top level") != nullptr);
    CHECK(check("func f():\n\tactor:\n\t\tpass\n", ScriptRole::Global)
              .find("Room and actor declarations are only allowed at the top level") != nullptr);
    CHECK(check("room:\n", ScriptRole::Room).find("Expected an indented block of handler functions for the room declaration") !=
          nullptr);
    CHECK(check("actor:\n\tfunc f():\n\t\tpass\n\tfunc f():\n\t\tpass\n", ScriptRole::Actor)
              .find("Duplicate handler 'f' in the actor declaration") != nullptr);
    // "room" and "actor" stay ordinary words elsewhere.
    CHECK(check("var room = 1\n\nfunc f(r: actor) -> actor:\n\tvar actor_count = room\n\treturn r\n", ScriptRole::Global)
              .result.success);
}

SCHUIMM_TEST(Behaviours_HandlersAreCalledOnlyWithinTheirBehaviour)
{
    const Project fromGlobal = compileFiles({
        {"Rooms/Kitchen.sch", "room:\n\tfunc ready():\n\t\tpass\n", ScriptRole::Room},
        {"Scripts/Global.sch", "func start():\n\tready()\n"},
    });
    CHECK(fromGlobal.find("Undefined function 'ready'") != nullptr);
    CHECK(fromGlobal.hasNote("'ready' is a handler of the room declaration"));
    // Handlers shadow global functions of the same name, with a warning.
    const Project shadow = compileFiles({
        {"Scripts/Global.sch", "func greet():\n\tprint(\"global\")\n"},
        {"Actors/Cook.sch", "actor:\n\tfunc talk_to():\n\t\tgreet()\n\tfunc greet():\n\t\tprint(\"handler\")\n", ScriptRole::Actor},
    });
    REQUIRE(shadow.result.success);
    CHECK(hasWarning(shadow, "Handler 'greet' shadows the global function 'greet'"));
    CHECK(shadow.hasNote("declared in 'Scripts/Global.sch' on line 1"));
    Game game(shadow);
    game.dispatch(game.instance(BehaviourKind::Actor, {}, {}, Value::fromActor(ObjectRef{3})), "talk_to");
    game.run();
    CHECK_EQ(game.joined(), std::string("handler\n"));
}

SCHUIMM_TEST(Behaviours_BytecodeRulesForSelfAndCalls)
{
    const Project project = compileFiles(kitchenGame);
    REQUIRE(project.result.success);
    const Module good = project.result.module;
    const auto invalid = [](const Module& module, std::string_view text) {
        auto status = validateModule(module);
        if (status) return false;
        if (status.error().message.find(text) == std::string::npos) {
            std::fprintf(stderr, "  got: %s\n", status.error().message.c_str());
            return false;
        }
        return true;
    };
    const auto functionNamed = [&](const Module& module, std::string_view name) {
        for (std::size_t f = 0; f < module.functions.size(); ++f) {
            if (module.string(module.functions[f].nameIndex) == name) return f;
        }
        REQUIRE(false);
        return std::size_t{0};
    };
    // The room's handlers use no self: one that does is rejected.
    Module m = good;
    m.functions[functionNamed(m, "award_points")].code.insert(m.functions[functionNamed(m, "award_points")].code.begin(),
                                                             Instruction{Opcode::LoadSelf, 0, 0, 0});
    m.functions[functionNamed(m, "award_points")].positions.insert(m.functions[functionNamed(m, "award_points")].positions.begin(),
                                                                  SourcePosition{});
    CHECK(invalid(m, "self is used by a global function"));
    // A global function calling a handler.
    m = good;
    const std::size_t inspect = functionNamed(m, "inspect");
    bool patched = false;
    for (Instruction& instruction : m.functions[functionNamed(m, "announce")].code) {
        if (instruction.opcode == Opcode::CallNative && !patched) {
            instruction.opcode = Opcode::Call;
            instruction.b = static_cast<std::uint16_t>(inspect);
            instruction.c = 0;
            patched = true;
        }
    }
    REQUIRE(patched);
    CHECK(invalid(m, "calls handler 'oven.inspect'; a handler is started by event dispatch or by its own behaviour"));
}

SCHUIMM_TEST(Behaviours_ScriptsCompiledOnTheirOwnStillWork)
{
    // A single room script (schuimmc file.sch): its room and objects, dispatched by name too.
    const Compiled c = compileSource("room:\n\tvar n = 0\n\tfunc ready():\n\t\tn += 1\n\t\tprint(n)\n"
                                     "object lamp:\n\tfunc use():\n\t\tprint(self)\n");
    REQUIRE(c.result.success);
    NativeRegistry natives;
    std::vector<std::string> output;
    REQUIRE(registerStandardNatives(natives, [&](std::string_view, std::string_view text) { output.emplace_back(text); })
                .has_value());
    auto program = Program::link(c.result.module, natives);
    REQUIRE(program.has_value());
    ScriptScheduler scheduler;
    REQUIRE(scheduler.dispatch(*program, "lamp", "use", ObjectRef{12})->handled);
    const auto globals = GlobalState::create(*program);
    auto room = BehaviourInstance::create(globals, *(*program)->findBehaviour(BehaviourKind::Room, {}));
    REQUIRE(room.has_value());
    REQUIRE(scheduler.dispatch(*room, "ready")->handled);
    REQUIRE(scheduler.dispatch(*room, "ready")->handled);
    CHECK(!scheduler.dispatch(*room, "exit")->handled); // A missing handler is no error.
    for (int i = 0; i < 3; ++i) scheduler.update(0.1);
    CHECK(output == std::vector<std::string>({"object#12", "1", "2"}));
}

SCHUIMM_TEST(Behaviours_InstanceVariablesArePrivate)
{
    const auto check = [](std::string text, ScriptRole role) { return compileFiles({{"A.sch", std::move(text), role}}); };
    const std::string guard = "actor:\n\tvar patrol_count: int = 0\n\tfunc patrol():\n\t\tpatrol_count += 1\n";
    // No member access on another entity.
    CHECK(check(guard + "\nfunc reset(other: actor):\n\tother.patrol_count = 10\n", ScriptRole::Actor)
              .find("Variable 'other' of type 'actor' has no component 'patrol_count'") != nullptr);
    CHECK(check(guard + "\nfunc read(other: actor) -> int:\n\treturn other.patrol_count\n", ScriptRole::Actor)
              .find("A value of type 'actor' has no member 'patrol_count'") != nullptr);
    // A global function declared in the same file does not see the behaviour's variables.
    CHECK(check(guard + "\nfunc helper():\n\tprint(patrol_count)\n", ScriptRole::Actor)
              .find("Undefined variable 'patrol_count'") != nullptr);
    // Nor does another behaviour.
    CHECK(check("object a:\n\tvar n = 1\n\tfunc f():\n\t\tpass\nobject b:\n\tfunc g():\n\t\tprint(n)\n", ScriptRole::Room)
              .find("Undefined variable 'n'") != nullptr);
}
