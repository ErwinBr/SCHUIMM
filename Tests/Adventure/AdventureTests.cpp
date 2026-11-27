// The adventure scripting API (Stage D of Milestone 4.4): interface/binding consistency, entity
// references, state natives, walk_to and its outcomes, animation operations, camera, the update
// order and room switching. Headless: images are PNG headers only; scripts are compiled
// in-process with the adventure interface and run through a real ScriptHost.

#include "ScriptTestSupport.h"
#include "TestAssets.h"
#include "TestFramework.h"
#include "TestPng.h"

#include "Adventure/AdventureContext.h"
#include "Adventure/AdventureInterface.h"
#include "Adventure/AdventureRuntime.h"
#include "Adventure/GameBuild.h"
#include "Core/Logging/Logger.h"
#include "Platform/FileSystem/FileSystem.h"
#include "Platform/FileSystem/Path.h"
#include "Room/RoomInstance.h"
#include "Room/RoomLibrary.h"
#include "Script/Bytecode/BytecodeWriter.h"
#include "Script/Runtime/ScriptHost.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

using namespace schuimm;
using namespace schuimm::adventure;
using room::Facing;

namespace {

std::span<const std::byte> bytesOf(std::string_view text) { return std::as_bytes(std::span(text.data(), text.size())); }

bool contains(const std::string& text, std::string_view part)
{
    if (text.find(part) != std::string::npos) return true;
    std::fprintf(stderr, "    text: %s\n    expected to contain: %.*s\n", text.c_str(), static_cast<int>(part.size()), part.data());
    return false;
}

constexpr std::string_view walkerAnimation = R"({ "version": 1, "clips": {
  "idle": { "loop": true, "frames": [ { "image": "Art/sheet.png", "source": [0, 0, 16, 32], "pivot": [8, 32], "duration": 30 } ] },
  "walk": { "loop": true, "frames": [ { "image": "Art/sheet.png", "source": [16, 0, 16, 32], "pivot": [8, 32], "duration": 6 } ] },
  "wave": { "loop": false, "frames": [
    { "image": "Art/sheet.png", "source": [32, 0, 16, 32], "pivot": [8, 32], "duration": 6 },
    { "image": "Art/sheet.png", "source": [48, 0, 16, 32], "pivot": [8, 32], "duration": 6 } ] },
  "north": { "loop": true, "frames": [ { "image": "Art/sheet.png", "source": [0, 32, 16, 32], "pivot": [8, 32], "duration": 30 } ] }
} })";

// Walks in every direction ("any"); "look_up" exists only facing north.
constexpr std::string_view walkerActor = R"({ "version": 1, "id": "walker",
  "animation_resources": { "main": "Art/walker.anim.json" },
  "animations": {
    "idle": { "any": { "resource": "main", "clip": "idle" } },
    "walk": { "any": { "resource": "main", "clip": "walk" } },
    "wave": { "any": { "resource": "main", "clip": "wave" } },
    "look_up": { "north": { "resource": "main", "clip": "north" } } },
  "default_animation": "idle", "default_facing": "south", "perspective_scale": false,
  "movement": { "speed": 100, "walk_animation": "walk", "idle_animation": "idle", "perspective_speed": false } })";

constexpr std::string_view plantActor = R"({ "version": 1, "id": "plant",
  "animation_resources": { "main": "Art/walker.anim.json" },
  "animations": { "idle": { "any": { "resource": "main", "clip": "idle" } } },
  "default_animation": "idle", "default_facing": "south" })";

constexpr std::string_view fanAnimation = R"({ "version": 1, "clips": {
  "still": { "loop": true, "frames": [ { "image": "Art/sheet.png", "source": [0, 0, 16, 16], "pivot": [8, 16], "duration": 10 } ] },
  "spin": { "loop": true, "frames": [ { "image": "Art/sheet.png", "source": [16, 0, 16, 16], "pivot": [8, 16], "duration": 2 } ] },
  "burst": { "loop": false, "frames": [ { "image": "Art/sheet.png", "source": [32, 0, 16, 16], "pivot": [8, 16], "duration": 12 } ] }
} })";

// A floor (0..400 x 0..300) and a disconnected balcony; hero and guard can walk, the plant cannot.
constexpr std::string_view plazaRoom = R"({ "version": 1, "id": "plaza", "size": { "width": 600, "height": 300 },
  "navigation": { "walkable_areas": [
    { "id": "floor", "points": [[0, 0], [400, 0], [400, 300], [0, 300]] },
    { "id": "balcony", "points": [[500, 0], [600, 0], [600, 100], [500, 100]] } ] },
  "objects": [
    { "id": "fan", "position": [300, 50],
      "animation_resources": { "main": "Art/fan.anim.json" },
      "animations": { "still": { "resource": "main", "clip": "still" }, "spin": { "resource": "main", "clip": "spin" },
                      "burst": { "resource": "main", "clip": "burst" } },
      "default_animation": "still" },
    { "id": "lamp", "sprite": "Art/sheet.png", "position": [20, 20] },
    { "id": "hero", "position": [5, 5] } ],
  "actors": [
    { "id": "hero", "actor": "Art/walker.actor.json", "position": [100, 100] },
    { "id": "guard", "actor": "Art/walker.actor.json", "position": [300, 200] },
    { "id": "plant", "actor": "Art/plant.actor.json", "position": [50, 250] } ] })";

struct Fixture {
    std::string root = test::makeScratchDirectory("adventure");
    Logger logger{LogLevel::Off};
    script::ScriptHost host{logger};
    std::unique_ptr<AssetLocator> locator;
    std::unique_ptr<room::RoomLibrary> library;
    std::unique_ptr<room::RoomInstance> room;
    AdventureContext adventure;
    Camera2D camera{Extent2D{320, 200}};
    std::vector<std::string> output;
    std::vector<script::ScriptError> errors;
    int cancelled = 0;

