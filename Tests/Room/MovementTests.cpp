// Actor movement: walking along navigation paths, timing, replacement and cancellation, facing
// and locomotion animation, and room perspective (scale maps, base vs perspective scale,
// perspective-adjusted speed). Headless: images are PNG headers only.

#include "TestAssets.h"
#include "TestFramework.h"
#include "TestPng.h"

#include "Core/Common/GeometryPredicates.h"
#include "Core/Common/StringBuilder.h"
#include "Core/Logging/Logger.h"
#include "Platform/FileSystem/FileSystem.h"
#include "Platform/FileSystem/Path.h"
#include "Room/Perspective.h"
#include "Room/RoomInstance.h"
#include "Room/RoomLibrary.h"
#include "Script/Runtime/ScriptHost.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace schuimm;
using namespace schuimm::room;
using navigation::DestinationPolicy;

namespace {

std::span<const std::byte> bytesOf(std::string_view text)
{
    return std::as_bytes(std::span(text.data(), text.size()));
}

bool near(Vec2 a, Vec2 b, double tolerance = 1e-3)
{
    const bool ok = distance(a, b) <= tolerance;
    if (!ok) std::fprintf(stderr, "    (%g, %g) is not near (%g, %g)\n", static_cast<double>(a.x), static_cast<double>(a.y),
                          static_cast<double>(b.x), static_cast<double>(b.y));
    return ok;
}

constexpr std::string_view walkerAnimation = R"({ "version": 1, "clips": {
  "idle_south": { "loop": true, "frames": [ { "image": "Actors/walker.png", "source": [0, 0, 16, 32], "pivot": [8, 32], "duration": 30 } ] },
  "idle_north": { "loop": true, "frames": [ { "image": "Actors/walker.png", "source": [16, 0, 16, 32], "pivot": [8, 32], "duration": 30 } ] },
  "idle_east": { "loop": true, "frames": [ { "image": "Actors/walker.png", "source": [32, 0, 16, 32], "pivot": [8, 32], "duration": 30 } ] },
  "walk_south": { "loop": true, "frames": [
    { "image": "Actors/walker.png", "source": [0, 32, 16, 32], "pivot": [8, 32], "duration": 6 },
    { "image": "Actors/walker.png", "source": [16, 32, 16, 32], "pivot": [8, 32], "duration": 6 } ] },
  "walk_north": { "loop": true, "frames": [ { "image": "Actors/walker.png", "source": [32, 32, 16, 32], "pivot": [8, 32], "duration": 6 } ] },
  "walk_east": { "loop": true, "frames": [ { "image": "Actors/walker.png", "source": [48, 32, 16, 32], "pivot": [8, 32], "duration": 6 } ] },
  "walk_once": { "loop": false, "frames": [
    { "image": "Actors/walker.png", "source": [0, 32, 16, 32], "pivot": [8, 32], "duration": 6 },
    { "image": "Actors/walker.png", "source": [16, 32, 16, 32], "pivot": [8, 32], "duration": 6 } ] }
} })";

// Three authored directions, west mirrored, diagonals falling back to south.
constexpr std::string_view walkerActor = R"({ "version": 1, "id": "walker",
  "animation_resources": { "main": "Actors/walker.anim.json" },
  "animations": {
    "idle": { "south": { "resource": "main", "clip": "idle_south" }, "north": { "resource": "main", "clip": "idle_north" },
              "east": { "resource": "main", "clip": "idle_east" },
              "west": { "resource": "main", "clip": "idle_east", "flip_x": true }, "fallback": "south" },
    "walk": { "south": { "resource": "main", "clip": "walk_south" }, "north": { "resource": "main", "clip": "walk_north" },
              "east": { "resource": "main", "clip": "walk_east" },
              "west": { "resource": "main", "clip": "walk_east", "flip_x": true }, "fallback": "south" } },
  "default_animation": "idle", "default_facing": "south",
  "movement": { "speed": 100, "walk_animation": "walk", "idle_animation": "idle" } })";

// Walks only south: no clip for other directions and no fallback.
constexpr std::string_view stiffActor = R"({ "version": 1, "id": "stiff",
  "animation_resources": { "main": "Actors/walker.anim.json" },
  "animations": {
    "idle": { "any": { "resource": "main", "clip": "idle_south" } },
    "walk": { "south": { "resource": "main", "clip": "walk_south" } } },
  "default_animation": "idle", "default_facing": "south",
  "movement": { "speed": 100, "walk_animation": "walk", "idle_animation": "idle" } })";

// A fixed-size actor that walks at a constant speed.
constexpr std::string_view steadyActor = R"({ "version": 1, "id": "steady",
  "animation_resources": { "main": "Actors/walker.anim.json" },
  "animations": { "idle": { "any": { "resource": "main", "clip": "idle_south" } },
                  "walk": { "any": { "resource": "main", "clip": "walk_south" } } },
  "default_animation": "idle", "default_facing": "south", "perspective_scale": false,
  "movement": { "speed": 100, "walk_animation": "walk", "idle_animation": "idle", "perspective_speed": false } })";

// A non-looping walk clip.
constexpr std::string_view shufflerActor = R"({ "version": 1, "id": "shuffler",
  "animation_resources": { "main": "Actors/walker.anim.json" },
  "animations": { "idle": { "any": { "resource": "main", "clip": "idle_south" } },
                  "walk": { "any": { "resource": "main", "clip": "walk_once" } } },
  "default_animation": "idle", "default_facing": "south",
  "movement": { "speed": 100, "walk_animation": "walk", "idle_animation": "idle" } })";

// Walk and idle resolve to the same clip.
constexpr std::string_view glideActor = R"({ "version": 1, "id": "glide",
  "animation_resources": { "main": "Actors/walker.anim.json" },
  "animations": { "idle": { "any": { "resource": "main", "clip": "walk_south" } },
                  "walk": { "any": { "resource": "main", "clip": "walk_south" } } },
  "default_animation": "idle", "default_facing": "south",
  "movement": { "speed": 100, "walk_animation": "walk", "idle_animation": "idle" } })";

