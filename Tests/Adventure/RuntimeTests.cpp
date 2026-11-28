// The game session (AdventureRuntime) with the real game build, loaders, program and natives,
// headless: global persistence across rooms, room, object and actor state, coroutine ownership,
// stale references, new sessions, and failures (Adventure.md, "Game sessions").

#include "ScriptTestSupport.h"
#include "TestAssets.h"
#include "TestFramework.h"
#include "TestPng.h"

#include "Adventure/AdventureInterface.h"
#include "Adventure/AdventureRuntime.h"
#include "Adventure/GameBuild.h"
#include "Core/Common/StringBuilder.h"
#include "Core/Logging/Logger.h"
#include "Platform/FileSystem/FileSystem.h"
#include "Platform/FileSystem/Path.h"
#include "Script/Bytecode/BytecodeWriter.h"

#include <algorithm>
#include <string>
#include <string_view>

using namespace schuimm;
using namespace schuimm::adventure;

namespace {

std::span<const std::byte> bytesOf(std::string_view text) { return std::as_bytes(std::span(text.data(), text.size())); }

AssetPath asset(std::string_view text) { return AssetPath::parse(text).value(); }

constexpr std::string_view cookAnimation = R"({ "version": 1, "clips": {
  "idle": { "loop": true, "frames": [ { "image": "Art/sheet.png", "source": [0, 0, 16, 32], "pivot": [8, 32], "duration": 30 } ] },
  "walk": { "loop": true, "frames": [ { "image": "Art/sheet.png", "source": [16, 0, 16, 32], "pivot": [8, 32], "duration": 6 } ] }
} })";

constexpr std::string_view cookActor = R"({ "version": 1, "id": "cook", "script": "Actors/Cook.sch",
  "animation_resources": { "main": "Art/cook.anim.json" },
  "animations": { "idle": { "any": { "resource": "main", "clip": "idle" } },
                  "walk": { "any": { "resource": "main", "clip": "walk" } } },
  "default_animation": "idle", "default_facing": "south", "perspective_scale": false,
  "movement": { "speed": 100, "walk_animation": "walk", "idle_animation": "idle", "perspective_speed": false } })";

constexpr std::string_view kitchenRoom = R"({ "version": 1, "id": "kitchen", "size": { "width": 400, "height": 300 },
  "script": "Rooms/Kitchen.sch",
  "navigation": { "walkable_areas": [ { "id": "floor", "points": [[0, 0], [400, 0], [400, 300], [0, 300]] } ] },
  "objects": [ { "id": "mug", "position": [50, 50] }, { "id": "oven", "position": [200, 50] } ],
  "actors": [ { "id": "cook", "actor": "Actors/Cook.actor.json", "position": [100, 100] },
              { "id": "cook_2", "actor": "Actors/Cook.actor.json", "position": [300, 200] } ] })";

constexpr std::string_view gardenRoom = R"({ "version": 1, "id": "garden", "size": { "width": 400, "height": 300 },
  "script": "Rooms/Garden.sch",
  "actors": [ { "id": "cook", "actor": "Actors/Cook.actor.json", "position": [50, 50] } ] })";

constexpr std::string_view game = R"({ "version": 1, "id": "kitchen_game", "program": "Scripts/Game.schb",
  "global_scripts": ["Scripts/Global.sch", "Scripts/Interface.sch"],
  "rooms": ["Rooms/Kitchen.room.json", "Rooms/Garden.room.json"] })";

constexpr std::string_view globalScript = R"(var score: int = 0
var ticks: int = 0
var remembered: actor
var game_state: GameState = GameState.STARTED

func award_points(amount: int):
	score += amount

func delayed_reward():
	wait(0.5)
	score += 100
	print("delayed reward")

func session_ticker():
	while True:
		wait(0.1)
		ticks += 1

func check_cook():
	print("remembered valid " + str(is_valid(remembered)))
	print("remembered is the new cook " + str(remembered == get_actor("cook")))
	print("new cook valid " + str(is_valid(get_actor("cook"))))
)";

constexpr std::string_view interfaceScript = R"(enum GameState:
	STARTED
	FINISHED

enum Verb:
	LOOK
	USE

var selected_verb: Verb = Verb.LOOK
)";

constexpr std::string_view kitchenScript = R"(var kitchen_visited: bool = False
var oven_repaired: bool = False