    Fixture()
    {
        writeBytes("Art/sheet.png", test::pngHeader(64, 64));
        write("Art/walker.anim.json", walkerAnimation);
        write("Art/fan.anim.json", fanAnimation);
        write("Art/walker.actor.json", walkerActor);
        write("Art/plant.actor.json", plantActor);
        write("Rooms/Plaza.room.json", plazaRoom);
        locator = std::make_unique<AssetLocator>(AssetLocator::create(root).value());
        REQUIRE(adventure.bind(host.natives()).has_value());
        library = std::make_unique<room::RoomLibrary>(*locator);
        room = makeRoom();
        adventure.setActiveRoom(room.get());
        adventure.setCamera(&camera);
        host.setOutputCallback([this](std::string_view, std::string_view text) { output.emplace_back(text); });
        host.setEventCallback([this](const script::CoroutineEvent& event) {
            if (event.kind == script::CoroutineEvent::Kind::Failed && event.error != nullptr) errors.push_back(*event.error);
            if (event.kind == script::CoroutineEvent::Kind::Cancelled) ++cancelled;
        });
    }

    ~Fixture()
    {
        host.stopAll(); // Before the context the natives capture.
        adventure.setActiveRoom(nullptr);
    }

    void write(std::string_view asset, std::string_view text) const { writeBytes(asset, bytesOf(text)); }

    void writeBytes(std::string_view asset, std::span<const std::byte> bytes) const
    {
        const std::string file = path::join(root, asset);
        REQUIRE(fs::createDirectories(path::parent(file)).has_value());
        REQUIRE(fs::writeFile(file, bytes).has_value());
    }

    std::unique_ptr<room::RoomInstance> makeRoom()
    {
        auto loaded = library->load(AssetPath::parse("Rooms/Plaza.room.json").value());
        if (!loaded) std::fprintf(stderr, "%s\n", loaded.error().message.c_str());
        REQUIRE(loaded.has_value());
        auto instance = room::RoomInstance::create(*loaded, host);
        REQUIRE(instance.has_value());
        return std::move(*instance);
    }

    std::shared_ptr<const script::Program> load(const std::string& source)
    {
        const script::test::Compiled compiled = script::test::compileSource(
            source, script::CompileOptions{.optimize = true, .interfaces = {&adventureInterface()}}, "plaza.sch");
        if (!compiled.result.success) std::fprintf(stderr, "%s", compiled.diagnostics().c_str());
        REQUIRE(compiled.result.success);
        auto bytes = script::writeBytecode(compiled.result.module);
        REQUIRE(bytes.has_value());
        auto program = host.load(*bytes, "plaza.sch");
        if (!program) std::fprintf(stderr, "%s\n", program.error().message.c_str());
        REQUIRE(program.has_value());
        return *program;
    }

    script::CoroutineHandle start(const std::string& source, const char* function = "main")
    {
        auto handle = host.start(load(source), function);
        REQUIRE(handle.has_value());
        return *handle;
    }

    // One frame in the documented order: room update, publish, scripts.
    void frame(double dt = 0.1)
    {
        if (room) room->update(dt);
        adventure.publish();
        host.update(dt);
    }

    void frames(int count, double dt = 0.1)
    {
        for (int i = 0; i < count; ++i) frame(dt);
    }

    room::ActorInstance& actor(std::string_view id) { return *room->actor(id); }

    [[nodiscard]] std::string joined() const
    {
        std::string text;
        for (const std::string& line : output) text += line + "\n";
        return text;
    }

    [[nodiscard]] std::string firstError() const { return errors.empty() ? std::string() : script::describe(errors.front()); }
};

bool near(Vec2 a, Vec2 b, double tolerance = 1e-3)
{
    const bool ok = distance(a, b) <= tolerance;
    if (!ok) std::fprintf(stderr, "    (%g, %g) is not near (%g, %g)\n", static_cast<double>(a.x), static_cast<double>(a.y),
                          static_cast<double>(b.x), static_cast<double>(b.y));
    return ok;
}

} // namespace

// --- Interface and binding ---------------------------------------------------------------------

SCHUIMM_TEST(Adventure_InterfaceAndBindingAgree)
{
    const script::NativeInterface& interface = adventureInterface();
    CHECK(script::checkInterface(interface).empty());
    CHECK_EQ(interface.name, std::string("adventure"));
    REQUIRE(interface.findEnum("Direction") != nullptr);
    CHECK_EQ(interface.findEnum("Direction")->members.size(), std::size_t{8});
    CHECK(interface.findFunction("walk_to")->category == script::NativeCategory::Suspending);
    CHECK(interface.findFunction("play_animation")->category == script::NativeCategory::Background);
    CHECK(interface.findFunction("set_position")->category == script::NativeCategory::Immediate);

    Logger logger{LogLevel::Off};
    script::ScriptHost host{logger};
    AdventureContext context;
    REQUIRE(context.bind(host.natives()).has_value());
    for (const script::NativeDeclaration& declaration : interface.functions) {
        const auto bound = host.natives().find(declaration.name);
        REQUIRE(bound != nullptr);
        CHECK_EQ(bound->interfaceName, std::string("adventure"));
        CHECK(bound->category == declaration.category);
    }
    CHECK(host.natives().findEnum("adventure.Direction") != nullptr);
    CHECK(!context.bind(host.natives()).has_value()); // The names are taken.
    // Without the interface, adventure names are unknown to the compiler.
    CHECK(script::test::compileSource("func main():\n\tvar hero = get_actor(\"hero\")\n").has("Undefined function 'get_actor'"));
    CHECK(script::test::compileSource("func f(d: Direction):\n\tpass\n").has("Unknown type 'Direction'"));
}