constexpr std::string_view decorativeActor = R"({ "version": 1, "id": "plant",
  "animation_resources": { "main": "Actors/walker.anim.json" },
  "animations": { "idle": { "any": { "resource": "main", "clip": "idle_south" } } },
  "default_animation": "idle", "default_facing": "south" })";

// 400 x 300, all walkable, no scale map.
constexpr std::string_view flatRoom = R"({ "version": 1, "id": "flat", "size": { "width": 400, "height": 300 },
  "navigation": { "walkable_areas": [ { "id": "floor", "points": [[0, 0], [400, 0], [400, 300], [0, 300]] } ] },
  "actors": [
    { "id": "hero", "actor": "Actors/walker.actor.json", "position": [100, 100] },
    { "id": "guard", "actor": "Actors/walker.actor.json", "position": [300, 250], "walk_speed": 50 },
    { "id": "stiff", "actor": "Actors/stiff.actor.json", "position": [200, 150] },
    { "id": "plant", "actor": "Actors/plant.actor.json", "position": [50, 50] },
    { "id": "shuffler", "actor": "Actors/shuffler.actor.json", "position": [50, 280] },
    { "id": "glide", "actor": "Actors/glide.actor.json", "position": [350, 20] } ] })";

// Far from the origin (float spacing 1/256 px there), with diagonal edges.
constexpr std::string_view farRoom = R"({ "version": 1, "id": "far", "size": { "width": 65536, "height": 65536 },
  "navigation": { "walkable_areas": [
    { "id": "a", "points": [[60000.3, 60000.7], [60800.1, 60100.9], [60700.6, 60900.2], [59900.4, 60800.8]] },
    { "id": "b", "points": [[60500.5, 60400.2], [61300.7, 60300.3], [61400.1, 61100.6], [60600.9, 61200.4]] } ] },
  "actors": [ { "id": "hero", "actor": "Actors/steady.actor.json", "position": [60400, 60450] } ] })";

// A U-shaped floor (the notch, x 160..240 and y 0..200, is not walkable), a separate balcony, and
// a scale map from 0.5 at y 100 to 1.5 at y 300.
constexpr std::string_view uRoom = R"({ "version": 1, "id": "yard", "size": { "width": 600, "height": 300 },
  "scale_map": { "type": "linear", "points": [ { "y": 100, "scale": 0.5 }, { "y": 300, "scale": 1.5 } ] },
  "navigation": { "walkable_areas": [
    { "id": "floor", "points": [[0, 0], [160, 0], [160, 200], [240, 200], [240, 0], [400, 0], [400, 300], [0, 300]] },
    { "id": "balcony", "points": [[450, 0], [600, 0], [600, 100], [450, 100]] } ] },
  "actors": [
    { "id": "hero", "actor": "Actors/walker.actor.json", "position": [80, 100] },
    { "id": "steady", "actor": "Actors/steady.actor.json", "position": [80, 150] } ] })";

constexpr std::string_view noNavigationRoom = R"({ "version": 1, "id": "void", "size": { "width": 100, "height": 100 },
  "actors": [ { "id": "hero", "actor": "Actors/walker.actor.json", "position": [50, 50] } ] })";

struct MovementFixture {
    std::string root = test::makeScratchDirectory("movement");
    Logger logger{LogLevel::Error};
    script::ScriptHost host{logger};
    std::unique_ptr<AssetLocator> locator;
    std::unique_ptr<RoomLibrary> library;

    MovementFixture()
    {
        write("Actors/walker.png", test::pngHeader(64, 64));
        write("Actors/walker.anim.json", bytesOf(walkerAnimation));
        write("Actors/walker.actor.json", bytesOf(walkerActor));
        write("Actors/stiff.actor.json", bytesOf(stiffActor));
        write("Actors/steady.actor.json", bytesOf(steadyActor));
        write("Actors/plant.actor.json", bytesOf(decorativeActor));
        write("Actors/shuffler.actor.json", bytesOf(shufflerActor));
        write("Actors/glide.actor.json", bytesOf(glideActor));
        write("Rooms/Far.room.json", bytesOf(farRoom));
        write("Rooms/Flat.room.json", bytesOf(flatRoom));
        write("Rooms/Yard.room.json", bytesOf(uRoom));
        write("Rooms/Void.room.json", bytesOf(noNavigationRoom));
        locator = std::make_unique<AssetLocator>(AssetLocator::create(root).value());
        library = std::make_unique<RoomLibrary>(*locator);
    }

    void write(std::string_view asset, std::span<const std::byte> bytes) const
    {
        const std::string file = path::join(root, asset);
        REQUIRE(fs::createDirectories(path::parent(file)).has_value());
        REQUIRE(fs::writeFile(file, bytes).has_value());
    }

    std::unique_ptr<RoomInstance> room(std::string_view name)
    {
        auto loaded = library->load(AssetPath::parse(concat("Rooms/", name, ".room.json")).value());
        if (!loaded) std::fprintf(stderr, "%s\n", loaded.error().message.c_str());
        REQUIRE(loaded.has_value());
        auto instance = RoomInstance::create(*loaded, host);
        REQUIRE(instance.has_value());
        return std::move(*instance);
    }
};

std::string clipName(const ActorInstance& actor)
{
    return actor.player().clip().clip().name;
}

// Updates in steps of `step` seconds for `total` seconds (the last step shorter if needed).
void run(RoomInstance& room, double total, double step)
{
    double done = 0.0;
    while (done < total - 1e-12) {
        const double dt = std::min(step, total - done);
        room.update(dt);
        done += dt;
    }
}

} // namespace

// --- Facing ---