func describe_mug():
	print("My favourite coffee mug.")

room:
	var visit_action_count: int = 0

	func init():
		set_visible(get_actor("cook_2"), False)

	func ready():
		visit_action_count += 1
		kitchen_visited = True
		print("kitchen ready " + str(visit_action_count))

	func spawn_reward():
		spawn delayed_reward()

object mug:
	func look_at():
		if selected_verb == Verb.LOOK:
			describe_mug()

object oven:
	var inspection_count: int = 0

	func look_at():
		inspection_count += 1
		print("oven inspected " + str(inspection_count))

	func use():
		if not oven_repaired:
			oven_repaired = True
			award_points(10)
)";

constexpr std::string_view cookScript = R"(var cook_has_been_greeted: bool = False

actor:
	var conversation_count: int = 0

	func talk_to():
		conversation_count += 1
		if not cook_has_been_greeted:
			cook_has_been_greeted = True
			print("Good morning!")
		print("conversations " + str(conversation_count))

	func remember_me():
		remembered = self

	func reward_later():
		spawn delayed_reward()

	func leave():
		walk_to(self, vec2(350, 250))
		print("cook left")
)";

constexpr std::string_view gardenScript = R"(room:
	func ready():
		if kitchen_visited:
			print("The kitchen has already been visited.")
		describe_mug()
		print("score " + str(score))
)";

struct Game {
    std::string root = test::makeScratchDirectory("runtime");
    Logger logger{LogLevel::Off};
    script::ScriptHost host{logger};
    std::unique_ptr<AssetLocator> locator;
    std::optional<AdventureNatives> natives;
    std::unique_ptr<AdventureRuntime> runtime;
    std::vector<std::string> output;
    std::vector<script::ScriptError> errors;

    Game()
    {
        write("Art/sheet.png", test::pngHeader(64, 64));
        write("Art/cook.anim.json", cookAnimation);
        write("Actors/Cook.actor.json", cookActor);
        write("Actors/Cook.sch", cookScript);
        write("Rooms/Kitchen.room.json", kitchenRoom);
        write("Rooms/Kitchen.sch", kitchenScript);
        write("Rooms/Garden.room.json", gardenRoom);
        write("Rooms/Garden.sch", gardenScript);
        write("Scripts/Global.sch", globalScript);
        write("Scripts/Interface.sch", interfaceScript);
        write("Game.game.json", game);
        locator = std::make_unique<AssetLocator>(AssetLocator::create(root).value());
        build();
        host.setOutputCallback([this](std::string_view, std::string_view text) { output.emplace_back(text); });
        host.setEventCallback([this](const script::CoroutineEvent& event) {
            if (event.kind == script::CoroutineEvent::Kind::Failed && event.error != nullptr) errors.push_back(*event.error);
        });
        auto bound = AdventureNatives::bind(host.natives());
        REQUIRE(bound.has_value());
        natives = std::move(*bound);
        newSession();
    }

    ~Game()
    {
        runtime.reset();
        host.stopAll();
    }

    void write(std::string_view path, std::string_view text) const { write(path, bytesOf(text)); }
    void write(std::string_view path, std::span<const std::byte> bytes) const
    {
        const std::string file = path::join(root, path);
        REQUIRE(fs::createDirectories(path::parent(file)).has_value());
        REQUIRE(fs::writeFile(file, bytes).has_value());
    }

    // The game build, as schuimmc --game does it: the program written where the game resource
    // says.
    void build()
    {
        auto built = compileGame(*locator, asset("Game.game.json"));
        if (!built) std::fprintf(stderr, "%s\n", built.error().message.c_str());
        REQUIRE(built.has_value());
        if (!(*built)->result.success) std::fprintf(stderr, "%s", script::formatDiagnostics((*built)->result).c_str());
        REQUIRE((*built)->result.success);
        auto bytes = script::writeBytecode((*built)->result.module);
        REQUIRE(bytes.has_value());
        write("Scripts/Game.schb", *bytes);
    }

    void newSession()
    {
        runtime.reset();
        auto created = AdventureRuntime::create(host, *natives, *locator, asset("Game.game.json"));
        if (!created) std::fprintf(stderr, "%s\n", created.error().message.c_str());
        REQUIRE(created.has_value());
        runtime = std::move(*created);
    }