SCHUIMM_TEST(Adventure_DirectionMapsExplicitlyToFacing)
{
    const std::array<std::pair<std::uint32_t, Facing>, 8> expected = {{{0, Facing::North},
                                                                        {1, Facing::NorthEast},
                                                                        {2, Facing::East},
                                                                        {3, Facing::SouthEast},
                                                                        {4, Facing::South},
                                                                        {5, Facing::SouthWest},
                                                                        {6, Facing::West},
                                                                        {7, Facing::NorthWest}}};
    const auto& members = adventureInterface().findEnum("Direction")->members;
    const std::array<std::string_view, 8> names = {"NORTH", "NORTH_EAST", "EAST", "SOUTH_EAST",
                                                   "SOUTH", "SOUTH_WEST", "WEST", "NORTH_WEST"};
    for (const auto& [ordinal, facing] : expected) {
        CHECK_EQ(members[ordinal], std::string(names[ordinal]));
        CHECK(facingFromDirection(ordinal) == facing);
        CHECK_EQ(directionFromFacing(facing), ordinal);
    }
}

// --- References and state ------------------------------------------------------------------------

SCHUIMM_TEST(Adventure_LookupAndReferences)
{
    Fixture f;
    f.start("func show(e: entity):\n\tprint(is_valid(e))\n"
            "func main():\n"
            "\tvar hero = get_actor(\"hero\")\n"
            "\tvar door = get_object(\"hero\")\n" // An object may share an actor's id: another entity.
            "\tprint(is_valid(hero))\n\tprint(is_valid(door))\n"
            "\tshow(hero)\n"
            "\tvar nobody = get_actor(\"nobody\")\n\tprint(is_valid(nobody))\n"
            "\tprint(is_valid(get_object(\"missing\")))\n"
            "\tvar a: entity = hero\n\tvar b: entity = door\n\tprint(a == b)\n\tprint(a == get_actor(\"hero\"))\n");
    f.frame();
    CHECK_EQ(f.joined(), std::string("True\nTrue\nTrue\nFalse\nFalse\nFalse\nTrue\n"));
    CHECK(f.errors.empty());
}

SCHUIMM_TEST(Adventure_NullAndExpiredReferencesAreErrors)
{
    Fixture f;
    f.start("func main():\n\tvar nobody = get_actor(\"nobody\")\n\tprint(get_position(nobody))\n");
    f.frame();
    REQUIRE(f.errors.size() == 1);
    CHECK(contains(f.firstError(), "get_position(): the actor reference is null"));
    CHECK(contains(f.firstError(), "plaza.sch:3"));
    // A reference into a room that is no longer active has expired.
    f.errors.clear();
    f.start("func main():\n\tvar hero = get_actor(\"hero\")\n\twait(0.15)\n\tset_visible(hero, False)\n");
    f.frame();
    auto other = f.makeRoom();
    f.adventure.setActiveRoom(other.get());
    f.room.swap(other);
    f.frames(3);
    REQUIRE(f.errors.size() == 1);
    CHECK(contains(f.firstError(), "set_visible(): actor reference #"));
    CHECK(contains(f.firstError(), "has expired: it is not in the active room 'plaza'"));
    // Without an active room lookups give null and is_valid is False.
    f.adventure.setActiveRoom(nullptr);
    f.errors.clear();
    f.output.clear();
    f.start("func main():\n\tprint(is_valid(get_actor(\"hero\")))\n\tset_visible(get_object(\"fan\"), True)\n");
    f.host.update(0.1);
    CHECK_EQ(f.joined(), std::string("False\n"));
    REQUIRE(f.errors.size() == 1);
    CHECK(contains(f.firstError(), "the object reference is null"));
}

SCHUIMM_TEST(Adventure_StateNatives)
{
    Fixture f;
    f.start("func main():\n"
            "\tvar hero = get_actor(\"hero\")\n"
            "\tvar fan = get_object(\"fan\")\n"
            "\tprint(get_position(hero))\n\tprint(get_position(fan))\n"
            "\tset_position(hero, vec2(120, 130.5))\n\tset_position(fan, get_position(fan) + vec2(10, 0))\n"
            "\tprint(get_position(hero))\n\tprint(get_position(fan))\n"
            "\tset_visible(fan, False)\n\tprint(is_visible(fan))\n\tprint(is_visible(hero))\n"
            "\tprint(get_direction(hero))\n\tset_direction(hero, Direction.NORTH_WEST)\n\tprint(get_direction(hero))\n"
            "\tprint(get_scale(hero))\n\tset_scale(hero, 2)\n\tprint(get_scale(hero))\n\tprint(get_effective_scale(hero))\n"
            "\tprint(get_walk_speed(hero))\n\tset_walk_speed(hero, 40)\n\tprint(get_walk_speed(hero))\n"
            "\tprint(get_animation(hero))\n\tprint(get_animation(fan))\n");
    f.frame();
    CHECK(f.errors.empty());
    CHECK_EQ(f.joined(), std::string("vec2(100.0, 100.0)\nvec2(300.0, 50.0)\nvec2(120.0, 130.5)\nvec2(310.0, 50.0)\nFalse\nTrue\n"
                                     "Direction.SOUTH\nDirection.NORTH_WEST\n1.0\n2.0\n2.0\n100.0\n40.0\nidle\nstill\n"));
    CHECK(f.actor("hero").facing() == Facing::NorthWest);
    CHECK(!f.room->object("fan")->visible());
    CHECK(f.room->object("fan")->player().status() == animation::PlaybackStatus::Playing); // Hiding stops nothing.
}

