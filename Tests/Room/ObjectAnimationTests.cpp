// Animated room objects: the resource schema (static sprite, animations or nothing), loading and
// validation, per-object playback state, controls, update, drawing, playback identity and room
// recreation.

#include "ScriptTestSupport.h"
#include "TestAssets.h"
#include "TestFramework.h"
#include "TestPng.h"

#include "Core/Logging/Logger.h"
#include "Platform/FileSystem/FileSystem.h"
#include "Platform/FileSystem/Path.h"
#include "Core/Common/StringBuilder.h"
#include "Room/RoomInstance.h"
#include "Room/RoomLibrary.h"
#include "Room/RoomResourceLoader.h"

#include <cmath>
#include <limits>
#include <string>
#include <string_view>

using namespace schuimm;
using namespace schuimm::room;
using animation::PlaybackStatus;

namespace {

std::span<const std::byte> bytesOf(std::string_view text)
{
    return std::as_bytes(std::span(text.data(), text.size()));
}

bool contains(std::string_view text, std::string_view part)
{
    if (text.find(part) != std::string_view::npos) return true;
    std::fprintf(stderr, "    message: %.*s\n    expected to contain: %.*s\n", static_cast<int>(text.size()), text.data(),
                 static_cast<int>(part.size()), part.data());
    return false;
}

// fan.png is 64 x 32. Frames of different sizes and pivots.
constexpr std::string_view fanAnimation = R"({ "version": 1, "clips": {
  "idle": { "loop": true, "frames": [
    { "image": "Objects/Fan/fan.png", "source": [0, 0, 32, 32], "pivot": [16, 30], "duration": 10 } ] },
  "spinning": { "loop": true, "frames": [
    { "image": "Objects/Fan/fan.png", "source": [0, 0, 32, 32], "pivot": [16, 30], "duration": 5 },
    { "image": "Objects/Fan/fan.png", "source": [32, 0, 32, 24], "pivot": [16, 22], "duration": 5 } ] },
  "burst": { "loop": false, "frames": [
    { "image": "Objects/Fan/fan.png", "source": [0, 0, 16, 16], "pivot": [8, 16], "duration": 4 },
    { "image": "Objects/Fan/fan.png", "source": [16, 0, 32, 24], "pivot": [16, 20], "duration": 4 } ] }
} })";

// Two fans share one animation resource; a lamp has a static sprite; a hotspot has nothing.
constexpr std::string_view workshopRoom = R"({
  "version": 1, "id": "workshop", "size": { "width": 800, "height": 600 },
  "objects": [
    { "id": "fan", "position": [400, 300],
      "animation_resources": { "main": "Objects/Fan/fan.anim.json" },
      "animations": { "idle": { "resource": "main", "clip": "idle" },
                      "spinning": { "resource": "main", "clip": "spinning" },
                      "burst": { "resource": "main", "clip": "burst" } },
      "default_animation": "idle" },
    { "id": "second_fan", "position": [100, 350],
      "animation_resources": { "main": "Objects/Fan/fan.anim.json" },
      "animations": { "idle": { "resource": "main", "clip": "idle" },
                      "spinning": { "resource": "main", "clip": "spinning" } },
      "default_animation": "spinning" },
    { "id": "lamp", "sprite": "Objects/lamp.png", "position": [200, 320], "pivot": [4, 8] },
    { "id": "hotspot", "position": [10, 10] } ] })";

struct Fixture {
    std::string root = test::makeScratchDirectory("object_animation");
    Logger logger{LogLevel::Error};
    script::ScriptHost host{logger};

    Fixture()
    {
        writeBytes("Objects/Fan/fan.png", test::pngHeader(64, 32));
        writeBytes("Objects/lamp.png", test::pngHeader(8, 8));
        writeText("Objects/Fan/fan.anim.json", fanAnimation);
        writeText("Rooms/Workshop.room.json", workshopRoom);
    }