    void enter(std::string_view room)
    {
        const Status status = runtime->enterRoom(asset(room));
        if (!status) std::fprintf(stderr, "%s\n", status.error().message.c_str());
        REQUIRE(status.has_value());
    }

    void handled(const Result<script::DispatchResult>& result)
    {
        if (!result) std::fprintf(stderr, "%s\n", result.error().message.c_str());
        REQUIRE(result.has_value());
        REQUIRE(result->handled);
    }

    // Frames in the documented order: the runtime (room update, publish), then the scripts.
    void frames(int count = 1, double dt = 0.1)
    {
        for (int i = 0; i < count; ++i) {
            runtime->update(dt);
            host.update(dt);
        }
    }

    [[nodiscard]] std::string joined()
    {
        std::string text;
        for (const std::string& line : output) text += line + "\n";
        for (const script::ScriptError& error : errors) text += script::describe(error) + "\n";
        output.clear();
        errors.clear();
        return text;
    }

    [[nodiscard]] std::int64_t global(std::string_view name) const { return runtime->globals()->find(name)->asInt(); }
};

} // namespace

SCHUIMM_TEST(Runtime_TheBuildFindsEverySourceOnce)
{
    Game g;
    auto sources = discoverGameSources(*g.locator, asset("Game.game.json"));
    REQUIRE(sources.has_value());
    std::vector<std::string> names;
    for (const script::ProjectSource& source : sources->sources) names.push_back(source.file->name());
    // Global scripts in listed order, then per room its script and its actors' scripts; the cook's
    // script, used by three placements in two rooms, takes part once.
    CHECK(names == std::vector<std::string>({"Scripts/Global.sch", "Scripts/Interface.sch", "Rooms/Kitchen.sch",
                                             "Actors/Cook.sch", "Rooms/Garden.sch"}));
    CHECK(sources->sources[3].role == script::ScriptRole::Actor);
    CHECK(sources->sources[4].role == script::ScriptRole::Room);
    // A source in two roles, a missing source, a missing room.
    g.write("Game.game.json", R"({ "version": 1, "id": "g", "program": "Scripts/Game.schb",
        "global_scripts": ["Rooms/Kitchen.sch"], "rooms": ["Rooms/Kitchen.room.json"] })");
    auto twoRoles = discoverGameSources(*g.locator, asset("Game.game.json"));
    REQUIRE(!twoRoles.has_value());
    CHECK(twoRoles.error().message.find("'Rooms/Kitchen.sch' is used as a room script, but Game.game.json uses it as a "
                                        "global script") != std::string::npos);
    g.write("Game.game.json", R"({ "version": 1, "id": "g", "program": "Scripts/Game.schb",
        "global_scripts": ["Scripts/Missing.sch"], "rooms": ["Rooms/Kitchen.room.json"] })");
    auto missing = discoverGameSources(*g.locator, asset("Game.game.json"));
    REQUIRE(!missing.has_value());
    CHECK(missing.error().message.find("Game.game.json: script 'Scripts/Missing.sch'") != std::string::npos);
    g.write("Game.game.json", R"({ "version": 1, "id": "g", "program": "Scripts/Game.schb", "rooms": ["Rooms/Attic.room.json"] })");
    CHECK(!discoverGameSources(*g.locator, asset("Game.game.json")).has_value());
}