SCHUIMM_TEST(Adventure_InvalidRequestsAreErrorsAndChangeNothing)
{
    Fixture f;
    const auto expectError = [&f](const std::string& statement, std::string_view message) {
        f.errors.clear();
        f.start("func main():\n\tvar hero = get_actor(\"hero\")\n\tvar plant = get_actor(\"plant\")\n"
                "\tvar lamp = get_object(\"lamp\")\n\t" +
                statement + "\n\tprint(\"not reached\")\n");
        f.frame();
        REQUIRE(f.errors.size() == 1);
        CHECK(contains(f.firstError(), message));
    };
    expectError("set_scale(hero, 0)", "set_scale(): ");
    CHECK_EQ(f.actor("hero").baseScale(), 1.0f);
    expectError("set_walk_speed(plant, 10)", "set_walk_speed(): ");
    expectError("set_walk_speed(hero, -1)", "set_walk_speed(): ");
    CHECK_EQ(f.actor("hero").walkSpeed(), 100.0f);
    expectError("set_position(hero, vec2(1e300, 0))", "set_position(): the position is not finite");
    expectError("set_position(hero, vec2(0, 1e300 * 1e300))", "set_position(): the position is not finite");
    expectError("set_position(hero, vec2(5e6, 0))", "set_position(): actor position");
    CHECK(f.actor("hero").position() == (Vec2{100, 100}));
    expectError("play_animation(hero, \"dance\")", "play_animation(): ");
    expectError("play_animation(lamp, \"spin\")", "object 'lamp' has no animations");
    expectError("pause_animation(lamp)", "object 'lamp' has no animations");
    expectError("print(get_animation(lamp))", "object 'lamp' has no animations");
    expectError("set_direction(plant, Direction.EAST)\n\tplay_animation(hero, \"look_up\")", "play_animation(): ");
    CHECK_EQ(f.actor("hero").animation(), std::string("idle"));
    expectError("walk_to(plant, vec2(10, 10))", "walk_to(): ");
    expectError("walk_to(hero, vec2(0, 1e300 * 1e300))", "walk_to(): the destination is not a finite position");
    CHECK(!f.actor("hero").isWalking());
    CHECK(f.output.empty());
}

// --- walk_to ------------------------------------------------------------------------------------------

SCHUIMM_TEST(Adventure_WalkToSuspendsUntilArrival)
{
    Fixture f;
    f.start("func main():\n\tvar hero = get_actor(\"hero\")\n\tlog_walk(hero)\n"
            "func log_walk(hero: actor):\n"
            "\tprint(\"start\")\n\twalk_to(hero, vec2(150, 100))\n\tprint(\"arrived \" + str(is_walking(hero)))\n"
            "\tprint(get_position(hero))\n");
    f.frame(); // The script starts the walk (it waits; the room has not moved the actor yet).
    CHECK_EQ(f.joined(), std::string("start\n"));
    CHECK(f.actor("hero").isWalking());
    CHECK(f.actor("hero").position() == (Vec2{100, 100}));
    f.frames(4); // 0.4 s at 100 px/s: 40 px.
    CHECK_EQ(f.joined(), std::string("start\n"));
    f.frames(2);
    CHECK_EQ(f.joined(), std::string("start\narrived False\nvec2(150.0, 100.0)\n"));
    CHECK(f.errors.empty());
    CHECK_EQ(f.adventure.pendingWalks(), std::size_t{0});
}

SCHUIMM_TEST(Adventure_WalkToUsesTheNearestReachablePoint)
{
    Fixture f;
    f.start("func main():\n\tvar hero = get_actor(\"hero\")\n"
            "\twalk_to(hero, vec2(550, 50))\n" // On the disconnected balcony.
            "\tprint(get_position(hero))\n"
            "\twalk_to(hero, get_position(hero))\n" // Already there: no suspension.
            "\tprint(\"no wait\")\n");
    f.frames(60);
    CHECK(f.errors.empty());
    CHECK_EQ(f.joined(), std::string("vec2(400.0, 50.0)\nno wait\n"));
    // The resolved destination differs from the requested one; that is still success.
    CHECK(f.actor("hero").movement().status == room::MovementStatus::Completed);
}

SCHUIMM_TEST(Adventure_StoppingAWalkCancelsItsWaiter)
{
    Fixture f;
    const auto program = f.load("func walker():\n\twalk_to(get_actor(\"hero\"), vec2(350, 100))\n\tprint(\"never\")\n"
                                "func stopper():\n\twait(0.25)\n\tstop_walking(get_actor(\"hero\"))\n\tprint(\"stopped\")\n");
    REQUIRE(f.host.start(program, "walker").has_value());
    REQUIRE(f.host.start(program, "stopper").has_value());
    f.frames(10);
    CHECK_EQ(f.joined(), std::string("stopped\n"));
    CHECK(f.errors.empty()); // Normal cancellation, not a failure.
    CHECK_EQ(f.cancelled, 1);
    CHECK(!f.actor("hero").isWalking());
    CHECK_EQ(f.adventure.pendingWalks(), std::size_t{0});
}

SCHUIMM_TEST(Adventure_ReplacedAndRepositionedWalksCancelTheirWaiters)
{
    Fixture f;
    const auto program = f.load("func first():\n\twalk_to(get_actor(\"hero\"), vec2(350, 100))\n\tprint(\"first never\")\n"
                                "func second():\n\twait(0.15)\n\twalk_to(get_actor(\"hero\"), vec2(100, 200))\n\tprint(\"second arrived\")\n"
                                "func guard():\n\twalk_to(get_actor(\"guard\"), vec2(300, 100))\n\tprint(\"guard never\")\n"
                                "func mover():\n\twait(0.15)\n\tset_position(get_actor(\"guard\"), vec2(10, 10))\n");
    for (const char* entry : {"first", "second", "guard", "mover"}) REQUIRE(f.host.start(program, entry).has_value());
    f.frames(40);
    CHECK_EQ(f.joined(), std::string("second arrived\n"));
    CHECK(f.errors.empty());
    CHECK_EQ(f.cancelled, 2); // first (replaced) and guard (moved away).
    CHECK(near(f.actor("hero").position(), {100, 200}));
    CHECK(f.actor("guard").position() == (Vec2{10, 10}));
}

SCHUIMM_TEST(Adventure_ConcurrentWalksWithSpawn)
{
    Fixture f;
    f.start("func main():\n"
            "\tvar guard = get_actor(\"guard\")\n\tvar hero = get_actor(\"hero\")\n"
            "\tspawn walk_to(guard, vec2(300, 100))\n"
            "\twalk_to(hero, vec2(100, 150))\n"
            "\tprint(\"hero arrived, guard walking: \" + str(is_walking(guard)))\n"
            "\twait(1.0)\n"
            "\tprint(get_position(guard))\n");
    f.frames(30);
    CHECK(f.errors.empty());
    CHECK_EQ(f.joined(), std::string("hero arrived, guard walking: True\nvec2(300.0, 100.0)\n"));
}