SCHUIMM_TEST(Movement_FacingFromDirection)
{
    CHECK(facingFromDirection({1, 0}) == Facing::East);
    CHECK(facingFromDirection({1, -1}) == Facing::NorthEast); // y points down: up is north.
    CHECK(facingFromDirection({0, -1}) == Facing::North);
    CHECK(facingFromDirection({-1, -1}) == Facing::NorthWest);
    CHECK(facingFromDirection({-1, 0}) == Facing::West);
    CHECK(facingFromDirection({-1, 1}) == Facing::SouthWest);
    CHECK(facingFromDirection({0, 1}) == Facing::South);
    CHECK(facingFromDirection({1, 1}) == Facing::SouthEast);
    CHECK(!facingFromDirection({0, 0}).has_value());
    CHECK(!facingFromDirection({std::nanf(""), 1}).has_value());
    // Sector boundaries (22.5 degrees from a compass direction) go counter-clockwise on screen.
    const double t = std::tan(22.5 * 3.14159265358979323846 / 180.0);
    CHECK(facingFromDirection({1, static_cast<float>(-t * 0.999)}) == Facing::East);
    CHECK(facingFromDirection({1, static_cast<float>(-t * 1.001)}) == Facing::NorthEast);
    CHECK(facingFromDirection({1, static_cast<float>(t * 0.999)}) == Facing::East);
    CHECK(facingFromDirection({1, static_cast<float>(t * 1.001)}) == Facing::SouthEast);
    CHECK(facingFromDirection({-100, -1}) == Facing::West);
    CHECK(facingFromDirection({-100, 1}) == Facing::West);
}

// --- Walking ---

SCHUIMM_TEST(Movement_WalksASegmentAtItsSpeedAndArrivesExactly)
{
    MovementFixture fixture;
    const auto room = fixture.room("Flat");
    ActorInstance& hero = *room->actor("hero");
    const auto request = room->walkTo(hero, {300, 100});
    REQUIRE(request.has_value());
    CHECK(!request->completed);
    CHECK(request->id != 0);
    CHECK(hero.isWalking());
    CHECK(hero.facing() == Facing::East);
    CHECK(clipName(hero) == "walk_east");
    room->update(1.0);
    CHECK(near(hero.position(), {200, 100}));
    room->update(0.5);
    CHECK(near(hero.position(), {250, 100}));
    room->update(5.0); // Much longer than needed: no overshoot.
    CHECK(hero.position() == (Vec2{300, 100}));
    CHECK(hero.movementStatus() == MovementStatus::Completed);
    CHECK(hero.movement().request == request->id);
    CHECK(clipName(hero) == "idle_east"); // Idle, keeping the last facing.
    CHECK(hero.facing() == Facing::East);
    room->update(1.0);
    CHECK(hero.position() == (Vec2{300, 100}));
}

SCHUIMM_TEST(Movement_ZeroTimeAndZeroDistance)
{
    MovementFixture fixture;
    const auto room = fixture.room("Flat");
    ActorInstance& hero = *room->actor("hero");
    const std::string clipBefore = clipName(hero);
    const auto here = room->walkTo(hero, {100, 100});
    REQUIRE(here.has_value());
    CHECK(here->completed);
    CHECK(hero.movementStatus() == MovementStatus::Completed);
    CHECK(clipName(hero) == clipBefore); // No animation change for a walk that never started.
    REQUIRE(room->walkTo(hero, {200, 100}).has_value());
    room->update(0.0);
    CHECK(hero.position() == (Vec2{100, 100}));
    CHECK(hero.isWalking());
}

SCHUIMM_TEST(Movement_FollowsAPathAroundAnObstacle)
{
    MovementFixture fixture;
    const auto room = fixture.room("Yard");
    ActorInstance& steady = *room->actor("steady"); // Constant speed: easy to time.
    const auto request = room->walkTo(steady, {320, 150});
    REQUIRE(request.has_value());
    const auto& path = steady.movement().path;
    REQUIRE(path.size() == 4);
    CHECK(near(path[1], {160, 200}));
    CHECK(near(path[2], {240, 200}));
    double length = 0.0;
    for (std::size_t i = 1; i < path.size(); ++i) {
        CHECK(room->navigation()->isSegmentWalkable(path[i - 1], path[i]));
        length += distance(path[i - 1], path[i]);
    }
    // Past both corners in one update: the remaining time carries over.
    const double toSecondCorner = (distance({80, 150}, {160, 200}) + 80.0) / 100.0;
    room->update(toSecondCorner + 0.1);
    CHECK(near(steady.position(), lerp(path[2], path[3], 10.0 / distance(path[2], path[3])), 1e-2));
    room->update(length / 100.0); // More than enough.
    CHECK(steady.position() == (Vec2{320, 150}));
    CHECK(steady.movementStatus() == MovementStatus::Completed);
}

SCHUIMM_TEST(Movement_UpdateRatesGiveTheSamePositions)
{
    for (const char* actorId : {"steady", "hero"}) { // Constant speed, and perspective speed.
        Vec2 positions[3];
        const double steps[3] = {1.0 / 60.0, 1.0 / 144.0, 2.5};
        for (int i = 0; i < 3; ++i) {
            MovementFixture fixture;
            const auto room = fixture.room("Yard");
            ActorInstance& actor = *room->actor(actorId);
            REQUIRE(room->walkTo(actor, {320, 150}).has_value());
            run(*room, 2.5, steps[i]);
            positions[i] = actor.position();
            CHECK(actor.isWalking());
        }
        CHECK(near(positions[0], positions[1], 1e-2));
        CHECK(near(positions[0], positions[2], 1e-2));
    }
}

SCHUIMM_TEST(Movement_StoppingCancelsAndSelectsIdle)
{
    MovementFixture fixture;
    const auto room = fixture.room("Flat");
    ActorInstance& hero = *room->actor("hero");
    REQUIRE(room->walkTo(hero, {100, 250}).has_value()); // South.
    room->update(0.5);
    hero.stopWalking();
    CHECK(hero.movementStatus() == MovementStatus::Cancelled);
    CHECK(clipName(hero) == "idle_south");
    const Vec2 stopped = hero.position();
    CHECK(near(stopped, {100, 150}));
    room->update(1.0);
    CHECK(hero.position() == stopped);
    hero.stopWalking(); // Not walking: nothing happens.
    CHECK(hero.movementStatus() == MovementStatus::Cancelled);
}

SCHUIMM_TEST(Movement_ANewRequestReplacesTheWalkFromTheCurrentPosition)
{
    MovementFixture fixture;
    const auto room = fixture.room("Flat");
    ActorInstance& hero = *room->actor("hero");
    const auto first = room->walkTo(hero, {300, 100});
    REQUIRE(first.has_value());
    room->update(1.0); // At (200, 100).
    const auto second = room->walkTo(hero, {200, 250});
    REQUIRE(second.has_value());
    CHECK(second->id != first->id);
    CHECK(hero.movement().request == second->id);
    CHECK(near(hero.movement().path.front(), {200, 100})); // Not the original start.
    CHECK(hero.facing() == Facing::South);
    room->update(10.0);
    CHECK(hero.position() == (Vec2{200, 250}));
}