    void writeText(std::string_view asset, std::string_view text) const { writeBytes(asset, bytesOf(text)); }

    void writeBytes(std::string_view asset, std::span<const std::byte> bytes) const
    {
        const std::string file = path::join(root, asset);
        REQUIRE(fs::createDirectories(path::parent(file)).has_value());
        REQUIRE(fs::writeFile(file, bytes).has_value());
    }

    [[nodiscard]] AssetLocator locator() const { return AssetLocator::create(root).value(); }
};

AssetPath asset(std::string_view text) { return AssetPath::parse(text).value(); }

std::unique_ptr<RoomInstance> workshop(Fixture& fixture, RoomLibrary& library)
{
    auto loaded = library.load(asset("Rooms/Workshop.room.json"));
    if (!loaded) std::fprintf(stderr, "%s\n", loaded.error().message.c_str());
    REQUIRE(loaded.has_value());
    auto instance = RoomInstance::create(*loaded, fixture.host);
    REQUIRE(instance.has_value());
    return std::move(*instance);
}

// The workshop room with `from` replaced by `to`, loaded: the error message, or "" if it loads.
std::string loadError(Fixture& fixture, std::string_view from, std::string_view to)
{
    std::string text(workshopRoom);
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
    fixture.writeText("Rooms/Variant.room.json", text);
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator);
    auto loaded = library.load(asset("Rooms/Variant.room.json"));
    return loaded ? std::string() : loaded.error().message;
}

const RoomDrawItem* drawn(const RoomDrawList& list, std::size_t objectIndex)
{
    for (const RoomDrawItem& item : list.items) {
        if (item.kind == RoomDrawKind::Object && item.index == objectIndex) return &item;
    }
    return nullptr;
}

} // namespace

SCHUIMM_TEST(ObjectAnimation_ResourcesLoadAndValidate)
{
    Fixture fixture;
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator);
    auto loaded = library.load(asset("Rooms/Workshop.room.json"));
    REQUIRE(loaded.has_value());
    const RoomResource& resource = *loaded->resource;
    REQUIRE(resource.objects.size() == 4);
    REQUIRE(resource.objects[0].animation.has_value());
    CHECK_EQ(resource.objects[0].animation->defaultAnimation, std::string("idle"));
    CHECK(!resource.objects[0].sprite.has_value());
    CHECK(resource.objects[2].sprite.has_value());
    CHECK(!resource.objects[2].animation.has_value());
    CHECK(!resource.objects[3].sprite.has_value() && !resource.objects[3].animation.has_value());
    REQUIRE(loaded->objectAnimations.size() == 4);
    REQUIRE(loaded->objectAnimations[0] != nullptr);
    CHECK(loaded->objectAnimations[0]->find("spinning") != nullptr);
    CHECK(loaded->objectAnimations[2] == nullptr);
    CHECK(loaded->objectAnimations[3] == nullptr);
    // One animation resource, shared through the cache, for both fans.
    CHECK_EQ(library.actors().animationCount(), std::size_t{1});
    CHECK(loaded->objectAnimations[0]->resources[0].second == loaded->objectAnimations[1]->resources[0].second);
}