SCHUIMM_TEST(Adventure_CancellingAWalkingCoroutineStopsOnlyItsOwnWalk)
{
    Fixture f;
    const auto program = f.load("func walker():\n\twalk_to(get_actor(\"hero\"), vec2(350, 100))\n\tprint(\"never\")\n"
                                "func taker():\n\twalk_to(get_actor(\"hero\"), vec2(100, 250))\n\tprint(\"taker arrived\")\n");
    const auto walker = f.host.start(program, "walker").value();
    f.frames(2);
    CHECK(f.host.cancel(walker)); // Gives up: its walk stops where the actor is.
    CHECK(!f.actor("hero").isWalking());
    const Vec2 stoppedAt = f.actor("hero").position();
    f.frames(2);
    CHECK(f.actor("hero").position() == stoppedAt);
    // A newer walk of another coroutine is never stopped by the old waiter.
    const auto first = f.host.start(program, "walker").value();
    f.frame();
    REQUIRE(f.host.start(program, "taker").has_value());
    f.frame(); // taker replaces walker's request.
    f.frame(); // publish: walker cancelled ("replaced").
    CHECK(!f.host.state(first) || *f.host.state(first) == script::CoroutineState::Cancelled);
    CHECK(f.actor("hero").isWalking()); // taker's walk goes on.
    f.frames(40);
    CHECK_EQ(f.joined(), std::string("taker arrived\n"));
}

// --- Animation operations ---------------------------------------------------------------------------

SCHUIMM_TEST(Adventure_PlayAnimationReturnsAnOperation)
{
    Fixture f;
    f.start("func main():\n"
            "\tvar hero = get_actor(\"hero\")\n"
            "\tvar playback: operation = play_animation(hero, \"wave\")\n"
            "\tprint(\"returned at once\")\n"
            "\twait(playback)\n"
            "\tprint(\"waved\")\n"
            "\twait(playback)\n" // Completed: no suspension.
            "\tprint(\"still done\")\n"
            "\twait(play_animation(get_object(\"fan\"), \"burst\"))\n"
            "\tprint(\"burst\")\n");
    f.frame();
    CHECK_EQ(f.joined(), std::string("returned at once\n"));
    f.frame(0.1); // 6 of 12 ticks.
    CHECK_EQ(f.joined(), std::string("returned at once\n"));
    f.frame(0.15); // Finished in this room update; published; the script resumes in the same frame.
    CHECK_EQ(f.joined(), std::string("returned at once\nwaved\nstill done\n"));
    f.frames(3, 0.1);
    CHECK_EQ(f.joined(), std::string("returned at once\nwaved\nstill done\nburst\n"));
    CHECK(f.errors.empty());
    CHECK_EQ(f.adventure.pendingPlaybacks(), std::size_t{0});
}

SCHUIMM_TEST(Adventure_WaitingForALoopingAnimationIsAnError)
{
    Fixture f;
    f.start("func main():\n\tvar fan = get_object(\"fan\")\n\tvar spin = play_animation(fan, \"spin\")\n\twait(spin)\n\tprint(\"never\")\n");
    f.frames(2);
    REQUIRE(f.errors.size() == 1);
    CHECK(contains(f.firstError(), "animation 'spin' loops, so its playback never completes"));
    CHECK_EQ(f.room->object("fan")->animation(), std::string("spin")); // It keeps spinning.
    CHECK(f.room->object("fan")->player().status() == animation::PlaybackStatus::Playing);
}

SCHUIMM_TEST(Adventure_StoppingOrReplacingAPlaybackCancelsItsWaiters)
{
    Fixture f;
    const auto program = f.load("func waiter(op: operation, name: string):\n\twait(op)\n\tprint(name + \" completed\")\n"
                                "func main():\n"
                                "\tvar hero = get_actor(\"hero\")\n\tvar fan = get_object(\"fan\")\n"
                                "\tvar wave = play_animation(hero, \"wave\")\n"
                                "\tspawn waiter(wave, \"wave a\")\n\tspawn waiter(wave, \"wave b\")\n"
                                "\tvar burst = play_animation(fan, \"burst\")\n\tspawn waiter(burst, \"burst\")\n"
                                "\tyield()\n\tyield()\n"
                                "\tplay_animation(hero, \"idle\")\n" // Replaces the wave.
                                "\tstop_animation(fan)\n"            // Stops the burst.
                                "\twait(1.0)\n"
                                "\twait(wave)\n\tprint(\"never: the wave was replaced\")\n");
    REQUIRE(f.host.start(program, "main").has_value());
    f.frames(30, 0.05);
    CHECK(f.output.empty());
    CHECK(f.errors.empty());
    CHECK_EQ(f.cancelled, 4); // Both wave waiters, the burst waiter and main's late wait.
}

SCHUIMM_TEST(Adventure_PausingKeepsThePlayback)
{
    Fixture f;
    f.start("func main():\n"
            "\tvar fan = get_object(\"fan\")\n"
            "\tvar burst = play_animation(fan, \"burst\")\n"
            "\tpause_animation(fan)\n"
            "\tspawn watch(burst)\n"
            "\twait(1.0)\n"
            "\tvar again = play_animation(fan, \"burst\")\n" // Paused: the same playback continues.
            "\tresume_animation(fan)\n"
            "\twait(again)\n"
            "\tprint(\"main saw it\")\n"
            "func watch(op: operation):\n\twait(op)\n\tprint(\"watcher saw it\")\n");
    f.frames(12, 0.1);
    CHECK(f.output.empty()); // Paused for 1 s: neither completed nor cancelled.
    CHECK_EQ(f.adventure.pendingPlaybacks(), std::size_t{1});
    f.frames(4, 0.1);
    CHECK_EQ(f.joined(), std::string("main saw it\nwatcher saw it\n")); // Scheduler order: main started first.
    CHECK(f.errors.empty());
}