SCHUIMM_TEST(Movement_AFailedRequestKeepsTheCurrentWalk)
{
    MovementFixture fixture;
    const auto room = fixture.room("Yard");
    ActorInstance& hero = *room->actor("hero");
    const auto walking = room->walkTo(hero, {80, 280});
    REQUIRE(walking.has_value());
    room->update(0.2);
    const MovementState before = hero.movement();
    const Vec2 position = hero.position();
    const std::string clip = clipName(hero);
    // Strict: the balcony is another walkable area; the notch is not walkable.
    const auto balcony = room->walkTo(hero, {500, 50}, DestinationPolicy::Strict);
    REQUIRE(!balcony.has_value());
    CHECK(balcony.error().code == ErrorCode::NotFound);
    CHECK(!room->walkTo(hero, {200, 100}, DestinationPolicy::Strict).has_value());
    CHECK(!room->walkTo(hero, {std::nanf(""), 0}).has_value());
    CHECK(hero.isWalking());
    CHECK(hero.movement().request == before.request);
    CHECK(hero.movement().path == before.path);
    CHECK(hero.position() == position);
    CHECK(clipName(hero) == clip);
}

SCHUIMM_TEST(Movement_NearestReachableDestinations)
{
    MovementFixture fixture;
    const auto room = fixture.room("Yard");
    ActorInstance& hero = *room->actor("hero");
    // In the notch: to its nearest side. On the balcony: not there (unreachable), but the nearest
    // point of the floor.
    const auto notch = room->walkTo(hero, {165, 100});
    REQUIRE(notch.has_value());
    CHECK(notch->requested == (Vec2{165, 100}));
    CHECK(near(notch->resolved, {160, 100}));
    CHECK(hero.movement().requested == notch->requested);
    CHECK(hero.movement().resolved == notch->resolved);
    const auto balcony = room->walkTo(hero, {500, 50});
    REQUIRE(balcony.has_value());
    CHECK(near(balcony->resolved, {400, 50}));
    room->update(100.0);
    CHECK(near(hero.position(), {400, 50}));
}

SCHUIMM_TEST(Movement_RepositioningCancelsTheWalk)
{
    MovementFixture fixture;
    const auto room = fixture.room("Flat");
    ActorInstance& hero = *room->actor("hero");
    REQUIRE(room->walkTo(hero, {300, 100}).has_value());
    room->update(0.5);
    REQUIRE(hero.setPosition({50, 280}).has_value());
    CHECK(hero.movementStatus() == MovementStatus::Cancelled);
    CHECK(clipName(hero) == "idle_east");
    room->update(5.0); // Stale waypoints do not pull it back.
    CHECK(hero.position() == (Vec2{50, 280}));
    // Placed outside the walkable area: allowed, but it cannot walk from there.
    REQUIRE(hero.setPosition({-50, 50}).has_value());
    const auto outside = room->walkTo(hero, {100, 100});
    REQUIRE(!outside.has_value());
    CHECK(outside.error().code == ErrorCode::NotFound);
    CHECK(outside.error().message.find("outside the walkable area") != std::string::npos);
    CHECK(hero.position() == (Vec2{-50, 50})); // Not teleported.
}

SCHUIMM_TEST(Movement_ExplicitAnimationRequestsTakeOver)
{
    MovementFixture fixture;
    const auto room = fixture.room("Flat");
    ActorInstance& hero = *room->actor("hero");
    REQUIRE(room->walkTo(hero, {300, 100}).has_value());
    room->update(0.3);
    REQUIRE(hero.setAnimation("idle", Facing::North).has_value());
    CHECK(hero.movementStatus() == MovementStatus::Cancelled);
    CHECK(clipName(hero) == "idle_north"); // The explicit request, not the idle selection.
    const Vec2 stopped = hero.position();
    room->update(1.0);
    CHECK(hero.position() == stopped);
    // A failed explicit request does not stop a walk.
    REQUIRE(room->walkTo(hero, {300, 200}).has_value());
    CHECK(!hero.playAnimation("dance").has_value());
    CHECK(hero.isWalking());
}

SCHUIMM_TEST(Movement_FacingAndAnimationAlongThePath)
{
    MovementFixture fixture;
    const auto room = fixture.room("Yard");
    ActorInstance& hero = *room->actor("hero");
    REQUIRE(room->walkTo(hero, {320, 100}).has_value());
    // First segment down-right: south-east, which falls back to the south clip.
    CHECK(hero.facing() == Facing::SouthEast);
    CHECK(clipName(hero) == "walk_south");
    // Walk until on the second segment (east), then the third (north-east, falls back to south).
    int guard = 0;
    while (hero.movement().nextWaypoint < 2 && guard++ < 10000) room->update(1.0 / 60.0);
    CHECK(hero.facing() == Facing::East);
    CHECK(clipName(hero) == "walk_east");
    CHECK(!hero.flipX());
    // The walk clip keeps playing: it is not restarted every update.
    const std::uint64_t ticks = hero.player().elapsedTicks();
    room->update(1.0 / 60.0);
    CHECK(hero.player().elapsedTicks() == (ticks + 1) % 6);
    while (hero.movement().nextWaypoint < 3 && guard++ < 10000) room->update(1.0 / 60.0);
    CHECK(hero.facing() == Facing::NorthEast);
    CHECK(clipName(hero) == "walk_south");
    room->update(100.0);
    CHECK(hero.facing() == Facing::NorthEast);
    CHECK(clipName(hero) == "idle_south"); // Idle north-east falls back to south as well.
}

SCHUIMM_TEST(Movement_WestUsesTheMirroredEastClip)
{
    MovementFixture fixture;
    const auto room = fixture.room("Flat");
    ActorInstance& hero = *room->actor("hero");
    REQUIRE(room->walkTo(hero, {20, 100}).has_value());
    CHECK(hero.facing() == Facing::West);
    CHECK(clipName(hero) == "walk_east");
    CHECK(hero.flipX());
    CHECK(hero.renderState().flipX);
    room->update(10.0);
    CHECK(clipName(hero) == "idle_east");
    CHECK(hero.flipX());
}