SCHUIMM_TEST(ObjectAnimation_InvalidConfigurationsAreRejected)
{
    Fixture fixture;
    CHECK(contains(loadError(fixture, R"("id": "fan", "position": [400, 300],)",
                             R"("id": "fan", "position": [400, 300], "sprite": "Objects/lamp.png",)"),
                   "objects[0].sprite: an object has either a static sprite or animations"));
    CHECK(contains(loadError(fixture, R"("id": "fan", "position": [400, 300],)",
                             R"("id": "fan", "position": [400, 300], "pivot": [1, 1],)"),
                   "objects[0].pivot: an animated object takes its pivot from its animation frames"));
    CHECK(contains(loadError(fixture, R"("default_animation": "idle" },)", R"("default_animation": "missing" },)"),
                   "objects[0].default_animation: no animation 'missing'"));
    CHECK(contains(loadError(fixture, R"(,
      "default_animation": "idle" },)", " },"),
                   "objects[0].default_animation: missing required property"));
    CHECK(contains(loadError(fixture, R"("burst": { "resource": "main", "clip": "burst" })",
                             R"("burst": { "resource": "other", "clip": "burst" })"),
                   "objects[0].animations.burst.resource: no animation resource 'other' in animation_resources"));
    CHECK(contains(loadError(fixture, R"("burst": { "resource": "main", "clip": "burst" })",
                             R"("burst": { "resource": "main", "clip": "explode" })"),
                   "objects[0] ('fan'): animations.burst.clip: no clip 'explode' in 'Objects/Fan/fan.anim.json'"));
    CHECK(contains(loadError(fixture, R"("burst": { "resource": "main", "clip": "burst" })",
                             R"("burst": { "resource": "main", "clip": "burst", "flip_x": true })"),
                   "objects[0].animations.burst.flip_x: unknown property"));
    CHECK(contains(loadError(fixture, R"("main": "Objects/Fan/fan.anim.json" },
      "animations": { "idle": { "resource": "main", "clip": "idle" },
                      "spinning")",
                             R"("main": "Objects/Fan/missing.anim.json" },
      "animations": { "idle": { "resource": "main", "clip": "idle" },
                      "spinning")"),
                   "objects[0].animation_resources.main: asset 'Objects/Fan/missing.anim.json'"));
    CHECK(contains(loadError(fixture, R"("animation_resources": { "main": "Objects/Fan/fan.anim.json" },
      "animations": { "idle": { "resource": "main", "clip": "idle" },
                      "spinning")",
                             R"("animation_resources": {},
      "animations": { "idle": { "resource": "main", "clip": "idle" },
                      "spinning")"),
                   "needs at least one animation resource"));
    CHECK(loadError(fixture, R"("id": "hotspot", "position": [10, 10])", R"("id": "hotspot", "position": [10, 10])").empty());
}

SCHUIMM_TEST(ObjectAnimation_ObjectsStartWithTheirAuthoredState)
{
    Fixture fixture;
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator);
    auto room = workshop(fixture, library);
    RoomObject& fan = *room->object("fan");
    CHECK(fan.isAnimated());
    CHECK_EQ(fan.animation(), std::string("idle"));
    CHECK(fan.player().status() == PlaybackStatus::Playing);
    CHECK_EQ(fan.player().clip().clip().name, std::string("idle"));
    CHECK_EQ(room->object("second_fan")->animation(), std::string("spinning"));
    for (const char* id : {"lamp", "hotspot"}) {
        const RoomObject& object = *room->object(id);
        CHECK(!object.isAnimated());
        CHECK(object.animation().empty());
        CHECK(!object.player().hasClip());
    }
}

SCHUIMM_TEST(ObjectAnimation_SharedResourcesHaveIndependentPlayback)
{
    Fixture fixture;
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator);
    auto room = workshop(fixture, library);
    auto other = workshop(fixture, library); // A second instance of the same room.
    RoomObject& fan = *room->object("fan");
    RoomObject& second = *room->object("second_fan");
    REQUIRE(fan.playAnimation("spinning").has_value());
    room->update(3.0 / 60.0); // 3 ticks.
    REQUIRE(fan.pauseAnimation().has_value());
    room->update(4.0 / 60.0);
    CHECK_EQ(fan.player().elapsedTicks(), std::uint64_t{3});
    CHECK(fan.player().status() == PlaybackStatus::Paused);
    CHECK_EQ(second.player().elapsedTicks(), std::uint64_t{7}); // Same clip, its own position.
    CHECK(second.player().status() == PlaybackStatus::Playing);
    CHECK_EQ(other->object("fan")->animation(), std::string("idle"));
    CHECK_EQ(other->object("fan")->player().elapsedTicks(), std::uint64_t{0});
}