SCHUIMM_TEST(Runtime_TheBuildChecksRoomScriptsAgainstTheirRooms)
{
    Game g;
    const auto build = [&]() {
        auto built = compileGame(*g.locator, asset("Game.game.json"));
        REQUIRE(built.has_value());
        return std::move(*built);
    };
    const auto count = [](const script::CompileResult& result, std::string_view text) {
        return std::count_if(result.diagnostics.all().begin(), result.diagnostics.all().end(),
                             [&](const script::Diagnostic& d) { return d.message.find(text) != std::string::npos; });
    };
    // The script and the room resource are each valid, but do not fit: found by the build, at the
    // declaration, before anyone enters the room.
    g.write("Rooms/Kitchen.sch", std::string(kitchenScript) + "\nobject coffee_mug:\n\tfunc look_at():\n\t\tprint(\"My favourite mug.\")\n");
    auto mismatch = build();
    CHECK(!mismatch->result.success);
    const std::string text = script::formatDiagnostics(mismatch->result);
    const std::size_t line = std::count(kitchenScript.begin(), kitchenScript.end(), '\n') + 2;
    CHECK(text.find(concat("Rooms/Kitchen.sch:", line, ":8: error: Room 'kitchen' (Rooms/Kitchen.room.json) has no object "
                           "'coffee_mug'.")) != std::string::npos);
    CHECK(text.find("note: The room's objects are 'mug', 'oven'.") != std::string::npos);
    // Every mismatch of every room in one build; a script shared by two rooms is checked against
    // each of them.
    g.write("Rooms/Kitchen.sch", std::string(kitchenScript) + "\nobject sink:\n\tfunc use():\n\t\tpass\n"
                                                              "object kettle:\n\tfunc use():\n\t\tpass\n");
    g.write("Rooms/Scullery.room.json", R"({ "version": 1, "id": "scullery", "size": { "width": 400, "height": 300 },
        "script": "Rooms/Kitchen.sch", "objects": [ { "id": "mug", "position": [1, 1] }, { "id": "oven", "position": [2, 2] },
        { "id": "sink", "position": [3, 3] } ] })");
    g.write("Game.game.json", R"({ "version": 1, "id": "kitchen_game", "program": "Scripts/Game.schb",
        "global_scripts": ["Scripts/Global.sch", "Scripts/Interface.sch"],
        "rooms": ["Rooms/Kitchen.room.json", "Rooms/Garden.room.json", "Rooms/Scullery.room.json"] })");
    auto several = build();
    CHECK(!several->result.success);
    CHECK_EQ(count(several->result, "Room 'kitchen' (Rooms/Kitchen.room.json) has no object 'sink'"), 1);
    CHECK_EQ(count(several->result, "Room 'kitchen' (Rooms/Kitchen.room.json) has no object 'kettle'"), 1);
    CHECK_EQ(count(several->result, "Room 'scullery' (Rooms/Scullery.room.json) has no object 'kettle'"), 1);
    CHECK_EQ(count(several->result, "has no object"), 3); // The scullery has a sink.
    // Fixed: the build succeeds, and the runtime keeps checking what it loads.
    g.write("Rooms/Kitchen.sch", kitchenScript);
    CHECK(build()->result.success);
    // Placements are checked against their actor resources too.
    g.write("Rooms/Garden.room.json", R"({ "version": 1, "id": "garden", "size": { "width": 400, "height": 300 },
        "script": "Rooms/Garden.sch",
        "actors": [ { "id": "cook", "actor": "Actors/Cook.actor.json", "position": [50, 50], "animation": "dance" } ] })");
    auto placement = compileGame(*g.locator, asset("Game.game.json"));
    REQUIRE(!placement.has_value());
    CHECK(placement.error().message.find("Rooms/Garden.room.json: actors[0] ('cook'):") != std::string::npos);
    CHECK(placement.error().message.find("dance") != std::string::npos);
}

SCHUIMM_TEST(Runtime_GameResourceValidation)
{
    const auto decode = [](std::string_view text) { return decodeGameResource(bytesOf(text), "Game.game.json"); };
    auto good = decode(game);
    REQUIRE(good.has_value());
    CHECK_EQ(good->id, std::string("kitchen_game"));
    CHECK(good->program == asset("Scripts/Game.schb"));
    CHECK_EQ(good->globalScripts.size(), std::size_t{2});
    CHECK(good->hasRoom(asset("Rooms/Garden.room.json")));
    CHECK(!good->hasRoom(asset("Rooms/Attic.room.json")));
    const auto fails = [&](std::string_view text, std::string_view message) {
        auto result = decode(text);
        if (result) return false;
        if (result.error().message.find(message) == std::string::npos) {
            std::fprintf(stderr, "  got: %s\n", result.error().message.c_str());
            return false;
        }
        return true;
    };
    CHECK(fails(R"({ "version": 2, "id": "g", "program": "a.schb", "rooms": ["r.room.json"] })", "version"));
    CHECK(fails(R"({ "version": 1, "id": "g", "program": "a.sch", "rooms": ["r.room.json"] })",
                "program: must name a compiled program (.schb)"));
    CHECK(fails(R"({ "version": 1, "id": "g", "program": "a.schb", "rooms": [] })", "rooms: a game has at least one room"));
    CHECK(fails(R"({ "version": 1, "id": "g", "program": "a.schb", "rooms": ["r.room.json", "r.room.json"] })",
                "rooms[1]: 'r.room.json' is listed twice"));
    CHECK(fails(R"({ "version": 1, "id": "g", "program": "a.schb", "global_scripts": ["a.schb"], "rooms": ["r.room.json"] })",
                "global_scripts[0]: must name a script source (.sch)"));
    CHECK(fails(R"({ "version": 1, "id": "g", "program": "a.schb", "rooms": ["r.room.json"], "display": {} })", "display"));
}