SCHUIMM_TEST(Movement_MissingLocomotionAnimations)
{
    MovementFixture fixture;
    const auto room = fixture.room("Flat");
    ActorInstance& stiff = *room->actor("stiff");
    const Vec2 position = stiff.position();
    const std::string clip = clipName(stiff);
    const auto north = room->walkTo(stiff, {200, 20});
    REQUIRE(!north.has_value());
    CHECK(north.error().code == ErrorCode::NotFound);
    CHECK(north.error().message.find("actor 'stiff': actor 'stiff' animation 'walk' has no clip for facing 'north'") !=
          std::string::npos);
    CHECK(stiff.movementStatus() == MovementStatus::Idle);
    CHECK(stiff.position() == position);
    CHECK(clipName(stiff) == clip);
    CHECK(room->walkTo(stiff, {200, 290}).has_value()); // South works.

    ActorInstance& plant = *room->actor("plant");
    CHECK(!plant.canWalk());
    const auto decorative = room->walkTo(plant, {60, 60});
    REQUIRE(!decorative.has_value());
    CHECK(decorative.error().code == ErrorCode::InvalidState);
    CHECK(decorative.error().message.find("has no movement configuration") != std::string::npos);
    CHECK(!plant.setWalkSpeed(10.0f).has_value());
}

SCHUIMM_TEST(Movement_RoomsWithoutNavigationAndForeignActors)
{
    MovementFixture fixture;
    const auto empty = fixture.room("Void");
    const auto result = empty->walkTo(*empty->actor("hero"), {10, 10});
    REQUIRE(!result.has_value());
    CHECK(result.error().code == ErrorCode::InvalidState);
    CHECK(result.error().message.find("has no walkable areas") != std::string::npos);
    const auto flat = fixture.room("Flat");
    CHECK(flat->walkTo(*empty->actor("hero"), {10, 10}).error().code == ErrorCode::InvalidArgument);
}

SCHUIMM_TEST(Movement_ActorsMoveIndependently)
{
    MovementFixture fixture;
    const auto room = fixture.room("Flat");
    ActorInstance& hero = *room->actor("hero");
    ActorInstance& guard = *room->actor("guard"); // The same resource, walk_speed 50.
    CHECK(guard.walkSpeed() == 50.0f);
    CHECK(hero.walkSpeed() == 100.0f);
    REQUIRE(room->walkTo(hero, {300, 100}).has_value());
    REQUIRE(room->walkTo(guard, {300, 50}).has_value());
    room->update(1.0);
    CHECK(near(hero.position(), {200, 100}));
    CHECK(near(guard.position(), {300, 200}));
    hero.stopWalking();
    CHECK(guard.isWalking());
    REQUIRE(guard.setWalkSpeed(100.0f).has_value());
    room->update(1.0);
    CHECK(near(guard.position(), {300, 100}));
    CHECK(!guard.setWalkSpeed(0.0f).has_value());
    CHECK(guard.movement().request != hero.movement().request);
}

SCHUIMM_TEST(Movement_DestroyingARoomWhileActorsWalk)
{
    MovementFixture fixture;
    auto room = fixture.room("Yard");
    REQUIRE(room->walkTo(*room->actor("hero"), {320, 100}).has_value());
    room->update(0.5);
    room.reset();
    // A fresh instance starts from the placements, not walking.
    room = fixture.room("Yard");
    CHECK(room->actor("hero")->movementStatus() == MovementStatus::Idle);
    CHECK(room->actor("hero")->position() == (Vec2{80, 100}));
}

// --- Perspective ---

SCHUIMM_TEST(Perspective_EvaluatesScaleMaps)
{
    const ScaleMap map{ScaleMap::Type::Linear, {{100, 0.5f}, {200, 1.0f}, {300, 0.75f}}};
    CHECK(evaluateScaleMap(map, 100) == 0.5f);   // Exact control points.
    CHECK(evaluateScaleMap(map, 200) == 1.0f);
    CHECK(evaluateScaleMap(map, 150) == 0.75f);  // Between.
    CHECK(evaluateScaleMap(map, 250) == 0.875f); // Decreasing.
    CHECK(evaluateScaleMap(map, -1000) == 0.5f); // Clamped, not extrapolated.
    CHECK(evaluateScaleMap(map, 1000) == 0.75f);
    CHECK(perspectiveScaleAt(nullptr, 50) == 1.0f);
}

SCHUIMM_TEST(Perspective_BaseAndPerspectiveScalesAreSeparate)
{
    MovementFixture fixture;
    const auto room = fixture.room("Yard");
    ActorInstance& hero = *room->actor("hero");
    CHECK(hero.baseScale() == 1.0f);
    CHECK(hero.perspectiveScale() == 0.5f); // y 100: the first point.
    CHECK(hero.effectiveScale() == 0.5f);
    REQUIRE(hero.setBaseScale(0.8f).has_value());
    CHECK(hero.baseScale() == 0.8f); // Not overwritten by perspective.
    CHECK(std::fabs(hero.effectiveScale() - 0.4f) < 1e-6f);
    REQUIRE(hero.setPosition({80, 200}).has_value());
    CHECK(hero.perspectiveScale() == 1.0f); // Derived from the new position.
    CHECK(std::fabs(hero.effectiveScale() - 0.8f) < 1e-6f);
    // The render state uses the effective scale (frame 16 x 32).
    CHECK(hero.renderState().destination.width == 16.0f * hero.effectiveScale());
    // Opting out, per instance or per resource.
    hero.setPerspectiveScaling(false);
    CHECK(hero.perspectiveScale() == 1.0f);
    CHECK(hero.effectiveScale() == 0.8f);
    const ActorInstance& steady = *room->actor("steady");
    CHECK(!steady.perspectiveScaling());
    CHECK(steady.effectiveScale() == steady.baseScale());
    // Base scale limits keep the effective scale finite and non-zero.
    CHECK(!hero.setBaseScale(0.0001f).has_value());
    CHECK(!hero.setBaseScale(2000.0f).has_value());
}