SCHUIMM_TEST(ObjectAnimation_PlaybackControls)
{
    Fixture fixture;
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator);
    auto room = workshop(fixture, library);
    RoomObject& fan = *room->object("fan");
    REQUIRE(fan.playAnimation("spinning").has_value());
    room->update(3.0 / 60.0);
    // Playing the current animation continues it untouched.
    REQUIRE(fan.playAnimation("spinning").has_value());
    CHECK_EQ(fan.player().elapsedTicks(), std::uint64_t{3});
    // A non-looping clip finishes on its last frame...
    REQUIRE(fan.playAnimation("burst").has_value());
    room->update(10.0 / 60.0);
    CHECK(fan.player().status() == PlaybackStatus::Finished);
    CHECK_EQ(fan.player().frameIndex(), std::size_t{1});
    // ...and plays again when requested once more.
    REQUIRE(fan.playAnimation("burst").has_value());
    CHECK(fan.player().status() == PlaybackStatus::Playing);
    CHECK_EQ(fan.player().elapsedTicks(), std::uint64_t{0});
    REQUIRE(fan.pauseAnimation().has_value());
    REQUIRE(fan.resumeAnimation().has_value());
    CHECK(fan.player().status() == PlaybackStatus::Playing);
    REQUIRE(fan.stopAnimation().has_value());
    CHECK(fan.player().status() == PlaybackStatus::Stopped);
    REQUIRE(fan.restartAnimation().has_value());
    CHECK(fan.player().status() == PlaybackStatus::Playing);
    // Errors leave the state alone.
    const auto unknown = fan.playAnimation("explode");
    REQUIRE(!unknown.has_value());
    CHECK(unknown.error().code == ErrorCode::NotFound);
    CHECK(contains(unknown.error().message, "object 'fan' has no animation 'explode'"));
    CHECK_EQ(fan.animation(), std::string("burst"));
    // A static object never becomes animated.
    RoomObject& lamp = *room->object("lamp");
    for (const Status& status : {lamp.playAnimation("idle"), lamp.pauseAnimation(), lamp.resumeAnimation(),
                                 lamp.stopAnimation(), lamp.restartAnimation()}) {
        REQUIRE(!status.has_value());
        CHECK(status.error().code == ErrorCode::InvalidState);
        CHECK(contains(status.error().message, "object 'lamp' has no animations"));
    }
    CHECK(!lamp.isAnimated());
    CHECK(!room->object("hotspot")->playAnimation("idle").has_value());
}

SCHUIMM_TEST(ObjectAnimation_PlaybackIdentity)
{
    Fixture fixture;
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator);
    auto room = workshop(fixture, library);
    RoomObject& fan = *room->object("fan");
    REQUIRE(fan.playAnimation("burst").has_value());
    const std::uint64_t burst = fan.player().playbackGeneration();
    // Same playback: continuing, pausing, resuming, advancing and finishing.
    REQUIRE(fan.playAnimation("burst").has_value());
    REQUIRE(fan.pauseAnimation().has_value());
    REQUIRE(fan.resumeAnimation().has_value());
    room->update(1.0);
    CHECK(fan.player().status() == PlaybackStatus::Finished);
    CHECK_EQ(fan.player().playbackGeneration(), burst);
    // A new playback: playing the finished clip again, restarting, stopping, switching.
    REQUIRE(fan.playAnimation("burst").has_value());
    const std::uint64_t again = fan.player().playbackGeneration();
    CHECK(again != burst);
    REQUIRE(fan.restartAnimation().has_value());
    CHECK(fan.player().playbackGeneration() != again);
    const std::uint64_t restarted = fan.player().playbackGeneration();
    REQUIRE(fan.stopAnimation().has_value());
    CHECK(fan.player().playbackGeneration() != restarted);
    const std::uint64_t stopped = fan.player().playbackGeneration();
    REQUIRE(fan.stopAnimation().has_value()); // Already stopped: nothing ends.
    CHECK_EQ(fan.player().playbackGeneration(), stopped);
    REQUIRE(fan.playAnimation("spinning").has_value());
    CHECK(fan.player().playbackGeneration() != stopped);
}