SCHUIMM_TEST(Runtime_GlobalsPersistWhileRoomsComeAndGo)
{
    Game g;
    g.enter("Rooms/Kitchen.room.json");
    g.handled(g.runtime->dispatchRoom("ready"));
    g.handled(g.runtime->dispatchObject("oven", "use"));
    g.frames();
    CHECK_EQ(g.joined(), std::string("kitchen ready 1\n"));
    CHECK_EQ(g.global("score"), 10);
    // Another room reads what the kitchen's script changed; the kitchen is gone meanwhile.
    g.enter("Rooms/Garden.room.json");
    g.handled(g.runtime->dispatchRoom("ready"));
    g.frames();
    CHECK_EQ(g.joined(), std::string("The kitchen has already been visited.\nMy favourite coffee mug.\nscore 10\n"));
    // Back in a new kitchen instance: the globals kept their values, the oven is not repaired twice.
    g.enter("Rooms/Kitchen.room.json");
    g.handled(g.runtime->dispatchObject("oven", "use"));
    g.handled(g.runtime->dispatchRoom("ready"));
    g.frames();
    CHECK_EQ(g.global("score"), 10);
    CHECK(g.runtime->globals()->find("oven_repaired")->asBool());
    CHECK_EQ(g.joined(), std::string("kitchen ready 1\n")); // Room variables started afresh.
}

SCHUIMM_TEST(Runtime_GlobalFunctionsOfAnInactiveRoom)
{
    Game g;
    g.enter("Rooms/Garden.room.json");
    // describe_mug is declared in the kitchen's script; the kitchen is not instantiated.
    REQUIRE(g.runtime->start("describe_mug").has_value());
    g.frames();
    CHECK_EQ(g.joined(), std::string("My favourite coffee mug.\n"));
    // Also without any active room.
    g.runtime->leaveRoom();
    REQUIRE(g.runtime->start("describe_mug").has_value());
    g.frames();
    CHECK_EQ(g.joined(), std::string("My favourite coffee mug.\n"));
}

SCHUIMM_TEST(Runtime_RoomAndObjectStateBelongToTheInstance)
{
    Game g;
    g.enter("Rooms/Kitchen.room.json");
    g.handled(g.runtime->dispatchRoom("ready"));
    g.handled(g.runtime->dispatchRoom("ready"));
    g.handled(g.runtime->dispatchObject("oven", "look_at"));
    g.handled(g.runtime->dispatchObject("oven", "look_at"));
    g.handled(g.runtime->dispatchObject("mug", "look_at"));
    g.frames();
    CHECK_EQ(g.joined(), std::string("kitchen ready 1\nkitchen ready 2\noven inspected 1\noven inspected 2\n"
                                     "My favourite coffee mug.\n"));
    const std::shared_ptr<script::BehaviourInstance> oven = g.runtime->activeRoom()->objectScript("oven");
    CHECK_EQ(oven->find("inspection_count")->asInt(), 2);
    // A new instance of the room: room and object variables start again.
    g.enter("Rooms/Garden.room.json");
    CHECK(oven->isExpired());
    g.enter("Rooms/Kitchen.room.json");
    g.output.clear();
    g.handled(g.runtime->dispatchRoom("ready"));
    g.handled(g.runtime->dispatchObject("oven", "look_at"));
    g.frames();
    CHECK_EQ(g.joined(), std::string("kitchen ready 1\noven inspected 1\n"));
    // A missing handler is no error; wrong arguments are.
    CHECK(!g.runtime->dispatchObject("mug", "use")->handled);
    CHECK(!g.runtime->dispatchObject("oven", "use", {script::Value::fromInt(1)}).has_value());
    CHECK(!g.runtime->dispatchObject("sink", "use").has_value()); // No such object.
}