SCHUIMM_TEST(Perspective_ScalesTheWalkingSpeed)
{
    MovementFixture fixture;
    const auto room = fixture.room("Yard");
    ActorInstance& hero = *room->actor("hero");
    // Straight down x = 80 from y 100 to 300, where s(y) = 0.5 + (y - 100) / 200. Time:
    //   integral dy / (v s(y)) = 200 / v * ln(s(300) / s(100)) = 2 * ln 3 seconds (v = 100).
    const double expected = 2.0 * std::log(3.0);
    REQUIRE(room->walkTo(hero, {80, 300}).has_value());
    run(*room, expected - 0.01, 1.0 / 60.0);
    CHECK(hero.isWalking());
    room->update(0.02);
    CHECK(hero.movementStatus() == MovementStatus::Completed);
    CHECK(hero.position() == (Vec2{80, 300}));

    // Halfway in time, the actor is where the exact solution puts it: s grows exponentially in
    // time, s(t) = 0.5 * 3^(t / T), so y(t) = 100 + 200 (s(t) - 0.5).
    REQUIRE(hero.setPosition({80, 100}).has_value());
    REQUIRE(room->walkTo(hero, {80, 300}).has_value());
    run(*room, expected / 2.0, 1.0 / 144.0);
    const double s = 0.5 * std::sqrt(3.0);
    CHECK(std::fabs(static_cast<double>(hero.position().y) - (100.0 + 200.0 * (s - 0.5))) < 1e-2);

    // Without perspective speed the same distance takes 2 seconds.
    ActorInstance& steady = *room->actor("steady");
    REQUIRE(steady.setPosition({80, 100}).has_value());
    REQUIRE(room->walkTo(steady, {80, 300}).has_value());
    run(*room, 1.99, 1.0 / 60.0);
    CHECK(steady.isWalking());
    room->update(0.02);
    CHECK(steady.movementStatus() == MovementStatus::Completed);
}

SCHUIMM_TEST(Perspective_CrossesScaleMapIntervalsConsistently)
{
    // From the clamped region above y 100 into the linear region and back up: several pieces
    // per segment, any update rate.
    Vec2 positions[2];
    const double steps[2] = {1.0 / 30.0, 0.7};
    for (int i = 0; i < 2; ++i) {
        MovementFixture fixture;
        const auto room = fixture.room("Yard");
        ActorInstance& hero = *room->actor("hero");
        REQUIRE(hero.setPosition({60, 20}).has_value());
        REQUIRE(room->walkTo(hero, {120, 280}).has_value());
        run(*room, 2.1, steps[i]);
        positions[i] = hero.position();
    }
    CHECK(near(positions[0], positions[1], 1e-2));
}

// --- Room drawing ---

namespace {

constexpr std::string_view sceneRoom = R"({ "version": 1, "id": "scene", "size": { "width": 400, "height": 300 },
  "background": { "image": "Rooms/back.png" },
  "scale_map": { "type": "linear", "points": [ { "y": 0, "scale": 0.5 }, { "y": 300, "scale": 2.0 } ] },
  "props": [ { "id": "pillar", "sprite": "Rooms/pillar.png", "position": [100, 150], "pivot": [10, 60] },
             { "id": "rug", "sprite": "Rooms/rug.png", "position": [200, 200] } ],
  "objects": [ { "id": "chest", "sprite": "Rooms/chest.png", "position": [300, 150], "pivot": [12, 20] },
               { "id": "hotspot", "position": [50, 50] } ],
  "navigation": { "walkable_areas": [ { "id": "floor", "points": [[0, 100], [400, 100], [400, 300], [0, 300]] } ] },
  "actors": [ { "id": "hero", "actor": "Actors/walker.actor.json", "position": [150, 150] },
              { "id": "far", "actor": "Actors/walker.actor.json", "position": [150, 120] },
              { "id": "near", "actor": "Actors/walker.actor.json", "position": [150, 250] } ] })";

std::unique_ptr<RoomInstance> sceneRoomInstance(MovementFixture& fixture)
{
    fixture.write("Rooms/back.png", test::pngHeader(400, 300));
    fixture.write("Rooms/pillar.png", test::pngHeader(20, 60));
    fixture.write("Rooms/rug.png", test::pngHeader(50, 10));
    fixture.write("Rooms/chest.png", test::pngHeader(24, 20));
    fixture.write("Rooms/Scene.room.json", bytesOf(sceneRoom));
    return fixture.room("Scene");
}

} // namespace

SCHUIMM_TEST(RoomDrawing_BackgroundPropsObjectsAndActorsInOneOrder)
{
    MovementFixture fixture;
    const auto room = sceneRoomInstance(fixture);
    const RoomDrawList list = room->drawList();
    REQUIRE(list.background.has_value());
    CHECK(list.background->destination == (RectF{0, 0, 400, 300}));
    // Depths: far 120, pillar 150 (prop), chest 150 (object), hero 150 (actor), rug 200, near 250.
    // At equal depth: props, then objects, then actors.
    REQUIRE(list.items.size() == 6);
    const RoomDrawKind expected[] = {RoomDrawKind::Actor, RoomDrawKind::Prop, RoomDrawKind::Object,
                                     RoomDrawKind::Actor, RoomDrawKind::Prop, RoomDrawKind::Actor};
    for (std::size_t i = 0; i < 6; ++i) CHECK(list.items[i].kind == expected[i]);
    CHECK_EQ(list.items[0].index, std::size_t{1}); // far
    CHECK_EQ(list.items[3].index, std::size_t{0}); // hero
    CHECK_EQ(list.items[5].index, std::size_t{2}); // near
    // Pivots: the prop and object stand on their positions; the rug's default pivot is its corner.
    CHECK(list.items[1].sprite.destination == (RectF{90, 90, 20, 60}));
    CHECK(list.items[2].sprite.destination == (RectF{288, 130, 24, 20}));
    CHECK(list.items[4].sprite.destination == (RectF{200, 200, 50, 10}));
    // Actors at their effective scale around their feet: s(150) = 1.25, s(250) = 1.75.
    const RectF hero = list.items[3].sprite.destination;
    CHECK(std::fabs(hero.width - 16.0f * 1.25f) < 1e-4f);
    CHECK(std::fabs(hero.x + 8.0f * 1.25f - 150.0f) < 1e-4f);
    CHECK(std::fabs(hero.y + hero.height - 150.0f) < 1e-4f);
    CHECK(std::fabs(list.items[5].sprite.destination.height - 32.0f * 1.75f) < 1e-4f);
}