SCHUIMM_TEST(ObjectAnimation_UpdateAdvancesOnceWithTheRoomClock)
{
    Fixture fixture;
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator);
    auto room = workshop(fixture, library);
    RoomObject& fan = *room->object("fan");
    REQUIRE(fan.playAnimation("spinning").has_value());
    for (int i = 0; i < 4; ++i) room->update(1.0 / 60.0);
    CHECK_EQ(fan.player().elapsedTicks(), std::uint64_t{4});
    CHECK_EQ(fan.player().frameIndex(), std::size_t{0});
    room->update(1.0 / 120.0); // Half a tick: carried, not lost.
    room->update(1.0 / 120.0);
    CHECK_EQ(fan.player().frameIndex(), std::size_t{1}); // Tick 5: the second frame.
    // Invisible objects keep playing (like actors); drawing never advances anything.
    fan.setVisible(false);
    room->update(2.0 / 60.0);
    CHECK_EQ(fan.player().elapsedTicks(), std::uint64_t{7});
    for (int i = 0; i < 5; ++i) (void)room->drawList();
    CHECK_EQ(fan.player().elapsedTicks(), std::uint64_t{7});
}

SCHUIMM_TEST(ObjectAnimation_DrawListUsesTheCurrentFrame)
{
    Fixture fixture;
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator);
    auto room = workshop(fixture, library);
    RoomObject& fan = *room->object("fan");
    RoomDrawList list = room->drawList();
    const RoomDrawItem* item = drawn(list, 0);
    REQUIRE(item != nullptr);
    CHECK(item->sprite.image == asset("Objects/Fan/fan.png"));
    CHECK(item->sprite.source == (RectI{0, 0, 32, 32}));
    CHECK(item->sprite.destination == (RectF{384, 270, 32, 32})); // Pivot (16, 30) on (400, 300).
    // A frame of another size keeps the pivot on the object's position.
    REQUIRE(fan.playAnimation("spinning").has_value());
    room->update(5.0 / 60.0);
    list = room->drawList();
    item = drawn(list, 0);
    REQUIRE(item != nullptr);
    CHECK(item->sprite.source == (RectI{32, 0, 32, 24}));
    CHECK(item->sprite.destination == (RectF{384, 278, 32, 24})); // Pivot (16, 22).
    CHECK(!item->sprite.flipX);
    // Depth: objects sort by position y with everything else; the static lamp is still drawn.
    const RoomDrawItem* lamp = drawn(list, 2);
    REQUIRE(lamp != nullptr);
    CHECK(lamp->sprite.destination == (RectF{196, 312, 8, 8}));
    CHECK(drawn(list, 3) == nullptr); // The hotspot has nothing to draw.
    std::vector<std::size_t> order;
    for (const RoomDrawItem& entry : list.items) order.push_back(entry.index);
    CHECK(order == (std::vector<std::size_t>{0, 2, 1})); // y 300, 320, 350.
    REQUIRE(fan.setPosition({400, 400}).has_value());
    list = room->drawList();
    order.clear();
    for (const RoomDrawItem& entry : list.items) order.push_back(entry.index);
    CHECK(order == (std::vector<std::size_t>{2, 1, 0}));
    // Hidden objects are not drawn.
    fan.setVisible(false);
    CHECK(drawn(room->drawList(), 0) == nullptr);
}

SCHUIMM_TEST(ObjectAnimation_PositionsAreValidated)
{
    Fixture fixture;
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator);
    auto room = workshop(fixture, library);
    for (RoomObject& object : room->objects()) {
        for (const Vec2 bad : {Vec2{std::numeric_limits<float>::quiet_NaN(), 0}, Vec2{0, std::numeric_limits<float>::infinity()},
                               Vec2{2e6f, 0}}) {
            const Status status = object.setPosition(bad);
            REQUIRE(!status.has_value());
            CHECK(status.error().code == ErrorCode::InvalidArgument);
        }
        CHECK(object.position() == object.definition().position); // Unchanged.
    }
}