SCHUIMM_TEST(Runtime_ActorsOfOneScriptHaveTheirOwnState)
{
    Game g;
    g.enter("Rooms/Kitchen.room.json");
    g.handled(g.runtime->dispatchActor("cook", "talk_to"));
    g.handled(g.runtime->dispatchActor("cook", "talk_to"));
    g.handled(g.runtime->dispatchActor("cook_2", "talk_to"));
    g.frames();
    CHECK_EQ(g.joined(), std::string("Good morning!\nconversations 1\nconversations 2\nconversations 1\n"));
    room::RoomInstance& kitchen = *g.runtime->activeRoom();
    CHECK_EQ(kitchen.actorScript("cook")->find("conversation_count")->asInt(), 2);
    CHECK_EQ(kitchen.actorScript("cook_2")->find("conversation_count")->asInt(), 1);
    // The global declared in the actor script is shared by both cooks.
    CHECK(g.runtime->globals()->find("cook_has_been_greeted")->asBool());
    CHECK(!g.runtime->dispatchActor("nobody", "talk_to").has_value());
    CHECK(!g.runtime->dispatchActor("cook", "dance")->handled);
}

SCHUIMM_TEST(Runtime_InitSeesEveryEntityOfTheRoom)
{
    Game g;
    g.enter("Rooms/Kitchen.room.json");
    CHECK(g.runtime->activeRoom()->actor("cook_2")->visible());
    g.handled(g.runtime->dispatchRoom("init"));
    g.frames();
    CHECK(g.joined().empty());
    CHECK(!g.runtime->activeRoom()->actor("cook_2")->visible());
}

SCHUIMM_TEST(Runtime_CoroutinesKeepTheirOwner)
{
    Game g;
    g.enter("Rooms/Kitchen.room.json");
    REQUIRE(g.runtime->start("session_ticker").has_value());
    g.handled(g.runtime->dispatchRoom("spawn_reward"));         // Room-owned: spawns a global function.
    g.handled(g.runtime->dispatchActor("cook", "reward_later")); // Actor-owned, the same.
    g.frames(2);
    CHECK_EQ(g.runtime->activeRoom()->activeCoroutineCount(), std::size_t{2}); // The two delayed rewards.
    CHECK_EQ(g.runtime->sessionCoroutineCount(), std::size_t{1});
    // Destroying the room cancels its own and its actors' coroutines, also the global functions
    // they spawned; the session's ticker goes on.
    g.enter("Rooms/Garden.room.json");
    const std::int64_t ticks = g.global("ticks");
    g.frames(10);
    CHECK_EQ(g.joined(), std::string());
    CHECK_EQ(g.global("score"), 0);
    CHECK(g.global("ticks") > ticks);
    CHECK_EQ(g.runtime->sessionCoroutineCount(), std::size_t{1});
    // A session-owned call of the same global function survives room changes.
    REQUIRE(g.runtime->start("delayed_reward").has_value());
    g.frames();
    g.enter("Rooms/Kitchen.room.json");
    g.frames(10);
    CHECK_EQ(g.joined(), std::string("delayed reward\n"));
    CHECK_EQ(g.global("score"), 100);
}

SCHUIMM_TEST(Runtime_OldReferencesStayInvalid)
{
    Game g;
    g.enter("Rooms/Kitchen.room.json");
    g.handled(g.runtime->dispatchActor("cook", "remember_me"));
    g.frames();
    // The kitchen and its cook are destroyed and created again: same placement id, new actor.
    g.enter("Rooms/Garden.room.json");
    g.enter("Rooms/Kitchen.room.json");
    REQUIRE(g.runtime->start("check_cook").has_value());
    g.frames();
    CHECK_EQ(g.joined(), std::string("remembered valid False\nremembered is the new cook False\nnew cook valid True\n"));
}