SCHUIMM_TEST(RoomDrawing_RuntimeStateAndReadOnlyDrawing)
{
    MovementFixture fixture;
    const auto room = sceneRoomInstance(fixture);
    // Objects use their runtime position and visibility; actors their runtime visibility.
    RoomObject& chest = *room->object("chest");
    REQUIRE(chest.setPosition({300, 260}).has_value());
    RoomDrawList list = room->drawList();
    CHECK(list.items.back().kind == RoomDrawKind::Object); // Now in front of everything.
    chest.setVisible(false);
    room->actor("near")->setVisible(false);
    list = room->drawList();
    CHECK_EQ(list.items.size(), std::size_t{4});
    for (const RoomDrawItem& item : list.items) CHECK(!(item.kind == RoomDrawKind::Object));
    // Drawing changes nothing: no movement, no animation.
    ActorInstance& hero = *room->actor("hero");
    REQUIRE(room->walkTo(hero, {300, 150}).has_value());
    const Vec2 before = hero.position();
    const std::uint64_t ticks = hero.player().elapsedTicks();
    for (int i = 0; i < 5; ++i) (void)room->drawList();
    CHECK(hero.position() == before);
    CHECK(hero.player().elapsedTicks() == ticks);
}

// --- Review follow-ups: final facing, entering locomotion, cancel position, precision ---

SCHUIMM_TEST(Movement_FinalFacingDoesNotDependOnTheUpdateRate)
{
    // Around the notch: the last segment (east, after a south-east one) starts close to the end,
    // so a single large update passes the corner and arrives in the same advance.
    for (const double step : {1.0 / 60.0, 100.0}) {
        MovementFixture fixture;
        const auto room = fixture.room("Yard");
        ActorInstance& hero = *room->actor("hero");
        REQUIRE(room->walkTo(hero, {320, 230}).has_value());
        REQUIRE(hero.movement().path.size() >= 3);
        run(*room, 100.0, step);
        CHECK(hero.movementStatus() == MovementStatus::Completed);
        const Vec2 last = hero.movement().path.back() - hero.movement().path[hero.movement().path.size() - 2];
        CHECK(hero.facing() == facingFromDirection(last));
        CHECK(clipName(hero) == (hero.facing() == Facing::East ? "idle_east" : "idle_south"));
    }
}

SCHUIMM_TEST(Movement_StartingAWalkPlaysTheWalkClip)
{
    MovementFixture fixture;
    const auto room = fixture.room("Flat");
    ActorInstance& hero = *room->actor("hero");
    // The walk clip already selected but stopped: a new walk plays it.
    REQUIRE(hero.setAnimation("walk", Facing::East).has_value());
    hero.stopAnimation();
    REQUIRE(room->walkTo(hero, {300, 100}).has_value());
    CHECK(clipName(hero) == "walk_east");
    CHECK(hero.playbackStatus() == animation::PlaybackStatus::Playing);
    // Paused during the walk: a new walk resumes it (it continues, it does not restart).
    room->update(0.1);
    hero.pauseAnimation();
    const std::uint64_t ticks = hero.player().elapsedTicks();
    REQUIRE(room->walkTo(hero, {350, 100}).has_value());
    CHECK(hero.playbackStatus() == animation::PlaybackStatus::Playing);
    CHECK(hero.player().elapsedTicks() == ticks);
    // A turn during a walk keeps an unchanged clip as it is (here: west, the same clip mirrored).
    room->update(0.1);
    hero.pauseAnimation();
    REQUIRE(room->walkTo(hero, {20, 100}).has_value()); // A new walk: resumed again.
    CHECK(hero.flipX());
    CHECK(hero.playbackStatus() == animation::PlaybackStatus::Playing);
}

SCHUIMM_TEST(Movement_NonLoopingWalkClipsHoldTheirLastFrame)
{
    MovementFixture fixture;
    const auto room = fixture.room("Flat");
    ActorInstance& shuffler = *room->actor("shuffler");
    REQUIRE(room->walkTo(shuffler, {350, 280}).has_value());
    room->update(1.0); // 60 ticks: the 12-tick clip finished, the walk goes on.
    CHECK(shuffler.isWalking());
    CHECK(shuffler.playbackStatus() == animation::PlaybackStatus::Finished);
    CHECK_EQ(shuffler.player().frameIndex(), std::size_t{1});
    // A new walk starts the finished clip again.
    REQUIRE(room->walkTo(shuffler, {350, 200}).has_value());
    CHECK(shuffler.playbackStatus() == animation::PlaybackStatus::Playing);
    CHECK_EQ(shuffler.player().elapsedTicks(), std::uint64_t{0});
}

SCHUIMM_TEST(Movement_WalkAndIdleSharingAClipKeepPlaying)
{
    MovementFixture fixture;
    const auto room = fixture.room("Flat");
    ActorInstance& glide = *room->actor("glide");
    REQUIRE(room->walkTo(glide, {350, 60}).has_value());
    room->update(0.25);
    const std::uint64_t ticks = glide.player().elapsedTicks();
    room->update(1.1); // Arrives: idle is the same clip, which keeps playing (66 ticks: not a whole loop).
    CHECK(glide.movementStatus() == MovementStatus::Completed);
    CHECK(clipName(glide) == "walk_south");
    CHECK(glide.playbackStatus() == animation::PlaybackStatus::Playing);
    CHECK(glide.player().elapsedTicks() != ticks);
}