SCHUIMM_TEST(Adventure_CancellingAWaiterDoesNotStopTheAnimation)
{
    Fixture f;
    const auto program = f.load("func main():\n\twait(play_animation(get_actor(\"hero\"), \"wave\"))\n\tprint(\"never\")\n");
    const auto main = f.host.start(program, "main").value();
    f.frame();
    CHECK(f.host.cancel(main));
    CHECK(f.actor("hero").player().status() == animation::PlaybackStatus::Playing);
    CHECK_EQ(f.actor("hero").animation(), std::string("wave"));
    f.frames(3);
    CHECK(f.actor("hero").player().status() == animation::PlaybackStatus::Finished);
}

SCHUIMM_TEST(Adventure_WalkingReplacesAnAnimationPlayback)
{
    // Locomotion changes the clip: a wave started before a walk is replaced by the walk animation.
    Fixture f;
    f.start("func main():\n\tvar hero = get_actor(\"hero\")\n\tvar wave = play_animation(hero, \"wave\")\n"
            "\tspawn walk_to(hero, vec2(200, 100))\n\twait(wave)\n\tprint(\"never\")\n");
    f.frames(5);
    CHECK(f.output.empty());
    CHECK(f.errors.empty());
    CHECK_EQ(f.cancelled, 1);
    CHECK(f.actor("hero").isWalking());
}

SCHUIMM_TEST(Adventure_OutcomesSurviveLaterOperations)
{
    Fixture f;
    f.start("func main():\n\tvar fan = get_object(\"fan\")\n\tvar burst = play_animation(fan, \"burst\")\n"
            "\twait(0.5)\n" // The burst has finished meanwhile...
            "\tplay_animation(fan, \"spin\")\n" // ...and another playback started.
            "\twait(burst)\n\tprint(\"burst had completed\")\n");
    f.frames(8, 0.1);
    CHECK_EQ(f.joined(), std::string("burst had completed\n"));
    CHECK(f.errors.empty());
}

// --- Camera -------------------------------------------------------------------------------------------

SCHUIMM_TEST(Adventure_Camera)
{
    Fixture f;
    f.start("func main():\n\tset_camera_position(vec2(250, 120))\n\tprint(get_camera_position())\n"
            "\tset_camera_zoom(2)\n\tprint(get_camera_zoom())\n\tset_camera_zoom(0)\n");
    f.frame();
    CHECK_EQ(f.joined(), std::string("vec2(250.0, 120.0)\n2.0\n"));
    REQUIRE(f.errors.size() == 1);
    CHECK(contains(f.firstError(), "set_camera_zoom(): the zoom must be a positive number, got 0.0"));
    CHECK(f.camera.position() == (Vec2{250, 120}));
    f.adventure.setCamera(nullptr);
    f.errors.clear();
    f.start("func main():\n\tprint(get_camera_zoom())\n");
    f.frame();
    REQUIRE(f.errors.size() == 1);
    CHECK(contains(f.firstError(), "there is no camera"));
}

// --- Rooms and lifetime -------------------------------------------------------------------------------

SCHUIMM_TEST(Adventure_LeavingTheRoomCancelsItsOperations)
{
    const std::int64_t tokens = script::CompletionToken::liveCount();
    {
        Fixture f;
        const auto program = f.load("func walker():\n\twalk_to(get_actor(\"hero\"), vec2(350, 100))\n\tprint(\"never\")\n"
                                    "func waver():\n\twait(play_animation(get_actor(\"guard\"), \"wave\"))\n\tprint(\"never\")\n");
        REQUIRE(f.host.start(program, "walker").has_value());
        REQUIRE(f.host.start(program, "waver").has_value());
        f.frame();
        CHECK_EQ(f.adventure.pendingWalks(), std::size_t{1});
        CHECK_EQ(f.adventure.pendingPlaybacks(), std::size_t{1});
        f.adventure.setActiveRoom(nullptr); // Before destroying the room.
        CHECK_EQ(f.adventure.pendingWalks(), std::size_t{0});
        CHECK_EQ(f.adventure.pendingPlaybacks(), std::size_t{0});
        CHECK_EQ(f.cancelled, 2);
        f.room.reset();
        f.frames(3);
        CHECK(f.output.empty());
        CHECK(f.errors.empty());
    }
    CHECK_EQ(script::CompletionToken::liveCount(), tokens); // Every record was reclaimed.
}

SCHUIMM_TEST(Adventure_UnobservedPlaybacksAreForgotten)
{
    Fixture f;
    f.start("func main():\n\tfor i in range(50):\n\t\tplay_animation(get_object(\"fan\"), \"spin\")\n"
            "\t\tplay_animation(get_actor(\"hero\"), \"wave\")\n\t\tyield()\n");
    f.frames(60);
    CHECK(f.errors.empty());
    CHECK_EQ(f.adventure.pendingPlaybacks(), std::size_t{0}); // No history of discarded operations.
}

// --- The ActorDemo courtyard script ------------------------------------------------------------------

namespace {

void copyTree(const std::string& from, const std::string& to)
{
    REQUIRE(fs::createDirectories(to).has_value());
    const auto entries = fs::listDirectory(from);
    REQUIRE(entries.has_value());
    for (const fs::DirectoryEntry& entry : *entries) {
        const std::string source = path::join(from, entry.name);
        const std::string target = path::join(to, entry.name);
        if (entry.type == fs::FileType::Directory) {
            copyTree(source, target);
        } else if (entry.type == fs::FileType::Regular) {
            const auto bytes = fs::readFile(source);
            REQUIRE(bytes.has_value());
            REQUIRE(fs::writeFile(target, *bytes).has_value());
        }
    }
}

} // namespace