SCHUIMM_TEST(Runtime_ANewSessionStartsAfresh)
{
    Game g;
    g.enter("Rooms/Kitchen.room.json");
    REQUIRE(g.runtime->start("session_ticker").has_value());
    g.handled(g.runtime->dispatchObject("oven", "use"));
    g.handled(g.runtime->dispatchActor("cook", "leave")); // Suspended in walk_to: a pending operation.
    g.frames(2);
    REQUIRE(g.runtime->activeRoom()->actor("cook")->isWalking());
    CHECK_EQ(g.global("score"), 10);
    // Ending the session with suspended coroutines and a pending walk is safe.
    g.runtime.reset();
    CHECK_EQ(g.host.activeCount(), std::size_t{0});
    CHECK(!g.natives->inSession());
    // Without a session, the adventure natives refuse to work.
    script::SourceFile probe("Probe.sch", "func main():\n\tprint(is_valid(get_actor(\"cook\")))\n");
    const script::ProjectSource source{&probe, script::ScriptRole::Global};
    auto compiled = script::compileProject(std::span<const script::ProjectSource>(&source, 1),
                                           script::CompileOptions{.optimize = true, .interfaces = {&adventureInterface()}});
    REQUIRE(compiled.success);
    auto program = script::Program::link(compiled.module, g.host.natives(), "probe");
    REQUIRE(program.has_value());
    REQUIRE(g.host.start(*program, "main").has_value());
    g.host.update(0.1);
    REQUIRE(g.errors.size() == 1);
    CHECK(g.errors[0].message.find("no adventure game session is running") != std::string::npos);
    g.errors.clear();
    // A new game: every global at its initial value, nothing left running.
    g.newSession();
    CHECK_EQ(g.global("score"), 0);
    CHECK_EQ(g.global("ticks"), 0);
    CHECK(!g.runtime->globals()->find("oven_repaired")->asBool());
    CHECK(g.runtime->activeRoom() == nullptr);
    CHECK_EQ(g.runtime->sessionCoroutineCount(), std::size_t{0});
    g.enter("Rooms/Kitchen.room.json");
    g.handled(g.runtime->dispatchRoom("ready"));
    g.frames();
    CHECK_EQ(g.joined(), std::string("kitchen ready 1\n"));
}

SCHUIMM_TEST(Runtime_FailuresLeaveTheSessionUsable)
{
    Game g;
    // One session per natives binding.
    auto second = AdventureRuntime::create(g.host, *g.natives, *g.locator, asset("Game.game.json"));
    REQUIRE(!second.has_value());
    CHECK(second.error().code == ErrorCode::InvalidState);
    // Only the game's rooms; a failed enter keeps the current room.
    g.enter("Rooms/Kitchen.room.json");
    room::RoomInstance* kitchen = g.runtime->activeRoom();
    const Status unknown = g.runtime->enterRoom(asset("Rooms/Attic.room.json"));
    REQUIRE(!unknown.has_value());
    CHECK(unknown.error().code == ErrorCode::InvalidArgument);
    CHECK(unknown.error().message.find("is not a room of the game 'kitchen_game'") != std::string::npos);
    CHECK(g.runtime->activeRoom() == kitchen);
    g.write("Rooms/Garden.room.json", R"({ "version": 1, "id": "garden", "size": { "width": 0, "height": 300 } })");
    g.runtime->library().unloadAll();
    CHECK(!g.runtime->enterRoom(asset("Rooms/Garden.room.json")).has_value());
    CHECK(g.runtime->activeRoom() == kitchen);
    // Without a room, dispatching does nothing.
    g.runtime->leaveRoom();
    CHECK(!g.runtime->dispatchRoom("ready")->handled);
    CHECK(!g.runtime->dispatchObject("oven", "use")->handled);
    CHECK(!g.runtime->dispatchActor("cook", "talk_to")->handled);
    CHECK(!g.runtime->start("no_such_function").has_value());
    // A missing or broken program fails the session's creation.
    g.runtime.reset();
    REQUIRE(fs::remove(path::join(g.root, "Scripts/Game.schb")).has_value());
    auto noProgram = AdventureRuntime::create(g.host, *g.natives, *g.locator, asset("Game.game.json"));
    REQUIRE(!noProgram.has_value());
    CHECK(noProgram.error().message.find("Game.game.json: program:") != std::string::npos);
    g.write("Scripts/Game.schb", "not bytecode");
    auto broken = AdventureRuntime::create(g.host, *g.natives, *g.locator, asset("Game.game.json"));
    REQUIRE(!broken.has_value());
    CHECK(broken.error().message.find("cannot be loaded") != std::string::npos);
    CHECK(!g.natives->inSession());
}