SCHUIMM_TEST(ObjectAnimation_RoomsRecreateAuthoredStateAndReleasePlayback)
{
    Fixture fixture;
    const AssetLocator locator = fixture.locator();
    std::weak_ptr<const animation::AnimationResource> resource;
    {
        RoomLibrary library(locator);
        {
            auto room = workshop(fixture, library);
            RoomObject& fan = *room->object("fan");
            REQUIRE(fan.playAnimation("spinning").has_value());
            REQUIRE(fan.setPosition({10, 20}).has_value());
            room->update(0.5);
            resource = fan.player().clip().resource();
        }
        // Re-entering the room starts from the authored configuration: no memory of the old instance.
        auto again = workshop(fixture, library);
        const RoomObject& fan = *again->object("fan");
        CHECK_EQ(fan.animation(), std::string("idle"));
        CHECK_EQ(fan.player().elapsedTicks(), std::uint64_t{0});
        CHECK(fan.position() == (Vec2{400, 300}));
        library.unloadAll();
        CHECK(!resource.expired()); // The live instance keeps what it plays.
    }
    CHECK(resource.expired()); // Room and library gone: the playback state and resource are released.
}

namespace {

struct PlaybackListener final : RoomEventListener {
    std::vector<std::string> events;
    void movementEnded(script::ObjectRef, MovementRequestId, MovementOutcome) override {}
    void playbackEnded(script::ObjectRef entity, bool isActor, std::uint64_t generation, PlaybackOutcome outcome) override
    {
        events.push_back(concat(isActor ? "actor" : "object", " #", entity.id, " g", generation, " ", toString(outcome)));
    }
    void roomDestroyed() override {}
};

} // namespace

SCHUIMM_TEST(ObjectAnimation_PlaybackEndsAreReportedWhenTheyHappen)
{
    Fixture fixture;
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator);
    auto room = workshop(fixture, library);
    PlaybackListener listener;
    room->setEventListener(&listener);
    RoomObject& fan = *room->object("fan");
    const std::string id = concat("object #", fan.handle().id, " g");
    const std::uint64_t idle = fan.player().playbackGeneration();
    REQUIRE(fan.playAnimation("burst").has_value()); // Replaces the idle playback.
    const std::uint64_t burst = fan.player().playbackGeneration();
    CHECK(listener.events == (std::vector<std::string>{concat(id, idle, " replaced")}));
    REQUIRE(fan.pauseAnimation().has_value());
    REQUIRE(fan.resumeAnimation().has_value());
    REQUIRE(fan.playAnimation("burst").has_value()); // Continues: the same playback, nothing ends.
    CHECK_EQ(listener.events.size(), std::size_t{1});
    room->update(1.0);
    CHECK_EQ(listener.events.back(), concat(id, burst, " finished"));
    // Another playback starting after the finish does not turn it into "replaced".
    REQUIRE(fan.playAnimation("spinning").has_value());
    CHECK_EQ(listener.events.size(), std::size_t{2});
    const std::uint64_t spinning = fan.player().playbackGeneration();
    REQUIRE(fan.stopAnimation().has_value());
    CHECK_EQ(listener.events.back(), concat(id, spinning, " replaced"));
    REQUIRE(fan.stopAnimation().has_value()); // Already stopped.
    CHECK_EQ(listener.events.size(), std::size_t{3});
    // Snapshots never report.
    RoomObject snapshot = *room->object("fan");
    REQUIRE(snapshot.restartAnimation().has_value());
    REQUIRE(snapshot.playAnimation("burst").has_value());
    CHECK_EQ(listener.events.size(), std::size_t{3});
    room->setEventListener(nullptr);
}