SCHUIMM_TEST(Adventure_TheDemoCourtyardScriptRunsToTheEnd)
{
    // The demo's real assets and scripts, headless: the game is compiled the way the build
    // compiles it (the game build, with the adventure interface) and run by an AdventureRuntime,
    // frame by frame in the documented order.
    const std::string root = test::makeScratchDirectory("adventure_demo");
    copyTree(SCHUIMM_ACTOR_DEMO_ASSET_DIR, root);
    const AssetLocator locator = AssetLocator::create(root).value();
    const AssetPath gamePath = AssetPath::parse("ActorDemo.game.json").value();
    auto build = compileGame(locator, gamePath);
    REQUIRE(build.has_value());
    if (!(*build)->result.success) std::fprintf(stderr, "%s", script::formatDiagnostics((*build)->result).c_str());
    REQUIRE((*build)->result.success);
    auto bytes = script::writeBytecode((*build)->result.module);
    REQUIRE(bytes.has_value());
    REQUIRE(fs::createDirectories(path::join(root, "Scripts")).has_value());
    REQUIRE(fs::writeFile(path::join(root, "Scripts/ActorDemo.schb"), *bytes).has_value());

    Logger logger{LogLevel::Off};
    script::ScriptHost host{logger};
    std::vector<std::string> output;
    std::vector<script::ScriptError> errors;
    host.setOutputCallback([&](std::string_view, std::string_view text) { output.emplace_back(text); });
    host.setEventCallback([&](const script::CoroutineEvent& event) {
        if (event.kind == script::CoroutineEvent::Kind::Failed && event.error != nullptr) errors.push_back(*event.error);
    });
    auto natives = AdventureNatives::bind(host.natives());
    REQUIRE(natives.has_value());
    Camera2D camera{Extent2D{640, 360}};
    {
        auto runtime = AdventureRuntime::create(host, *natives, locator, gamePath, &camera);
        if (!runtime) std::fprintf(stderr, "%s\n", runtime.error().message.c_str());
        REQUIRE(runtime.has_value());
        const auto status = (*runtime)->enterRoom(AssetPath::parse("Rooms/Courtyard.room.json").value());
        if (!status) std::fprintf(stderr, "%s\n", status.error().message.c_str());
        REQUIRE(status.has_value());
        room::RoomInstance* courtyard = (*runtime)->activeRoom();
        REQUIRE(courtyard != nullptr);
        REQUIRE((*runtime)->dispatchRoom("ready")->handled);
        REQUIRE((*runtime)->dispatchRoom("demonstrate")->handled);
        // The ScriptHost forgets ended coroutines after each update: the last line tells when it is done.
        const auto done = [&output] { return !output.empty() && output.back() == "demonstrate: done"; };
        const double dt = 1.0 / 60.0;
        for (int frame = 0; frame < 6000 && !done() && errors.empty(); ++frame) {
            (*runtime)->update(dt);
            host.update(dt);
        }
        for (const script::ScriptError& error : errors) std::fprintf(stderr, "%s\n", script::describe(error).c_str());
        CHECK(errors.empty());
        REQUIRE(done());
        REQUIRE(output.size() >= 7);
        CHECK_EQ(output.front(), std::string("courtyard: the gardener starts its rounds"));
        CHECK_EQ(output[1], std::string("demonstrate: start"));
        CHECK(contains(output[2], "demonstrate: nearest reachable point 800, 240")); // The balcony is out of reach.
        CHECK_EQ(output[3], std::string("demonstrate: the fan spins (spinning)"));
        CHECK(contains(output[5], "the gardener came over, facing True"));
        CHECK_EQ(output.back(), std::string("demonstrate: done"));
        CHECK_EQ(courtyard->object("fan")->animation(), std::string("spinning"));
        // The fan's handler toggles it.
        output.clear();
        REQUIRE((*runtime)->dispatchObject("fan", "use")->handled);
        host.update(dt);
        CHECK_EQ(output, std::vector<std::string>{"fan: off"});
        CHECK_EQ(courtyard->object("fan")->animation(), std::string("idle"));
        // The gardener is back on its rounds; leaving the room ends them.
        CHECK_EQ(courtyard->activeCoroutineCount(), std::size_t{1});
        // The courtyard's movers have no script; the hall's walkers run their actor script, each
        // as itself with its own variables.
        CHECK(!(*runtime)->dispatchActor("gardener", "look_at")->handled);
        REQUIRE((*runtime)->enterRoom(AssetPath::parse("Rooms/Hall.room.json").value()).has_value());
        output.clear();
        REQUIRE((*runtime)->dispatchActor("guard_01", "look_at")->handled);
        REQUIRE((*runtime)->dispatchActor("guard_01", "look_at")->handled);
        REQUIRE((*runtime)->dispatchActor("guard_02", "look_at")->handled);
        host.update(dt);
        CHECK_EQ(output, std::vector<std::string>({"Looking good, as always. (1)", "Looking good, as always. (2)",
                                                   "Looking good, as always. (1)"}));
        host.stopAll();
    }
}

// --- Outcomes are recorded when they happen (review fixes) ----------------------------------------

SCHUIMM_TEST(Adventure_ACompletedWalkStaysCompletedWhenAnotherStartsBeforePublish)
{
    Fixture f;
    f.start("func main():\n\twalk_to(get_actor(\"hero\"), vec2(150, 100))\n\tprint(\"A arrived\")\n");
    f.frame(); // Walk A starts; its coroutine waits.
    room::ActorInstance& hero = f.actor("hero");
    // Room updates without publishing, until A arrives.
    for (int i = 0; i < 20 && hero.isWalking(); ++i) f.room->update(0.1);
    REQUIRE(hero.movementStatus() == room::MovementStatus::Completed);
    // C++ starts walk B before the next publish.
    REQUIRE(f.room->walkTo(hero, {300, 100}).has_value());
    REQUIRE(hero.isWalking());
    f.adventure.publish();
    f.host.update(0.1);
    CHECK_EQ(f.joined(), std::string("A arrived\n")); // Not cancelled as "replaced".
    CHECK_EQ(f.cancelled, 0);
    CHECK(hero.isWalking()); // B goes on.
}