SCHUIMM_TEST(Movement_ControllerPositionAfterCancelling)
{
    MovementController controller;
    CHECK(controller.position() == (Vec2{0, 0}));
    controller.start(7, {100, 0}, {100, 0}, {{0, 0}, {100, 0}});
    (void)controller.advance(0.4, 100.0, nullptr);
    CHECK(controller.position() == (Vec2{40, 0}));
    controller.cancel();
    CHECK(controller.state().status == MovementStatus::Cancelled);
    CHECK(controller.position() == (Vec2{40, 0})); // Where it stopped, not the path start.
    CHECK(!controller.advance(1.0, 100.0, nullptr).arrived);
    CHECK(controller.position() == (Vec2{40, 0}));

    // The actor and its controller agree after stopWalking.
    MovementFixture fixture;
    const auto room = fixture.room("Flat");
    ActorInstance& hero = *room->actor("hero");
    REQUIRE(room->walkTo(hero, {300, 100}).has_value());
    room->update(0.5);
    hero.stopWalking();
    const MovementState& state = hero.movement();
    const Vec2 a = state.path[state.nextWaypoint - 1];
    const Vec2 b = state.path[state.nextWaypoint];
    CHECK(near(lerp(a, b, state.travelled / distance(a, b)), hero.position()));
}

SCHUIMM_TEST(Movement_WalkingOnDiagonalEdgesFarFromTheOrigin)
{
    MovementFixture fixture;
    const auto room = fixture.room("Far");
    ActorInstance& hero = *room->actor("hero");
    // Outside, beyond a diagonal edge: resolved onto it. From there (a float position on the
    // boundary) the actor can walk again.
    for (const Vec2 outside : {Vec2{60400, 59000}, Vec2{62000, 60700}, Vec2{59000, 60400}, Vec2{60700, 62000}}) {
        const auto there = room->walkTo(hero, outside);
        REQUIRE(there.has_value());
        room->update(1000.0);
        CHECK(hero.movementStatus() == MovementStatus::Completed);
        CHECK(hero.position() == there->resolved);
        CHECK(room->navigation()->contains(hero.position()));
        const auto again = room->walkTo(hero, {60600, 60500});
        if (!again) std::fprintf(stderr, "%s\n", again.error().message.c_str());
        REQUIRE(again.has_value());
        room->update(1000.0);
        CHECK(hero.position() == (Vec2{60600, 60500}));
    }
}

// --- Operation events (RoomEvents.h) ---------------------------------------------------------------

namespace {

struct RecordingListener final : RoomEventListener {
    std::vector<std::string> events;
    int destroyed = 0;

    void movementEnded(script::ObjectRef actor, MovementRequestId request, MovementOutcome outcome) override
    {
        events.push_back(concat("walk #", actor.id, ".", request, " ", toString(outcome)));
    }
    void playbackEnded(script::ObjectRef entity, bool isActor, std::uint64_t generation, PlaybackOutcome outcome) override
    {
        events.push_back(concat(isActor ? "actor" : "object", " #", entity.id, " g", generation, " ", toString(outcome)));
    }
    void roomDestroyed() override { ++destroyed; }

    [[nodiscard]] std::size_t count(std::string_view part) const
    {
        return static_cast<std::size_t>(
            std::count_if(events.begin(), events.end(), [&](const std::string& e) { return e.find(part) != std::string::npos; }));
    }
};

} // namespace

SCHUIMM_TEST(Movement_EveryEndedRequestIsReportedOnceWithItsOutcome)
{
    MovementFixture fixture;
    auto room = fixture.room("Flat");
    RecordingListener listener;
    room->setEventListener(&listener);
    ActorInstance& hero = *room->actor("hero");
    const auto walk = [&](Vec2 to) { return room->walkTo(hero, to).value().id; };
    const std::string id = concat("walk #", hero.handle().id, ".");

    const MovementRequestId arrive = walk({150, 100});
    run(*room, 1.0, 0.1);
    CHECK_EQ(listener.count(concat(id, arrive, " completed")), std::size_t{1});

    const MovementRequestId replaced = walk({300, 100});
    const MovementRequestId stopped = walk({300, 200}); // Replaces the walk in progress.
    CHECK_EQ(listener.count(concat(id, replaced, " replaced")), std::size_t{1});
    room->update(0.1);
    hero.stopWalking();
    hero.stopWalking(); // Not walking any more: nothing more to report.
    CHECK_EQ(listener.count(concat(id, stopped, " stopped")), std::size_t{1});

    const MovementRequestId moved = walk({300, 200});
    REQUIRE(hero.setPosition({200, 200}).has_value());
    CHECK_EQ(listener.count(concat(id, moved, " stopped")), std::size_t{1});

    const MovementRequestId animated = walk({100, 200});
    REQUIRE(hero.playAnimation("idle").has_value()); // An explicit animation stops the walk.
    CHECK_EQ(listener.count(concat(id, animated, " stopped")), std::size_t{1});

    // A request that needs no walking completes at once, and replaces a walk in progress.
    const MovementRequestId walking = walk({300, 250});
    const MovementRequestId here = walk(hero.position());
    CHECK_EQ(listener.count(concat(id, walking, " replaced")), std::size_t{1});
    CHECK_EQ(listener.count(concat(id, here, " completed")), std::size_t{1});
    CHECK_EQ(listener.count("walk #"), std::size_t{7}); // Exactly one event per request.

    // Walking switches clips (idle -> walk -> idle): each switch reports the playback it ended.
    CHECK(listener.count(concat("actor #", hero.handle().id)) >= std::size_t{2});
    CHECK_EQ(listener.count(" finished"), std::size_t{0}); // Looping clips never finish.
    // A failed request reports nothing.
    const std::size_t before = listener.events.size();
    CHECK(!room->walkTo(*room->actor("plant"), {60, 60}).has_value());
    CHECK_EQ(listener.events.size(), before);

    room.reset();
    CHECK_EQ(listener.destroyed, 1);
}

SCHUIMM_TEST(Movement_ActorsOutsideARoomReportNothing)
{
    MovementFixture fixture;
    auto room = fixture.room("Flat");
    RecordingListener listener;
    room->setEventListener(&listener);
    room->setEventListener(nullptr); // Detached: nothing is reported.
    REQUIRE(room->walkTo(*room->actor("hero"), {150, 100}).has_value());
    run(*room, 1.0, 0.1);
    CHECK(listener.events.empty());
    room.reset();
    CHECK_EQ(listener.destroyed, 0);
}