SCHUIMM_TEST(Adventure_AFinishedPlaybackStaysFinishedWhenAnotherStartsBeforePublish)
{
    Fixture f;
    f.start("func main():\n\twait(play_animation(get_actor(\"hero\"), \"wave\"))\n\tprint(\"waved\")\n");
    f.frame();
    room::ActorInstance& hero = f.actor("hero");
    for (int i = 0; i < 20 && hero.playbackStatus() != animation::PlaybackStatus::Finished; ++i) f.room->update(0.05);
    REQUIRE(hero.playbackStatus() == animation::PlaybackStatus::Finished);
    REQUIRE(hero.playAnimation("idle").has_value()); // Another playback, before publish.
    f.adventure.publish();
    f.host.update(0.1);
    CHECK_EQ(f.joined(), std::string("waved\n"));
    CHECK_EQ(f.cancelled, 0);
}

SCHUIMM_TEST(Adventure_CancellationIsRecordedAtOnceAndDeliveredAtPublish)
{
    Fixture f;
    // Recorded at once: the same coroutine's wait sees the stopped playback without suspending.
    f.start("func main():\n\tvar fan = get_object(\"fan\")\n\tvar burst = play_animation(fan, \"burst\")\n"
            "\tstop_animation(fan)\n\twait(burst)\n\tprint(\"never\")\n");
    f.frame();
    CHECK_EQ(f.cancelled, 1); // In this frame's script update, not a frame later.
    CHECK(f.output.empty());
    // A waiter that is already suspended hears of it at the next publish, not inside the native.
    f.cancelled = 0;
    const auto program = f.load("func walker():\n\twalk_to(get_actor(\"hero\"), vec2(350, 100))\n\tprint(\"never\")\n"
                                "func stopper():\n\tyield()\n\tstop_walking(get_actor(\"hero\"))\n\tprint(\"stopped\")\n");
    REQUIRE(f.host.start(program, "walker").has_value());
    REQUIRE(f.host.start(program, "stopper").has_value());
    f.frame(); // walker waits; stopper yields.
    f.frame(); // stopper stops the walk: recorded, not delivered yet.
    CHECK_EQ(f.joined(), std::string("stopped\n"));
    CHECK_EQ(f.cancelled, 0);
    CHECK_EQ(f.adventure.undelivered(), std::size_t{1});
    CHECK_EQ(f.adventure.pendingWalks(), std::size_t{0});
    f.frame(); // publish delivers; the waiter is cancelled.
    CHECK_EQ(f.cancelled, 1);
    CHECK_EQ(f.adventure.undelivered(), std::size_t{0});
    CHECK(f.errors.empty());
}

SCHUIMM_TEST(Adventure_DestroyingTheActiveRoomDetachesItSafely)
{
    Fixture f;
    const auto program = f.load("func walker():\n\twalk_to(get_actor(\"hero\"), vec2(350, 100))\n\tprint(\"never\")\n"
                                "func waver():\n\twait(play_animation(get_object(\"fan\"), \"burst\"))\n\tprint(\"never\")\n"
                                "func probe():\n\tprint(is_valid(get_actor(\"hero\")))\n\tset_visible(get_actor(\"hero\"), False)\n");
    REQUIRE(f.host.start(program, "walker").has_value());
    REQUIRE(f.host.start(program, "waver").has_value());
    f.frame();
    REQUIRE(f.adventure.pendingWalks() == 1);
    f.room.reset(); // Without setActiveRoom(nullptr) first.
    CHECK(f.adventure.activeRoom() == nullptr);
    CHECK_EQ(f.cancelled, 2); // Both waiters, "destroyed", at once.
    CHECK_EQ(f.adventure.pendingWalks(), std::size_t{0});
    CHECK_EQ(f.adventure.pendingPlaybacks(), std::size_t{0});
    REQUIRE(f.host.start(program, "probe").has_value());
    f.frames(3); // publish, natives and abandon paths with no room: nothing touches freed state.
    CHECK_EQ(f.joined(), std::string("False\n"));
    REQUIRE(f.errors.size() == 1);
    CHECK(contains(f.firstError(), "the actor reference is null"));
}

SCHUIMM_TEST(Adventure_DestroyingTheContextDetachesItFromTheRoom)
{
    Fixture f;
    {
        Logger logger{LogLevel::Off};
        script::ScriptHost host{logger};
        auto context = std::make_unique<AdventureContext>();
        REQUIRE(context->bind(host.natives()).has_value());
        context->setActiveRoom(f.room.get()); // Takes the room's listener over from f.adventure.
        CHECK(f.room->eventListener() != nullptr);
        host.stopAll();
        context.reset();
        CHECK(f.room->eventListener() == nullptr);
    }
    // The room keeps working without a listener.
    room::ActorInstance& hero = f.actor("hero");
    REQUIRE(f.room->walkTo(hero, {300, 100}).has_value());
    REQUIRE(hero.playAnimation("wave").has_value());
    hero.stopWalking();
    f.room->update(1.0);
    f.adventure.setActiveRoom(nullptr);
}

// --- Coroutine reclamation ------------------------------------------------------------------------------

SCHUIMM_TEST(Adventure_SpawnedCoroutinesAreReclaimed)
{
    Fixture f;
    f.start("func blink():\n\tpass\n"
            "func main():\n\tfor i in range(20000):\n\t\tspawn blink()\n\t\tif i % 100 == 99:\n\t\t\tyield()\n"
            "\tprint(\"spawned 20000\")\n");
    std::size_t largest = 0;
    for (int frame = 0; frame < 400 && f.output.empty(); ++frame) {
        f.frame();
        largest = std::max(largest, f.host.scheduler().slotCapacity());
    }
    f.frame();
    CHECK_EQ(f.joined(), std::string("spawned 20000\n"));
    CHECK(f.errors.empty());
    // At most the coroutines alive at once (main and one batch of 100), never 20000.
    CHECK(largest <= std::size_t{256});
    CHECK_EQ(f.host.scheduler().size(), std::size_t{0}); // Every ended record was released.
}
