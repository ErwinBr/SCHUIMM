// Actors: actor resources (format, validation, directional resolution), actor instances
// (identity, state, playback, render state) and their integration with rooms (placements,
// ownership, handles, lifetime, drawing order).

#include "ScriptTestSupport.h"
#include "TestAssets.h"
#include "TestFramework.h"
#include "TestPng.h"

#include "Animation/AnimationResourceLoader.h"
#include "Core/Logging/Logger.h"
#include "Platform/FileSystem/FileSystem.h"
#include "Platform/FileSystem/Path.h"
#include "Room/ActorInstance.h"
#include "Room/ActorResourceLoader.h"
#include "Room/RoomInstance.h"
#include "Room/RoomLibrary.h"
#include "Room/RoomResourceLoader.h"
#include "Script/Bytecode/BytecodeWriter.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

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
    std::fprintf(stderr, "    message: %.*s\n    expected to contain: %.*s\n", static_cast<int>(text.size()),
                 text.data(), static_cast<int>(part.size()), part.data());
    return false;
}

// --- Shared test content ---------------------------------------------------------------------

// Image sizes: hero.png 256 x 128, glow.png 16 x 16.
constexpr std::string_view heroAnimation = R"({ "version": 1, "clips": {
  "idle_south": { "loop": true, "frames": [
    { "image": "Actors/Hero/hero.png", "source": [0, 0, 32, 64], "pivot": [16, 60], "duration": 10 },
    { "image": "Actors/Hero/hero.png", "source": [32, 0, 40, 72], "pivot": [20, 70], "duration": 10 } ] },
  "idle_north": { "loop": true, "frames": [
    { "image": "Actors/Hero/hero.png", "source": [72, 0, 32, 64], "pivot": [16, 60], "duration": 20 } ] },
  "idle_west": { "loop": true, "frames": [
    { "image": "Actors/Hero/hero.png", "source": [104, 0, 30, 64], "pivot": [10, 60], "duration": 10 },
    { "image": "Actors/Hero/hero.png", "source": [134, 0, 30, 64], "pivot": [10, 60], "duration": 10 } ] },
  "wave_south": { "loop": false, "frames": [
    { "image": "Actors/Hero/hero.png", "source": [0, 64, 48, 64], "pivot": [24, 60], "duration": 6 },
    { "image": "Actors/Hero/hero.png", "source": [48, 64, 48, 64], "pivot": [24, 60], "duration": 6 } ] },
  "glow": { "loop": true, "frames": [ { "image": "Actors/Hero/glow.png", "duration": 5 } ] }
} })";

constexpr std::string_view heroActor = R"({
  "version": 1,
  "id": "hero",
  "animation_resources": { "main": "Actors/Hero/hero.anim.json" },
  "animations": {
    "idle": {
      "south": { "resource": "main", "clip": "idle_south" },
      "north": { "resource": "main", "clip": "idle_north" },
      "west": { "resource": "main", "clip": "idle_west" },
      "east": { "resource": "main", "clip": "idle_west", "flip_x": true },
      "fallback": "south"
    },
    "wave": { "south": { "resource": "main", "clip": "wave_south" }, "fallback": "south" },
    "glow": { "any": { "resource": "main", "clip": "glow" } },
    "look_up": { "north": { "resource": "main", "clip": "idle_north" } }
  },
  "default_animation": "idle",
  "default_facing": "south"
})";

Result<Extent2D> fakeImageSize(const AssetPath& image)
{
    if (image.view() == "Actors/Hero/hero.png") return Extent2D{256, 128};
    if (image.view() == "Actors/Hero/glow.png") return Extent2D{16, 16};
    return makeError(ErrorCode::NotFound, concat("asset '", image.view(), "' does not exist"));
}

std::shared_ptr<const animation::AnimationResource> heroAnimationResource()
{
    static const auto resource = [] {
        auto decoded = animation::decodeAnimationResource(bytesOf(heroAnimation), "hero.anim.json", fakeImageSize);
        REQUIRE(decoded.has_value());
        return std::make_shared<const animation::AnimationResource>(std::move(*decoded));
    }();
    return resource;
}

const AnimationResolver fakeAnimations = [](const AssetPath& path) -> Result<std::shared_ptr<const animation::AnimationResource>> {
    if (path.view() == "Actors/Hero/hero.anim.json") return heroAnimationResource();
    return makeError(ErrorCode::NotFound, concat("asset '", path.view(), "' does not exist"));
};

const AssetCheck fakeScripts = [](const AssetPath& path) -> Status {
    if (path.view() == "Actors/Hero/hero.schb") return {};
    return makeError(ErrorCode::NotFound, concat("asset '", path.view(), "' does not exist"));
};

Result<ActorResource> decodeActor(std::string_view text)
{
    return decodeActorResource(bytesOf(text), "hero.actor.json", fakeAnimations, fakeScripts);
}

std::shared_ptr<const ActorResource> heroResource()
{
    auto actor = decodeActor(heroActor);
    if (!actor) std::fprintf(stderr, "%s\n", actor.error().message.c_str());
    REQUIRE(actor.has_value());
    return std::make_shared<const ActorResource>(std::move(*actor));
}

// heroActor with `json` (a JSON fragment "key": value) spliced in after the version.
std::string actorWith(std::string_view fragment)
{
    std::string text(heroActor);
    text.insert(text.find("\"version\": 1,") + 13, concat("\n  ", fragment, ","));
    return text;
}

// heroActor with `from` replaced by `to` (once).
std::string actorReplacing(std::string_view from, std::string_view to)
{
    std::string text(heroActor);
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
    return text;
}

#define CHECK_ACTOR_ERROR(text, expectedCode, expected)                                            \
    do {                                                                                           \
        const auto result_ = decodeActor(text);                                                    \
        CHECK(!result_.has_value());                                                               \
        if (!result_.has_value()) {                                                                \
            CHECK(result_.error().code == (expectedCode));                                         \
            CHECK(contains(result_.error().message, expected));                                    \
        }                                                                                          \
    } while (false)

ActorInstance makeActor(std::string id, const std::shared_ptr<const ActorResource>& resource, Vec2 position = {100, 200})
{
    auto actor = ActorInstance::create(std::move(id), resource, defaultActorConfiguration(*resource, position));
    REQUIRE(actor.has_value());
    return std::move(*actor);
}

std::string clipName(const ActorInstance& actor)
{
    return actor.player().clip().clip().name;
}

} // namespace

// --- Actor resources ---

SCHUIMM_TEST(Actor_DecodesAResource)
{
    const auto actor = heroResource();
    CHECK(actor->id == "hero");
    CHECK(!actor->script.has_value());
    CHECK_EQ(actor->animationResources.size(), std::size_t{1});
    CHECK(actor->animationResources.at("main").resource == heroAnimationResource()); // Referenced, not copied.
    CHECK_EQ(actor->animations.size(), std::size_t{4});
    CHECK(actor->defaultAnimation == "idle");
    CHECK(actor->defaultFacing == Facing::South);
    CHECK(actor->defaultScale == 1.0f);
}

SCHUIMM_TEST(Actor_ScriptReferenceIsOptionalAndChecked)
{
    const auto withScript = decodeActor(actorWith(R"("script": "Actors/Hero/hero.schb")"));
    REQUIRE(withScript.has_value());
    CHECK(withScript->script == AssetPath::parse("Actors/Hero/hero.schb").value());
    CHECK_ACTOR_ERROR(actorWith(R"("script": "Actors/Hero/missing.schb")"), ErrorCode::NotFound,
                      "hero.actor.json: script: asset 'Actors/Hero/missing.schb' does not exist");
    CHECK_ACTOR_ERROR(actorWith(R"("script": "Actors/Hero/hero.sch")"), ErrorCode::InvalidData,
                      "script: must name a compiled script (.schb)");
    // null is the same as omitted.
    CHECK(decodeActor(actorWith(R"("script": null)")).has_value());
}

SCHUIMM_TEST(Actor_ResolutionOrder)
{
    const auto actor = heroResource();
    const auto clip = [&](std::string_view animation, Facing facing) {
        auto resolved = actor->resolve(animation, facing);
        REQUIRE(resolved.has_value());
        return std::make_pair(resolved->clip.clip().name, resolved->flipX);
    };
    // 1. Explicit mappings, with explicit mirroring.
    CHECK(clip("idle", Facing::North) == std::make_pair(std::string("idle_north"), false));
    CHECK(clip("idle", Facing::West) == std::make_pair(std::string("idle_west"), false));
    CHECK(clip("idle", Facing::East) == std::make_pair(std::string("idle_west"), true));
    // 2. Direction-independent.
    for (const Facing facing : allFacings) CHECK(clip("glow", facing) == std::make_pair(std::string("glow"), false));
    // 3. The fallback facing's mapping.
    CHECK(clip("idle", Facing::NorthEast) == std::make_pair(std::string("idle_south"), false));
    CHECK(clip("wave", Facing::West) == std::make_pair(std::string("wave_south"), false));
    // 4. Nothing else: no other facing is chosen silently.
    const auto missing = actor->resolve("look_up", Facing::South);
    REQUIRE(!missing.has_value());
    CHECK(missing.error().code == ErrorCode::NotFound);
    CHECK(contains(missing.error().message, "actor 'hero' animation 'look_up' has no clip for facing 'south'"));
    const auto unknown = actor->resolve("dance", Facing::South);
    REQUIRE(!unknown.has_value());
    CHECK(contains(unknown.error().message, "actor 'hero' has no animation 'dance'"));
}

SCHUIMM_TEST(Actor_InvalidResources)
{
    CHECK_ACTOR_ERROR("{", ErrorCode::InvalidData, "hero.actor.json: invalid JSON at line 1");
    CHECK_ACTOR_ERROR(actorReplacing("\"version\": 1", "\"version\": 2"), ErrorCode::InvalidData,
                      "version: unsupported actor resource version 2 (this engine reads version 1)");
    CHECK_ACTOR_ERROR(actorReplacing("\"id\": \"hero\",", ""), ErrorCode::InvalidData, "id: missing required property");
    CHECK_ACTOR_ERROR(actorReplacing("\"id\": \"hero\"", "\"id\": \"the hero\""), ErrorCode::InvalidData,
                      "id: invalid identifier 'the hero'");
    CHECK_ACTOR_ERROR(actorWith(R"("speed": 3)"), ErrorCode::InvalidData, "speed: unknown property");
    CHECK_ACTOR_ERROR(actorWith(R"("id": "again")"), ErrorCode::InvalidData, "id: duplicate property 'id'");
    // Animation resources.
    CHECK_ACTOR_ERROR(actorReplacing(R"({ "main": "Actors/Hero/hero.anim.json" })", "{}"), ErrorCode::InvalidData,
                      "animation_resources: an actor needs at least one animation resource");
    CHECK_ACTOR_ERROR(actorReplacing(R"("main": "Actors/Hero/hero.anim.json")",
                                     R"("main": "Actors/Hero/hero.anim.json", "extra": "Actors/Hero/none.anim.json")"),
                      ErrorCode::NotFound,
                      "hero.actor.json: animation_resources.extra: asset 'Actors/Hero/none.anim.json' does not exist");
    CHECK_ACTOR_ERROR(actorReplacing(R"("main": "Actors)", R"("1main": "Actors)"), ErrorCode::InvalidData,
                      "animation_resources.1main: invalid animation resource name '1main'");
    // Mappings.
    CHECK_ACTOR_ERROR(actorReplacing(R"("resource": "main", "clip": "idle_north")",
                                     R"("resource": "other", "clip": "idle_north")"),
                      ErrorCode::InvalidData,
                      "animations.idle.north.resource: no animation resource 'other' in animation_resources");
    CHECK_ACTOR_ERROR(actorReplacing(R"("clip": "idle_north")", R"("clip": "idle_up")"), ErrorCode::InvalidData,
                      "animations.idle.north.clip: animation resource 'Actors/Hero/hero.anim.json' has no clip 'idle_up'");
    CHECK_ACTOR_ERROR(actorReplacing(R"("flip_x": true)", R"("flip_x": "yes")"), ErrorCode::InvalidData,
                      "animations.idle.east.flip_x: expected true or false, found a string");
    CHECK_ACTOR_ERROR(actorReplacing(R"("clip": "idle_north" })", R"("clip": "idle_north", "speed": 2 })"),
                      ErrorCode::InvalidData, "animations.idle.north.speed: unknown property");
    // Facings and fallbacks.
    CHECK_ACTOR_ERROR(actorReplacing(R"("north": { "resource")", R"("up": { "resource")"), ErrorCode::InvalidData,
                      "animations.idle.up: unknown property (expected a facing name, \"any\" or \"fallback\")");
    CHECK_ACTOR_ERROR(actorReplacing(R"("fallback": "south")", R"("fallback": "southwards")"), ErrorCode::InvalidData,
                      "animations.idle.fallback: unknown facing 'southwards'");
    CHECK_ACTOR_ERROR(actorReplacing(R"("fallback": "south")", R"("fallback": "south_west")"), ErrorCode::InvalidData,
                      "animations.idle.fallback: facing 'south_west' has no clip in this animation");
    CHECK_ACTOR_ERROR(actorReplacing(R"("glow": { "any": { "resource": "main", "clip": "glow" } })",
                                     R"("glow": { "any": { "resource": "main", "clip": "glow" }, "south": { "resource": "main", "clip": "glow" } })"),
                      ErrorCode::InvalidData, "animations.glow.south: not allowed in an animation that uses \"any\"");
    CHECK_ACTOR_ERROR(actorReplacing(R"("glow": { "any": { "resource": "main", "clip": "glow" } })", R"("glow": {})"),
                      ErrorCode::InvalidData, "animations.glow: an animation needs a clip for at least one facing, or \"any\"");
    CHECK_ACTOR_ERROR(actorReplacing(R"("glow": { "any": { "resource": "main", "clip": "glow" } })",
                                     R"("glow": { "fallback": "south" })"),
                      ErrorCode::InvalidData, "animations.glow: an animation needs a clip");
    CHECK_ACTOR_ERROR(actorReplacing(R"("glow": {)", R"("glow-2": {)"), ErrorCode::InvalidData,
                      "animations.glow-2: invalid animation name 'glow-2'");
    // Defaults.
    CHECK_ACTOR_ERROR(actorReplacing(R"("default_animation": "idle")", R"("default_animation": "dance")"),
                      ErrorCode::InvalidData, "default_animation: no animation 'dance'");
    CHECK_ACTOR_ERROR(actorReplacing(R"("default_facing": "south")", R"("default_facing": "down")"),
                      ErrorCode::InvalidData, "default_facing: unknown facing 'down'");
    CHECK_ACTOR_ERROR(actorReplacing(R"("default_animation": "idle")", R"("default_animation": "look_up")"),
                      ErrorCode::InvalidData,
                      "default_facing: the default animation does not resolve: actor 'hero' animation 'look_up' has no clip for facing 'south'");
    // The same animation is fine as a default with a facing it has a clip for.
    std::string lookingUp = actorReplacing(R"("default_animation": "idle")", R"("default_animation": "look_up")");
    lookingUp.replace(lookingUp.find(R"("default_facing": "south")"), 25, R"("default_facing": "north")");
    CHECK(decodeActor(lookingUp).has_value());
}

SCHUIMM_TEST(Actor_DefaultScale)
{
    const auto scaled = decodeActor(actorWith(R"("default_scale": 0.75)"));
    REQUIRE(scaled.has_value());
    CHECK(scaled->defaultScale == 0.75f);
    for (const char* bad : {"0", "-1", "1e4", "\"1\"", "1e-50"}) {
        const auto result = decodeActor(actorWith(concat(R"("default_scale": )", bad)));
        if (result.has_value()) {
            SCHUIMM_TEST_FAIL_(concat("default_scale ", bad, " was accepted"), false);
            continue;
        }
        CHECK(contains(result.error().message, "default_scale: "));
    }
    CHECK_ACTOR_ERROR(actorWith(R"("default_scale": 1e-50)"), ErrorCode::InvalidData,
                      "default_scale: must remain positive when stored as a float");
}

// --- Actor instances ---

SCHUIMM_TEST(Actor_IdentityIsImmutable)
{
    static_assert(!std::is_copy_constructible_v<ActorInstance>);
    static_assert(!std::is_copy_assignable_v<ActorInstance>);
    static_assert(!std::is_move_assignable_v<ActorInstance>);
    static_assert(std::is_same_v<decltype(std::declval<const ActorInstance&>().handle()), script::ObjectRef>);
    static_assert(std::is_same_v<decltype(std::declval<ActorInstance&>().id()), const std::string&>);
    static_assert(std::is_same_v<decltype(std::declval<ActorInstance&>().resource()), const ActorResource&>);
}

SCHUIMM_TEST(Actor_CreatedFromTheResourceDefaults)
{
    const auto resource = heroResource();
    const ActorInstance actor = makeActor("hero", resource);
    CHECK(actor.id() == "hero");
    CHECK(!actor.handle().isNull());
    CHECK(&actor.resource() == resource.get());
    CHECK(actor.position() == (Vec2{100, 200}));
    CHECK(actor.facing() == Facing::South);
    CHECK(actor.visible());
    CHECK(actor.scale() == 1.0f);
    CHECK(actor.animation() == "idle");
    CHECK(clipName(actor) == "idle_south");
    CHECK(actor.playbackStatus() == PlaybackStatus::Playing);
}

SCHUIMM_TEST(Actor_CreationValidatesTheConfiguration)
{
    const auto resource = heroResource();
    ActorConfiguration configuration = defaultActorConfiguration(*resource, {0, 0});
    configuration.animation = "look_up";
    auto failed = ActorInstance::create("guard", resource, configuration);
    REQUIRE(!failed.has_value());
    CHECK(failed.error().code == ErrorCode::NotFound);
    CHECK(contains(failed.error().message, "actor 'guard': actor 'hero' animation 'look_up' has no clip for facing 'south'"));
    configuration.facing = Facing::North;
    CHECK(ActorInstance::create("guard", resource, configuration).has_value());

    configuration = defaultActorConfiguration(*resource, {0, 0});
    configuration.scale = 0.0f;
    CHECK(!ActorInstance::create("guard", resource, configuration).has_value());
    configuration = defaultActorConfiguration(*resource, {std::nanf(""), 0});
    CHECK(!ActorInstance::create("guard", resource, configuration).has_value());
    CHECK(!ActorInstance::create("guard", nullptr, configuration).has_value());
}

SCHUIMM_TEST(Actor_InstancesSharingAResourceAreIndependent)
{
    const auto resource = heroResource();
    ActorInstance a = makeActor("guard_01", resource, {10, 10});
    ActorInstance b = makeActor("guard_02", resource, {20, 20});
    CHECK(a.handle() != b.handle());
    CHECK(&a.resource() == &b.resource());
    REQUIRE(a.setFacing(Facing::West).has_value());
    REQUIRE(a.setPosition({50, 60}).has_value());
    a.setVisible(false);
    REQUIRE(a.setScale(2.0f).has_value());
    a.update(15);
    b.update(3);
    CHECK(b.facing() == Facing::South);
    CHECK(b.position() == (Vec2{20, 20}));
    CHECK(b.visible());
    CHECK(b.scale() == 1.0f);
    CHECK(clipName(a) == "idle_west");
    CHECK(clipName(b) == "idle_south");
    CHECK_EQ(a.player().elapsedTicks(), std::uint64_t{15});
    CHECK_EQ(b.player().elapsedTicks(), std::uint64_t{3});
}

SCHUIMM_TEST(Actor_PlayingTheCurrentAnimationDoesNotRestart)
{
    ActorInstance actor = makeActor("hero", heroResource());
    actor.update(7);
    REQUIRE(actor.playAnimation("idle").has_value());
    CHECK_EQ(actor.player().elapsedTicks(), std::uint64_t{7});
    REQUIRE(actor.playAnimation("wave").has_value());
    CHECK(clipName(actor) == "wave_south");
    CHECK_EQ(actor.player().elapsedTicks(), std::uint64_t{0});
    CHECK(actor.update(12).finished);
    CHECK(actor.playbackStatus() == PlaybackStatus::Finished);
    CHECK_EQ(actor.player().frameIndex(), std::size_t{1}); // Stays on its last frame.
    CHECK(!actor.update(12).finished);
    // A finished clip requested again starts over.
    REQUIRE(actor.playAnimation("wave").has_value());
    CHECK(actor.playbackStatus() == PlaybackStatus::Playing);
    actor.restartAnimation();
    CHECK_EQ(actor.player().elapsedTicks(), std::uint64_t{0});
}

SCHUIMM_TEST(Actor_FacingChanges)
{
    ActorInstance actor = makeActor("hero", heroResource());
    actor.update(5);
    // A different clip starts from its beginning.
    REQUIRE(actor.setFacing(Facing::West).has_value());
    CHECK(clipName(actor) == "idle_west");
    CHECK(!actor.flipX());
    CHECK_EQ(actor.player().elapsedTicks(), std::uint64_t{0});
    actor.update(13);
    // The same clip, mirrored: playback continues.
    REQUIRE(actor.setFacing(Facing::East).has_value());
    CHECK(clipName(actor) == "idle_west");
    CHECK(actor.flipX());
    CHECK_EQ(actor.player().elapsedTicks(), std::uint64_t{13});
    // Pausing survives a facing change that keeps the clip.
    actor.pauseAnimation();
    REQUIRE(actor.setFacing(Facing::West).has_value());
    CHECK(actor.playbackStatus() == PlaybackStatus::Paused);
    actor.resumeAnimation();
    // A direction-independent animation keeps playing across every facing.
    REQUIRE(actor.playAnimation("glow").has_value());
    actor.update(3);
    for (const Facing facing : allFacings) REQUIRE(actor.setFacing(facing).has_value());
    CHECK_EQ(actor.player().elapsedTicks(), std::uint64_t{3});
}

SCHUIMM_TEST(Actor_FailedChangesPreserveTheState)
{
    ActorInstance actor = makeActor("hero", heroResource());
    REQUIRE(actor.setFacing(Facing::East).has_value());
    actor.update(4);
    const auto before = [&] {
        return std::make_tuple(actor.facing(), actor.animation(), clipName(actor), actor.flipX(),
                               actor.player().elapsedTicks(), actor.playbackStatus());
    };
    const auto state = before();

    const auto unknown = actor.playAnimation("dance");
    REQUIRE(!unknown.has_value());
    CHECK(unknown.error().code == ErrorCode::NotFound);
    CHECK(actor.playAnimation("look_up").error().code == ErrorCode::NotFound); // No clip facing east.
    CHECK(before() == state);

    REQUIRE(actor.setFacing(Facing::North).has_value());
    REQUIRE(actor.playAnimation("look_up").has_value());
    const auto lookUp = before();
    const auto turn = actor.setFacing(Facing::South);
    REQUIRE(!turn.has_value());
    CHECK(contains(turn.error().message, "no clip for facing 'south'"));
    CHECK(before() == lookUp);

    const Vec2 position = actor.position();
    CHECK(actor.setPosition({std::numeric_limits<float>::infinity(), 0}).error().code == ErrorCode::InvalidArgument);
    CHECK(!actor.setPosition({0, 2e6f}).has_value());
    CHECK(actor.position() == position);
    CHECK(!actor.setScale(0.0f).has_value());
    CHECK(!actor.setScale(-1.0f).has_value());
    CHECK(!actor.setScale(std::nanf("")).has_value());
    CHECK(!actor.setScale(2000.0f).has_value());
    CHECK(actor.scale() == 1.0f);
}

SCHUIMM_TEST(Actor_StopPauseResume)
{
    ActorInstance actor = makeActor("hero", heroResource());
    actor.update(12);
    actor.pauseAnimation();
    actor.update(100);
    CHECK_EQ(actor.player().elapsedTicks(), std::uint64_t{12});
    actor.resumeAnimation();
    actor.stopAnimation();
    CHECK(actor.playbackStatus() == PlaybackStatus::Stopped);
    CHECK_EQ(actor.player().frameIndex(), std::size_t{0});
    actor.update(5);
    CHECK_EQ(actor.player().elapsedTicks(), std::uint64_t{0});
    // Requesting the stopped clip again starts it.
    REQUIRE(actor.playAnimation("idle").has_value());
    CHECK(actor.playbackStatus() == PlaybackStatus::Playing);
}

// --- Render state ---

SCHUIMM_TEST(Actor_RenderStatePlacesThePivotOnTheGroundPoint)
{
    ActorInstance actor = makeActor("hero", heroResource(), {100, 200});
    // Frame 0 of idle_south: source [0, 0, 32, 64], pivot [16, 60].
    ActorRenderState state = actor.renderState();
    CHECK(state.image == AssetPath::parse("Actors/Hero/hero.png").value());
    CHECK(state.source == (RectI{0, 0, 32, 64}));
    CHECK(state.destination == (RectF{84, 140, 32, 64}));
    CHECK(!state.flipX);

    // Frame 1 is larger, with another pivot: the ground point does not move.
    actor.update(10);
    state = actor.renderState();
    CHECK(state.source == (RectI{32, 0, 40, 72}));
    CHECK(state.destination == (RectF{80, 130, 40, 72}));
    CHECK(state.destination.x + state.pivot.x == 100.0f);
    CHECK(state.destination.y + state.pivot.y == 200.0f);
}

SCHUIMM_TEST(Actor_RenderStateMirroringAndScale)
{
    ActorInstance actor = makeActor("hero", heroResource(), {100, 200});
    REQUIRE(actor.setFacing(Facing::West).has_value());
    // idle_west frame 0: source [104, 0, 30, 64], pivot [10, 60].
    CHECK(actor.renderState().destination == (RectF{90, 140, 30, 64}));
    REQUIRE(actor.setFacing(Facing::East).has_value());
    // Mirrored: the pivot is 30 - 10 = 20 pixels from the left edge.
    ActorRenderState state = actor.renderState();
    CHECK(state.flipX);
    CHECK(state.destination == (RectF{80, 140, 30, 64}));
    // Scaled about the ground point.
    REQUIRE(actor.setScale(2.0f).has_value());
    state = actor.renderState();
    CHECK(state.destination == (RectF{60, 80, 60, 128}));
    CHECK(state.destination.x + (30.0f - state.pivot.x) * state.scale == 100.0f);
    CHECK(state.destination.y + state.destination.height - (64.0f - state.pivot.y) * state.scale == 200.0f);
}

SCHUIMM_TEST(Actor_RenderingDoesNotAdvancePlayback)
{
    ActorInstance actor = makeActor("hero", heroResource());
    actor.update(3);
    const ActorRenderState first = actor.renderState();
    for (int i = 0; i < 10; ++i) {
        const ActorRenderState again = actor.renderState();
        CHECK(again.source == first.source);
        CHECK(again.destination == first.destination);
    }
    CHECK_EQ(actor.player().elapsedTicks(), std::uint64_t{3});
}

// --- Rooms with actors ---

namespace {

const char* const heroWithScript = R"({
  "version": 1, "id": "hero", "script": "Actors/Hero/hero.schb",
  "animation_resources": { "main": "Actors/Hero/hero.anim.json" },
  "animations": {
    "idle": { "south": { "resource": "main", "clip": "idle_south" },
              "west": { "resource": "main", "clip": "idle_west" },
              "east": { "resource": "main", "clip": "idle_west", "flip_x": true }, "fallback": "south" },
    "wave": { "south": { "resource": "main", "clip": "wave_south" } },
    "glow": { "any": { "resource": "main", "clip": "glow" } } },
  "default_animation": "idle", "default_facing": "south", "default_scale": 0.5 })";

const char* const hallRoom = R"({
  "version": 2, "id": "hall", "size": { "width": 800, "height": 600 },
  "objects": [ { "id": "door", "position": [700, 300] } ],
  "actors": [
    { "id": "hero", "actor": "Actors/Hero/hero.actor.json", "position": [400, 500] },
    { "id": "guard_01", "actor": "Actors/Hero/hero.actor.json", "position": [200, 450], "facing": "east",
      "animation": "idle", "scale": 0.85 },
    { "id": "guard_02", "actor": "Actors/Hero/hero.actor.json", "position": [600, 450], "facing": "west",
      "visible": false },
    { "id": "door", "actor": "Actors/Hero/hero.actor.json", "position": [700, 300], "animation": "glow" } ] })";

const char* const gardenRoom = R"({
  "version": 2, "id": "garden", "size": { "width": 800, "height": 600 },
  "actors": [ { "id": "hero", "actor": "Actors/Hero/hero.actor.json", "position": [120, 550], "facing": "west" } ] })";

struct ActorRoomFixture {
    std::string root = test::makeScratchDirectory("actors");
    Logger logger{LogLevel::Error};
    script::ScriptHost host{logger};

    ActorRoomFixture()
    {
        writeBytes("Actors/Hero/hero.png", test::pngHeader(256, 128));
        writeBytes("Actors/Hero/glow.png", test::pngHeader(16, 16));
        writeText("Actors/Hero/hero.anim.json", heroAnimation);
        writeText("Actors/Hero/hero.schb", "placeholder: actor scripts are referenced, not loaded");
        writeText("Actors/Hero/hero.actor.json", heroWithScript);
        writeText("Rooms/Hall.room.json", hallRoom);
        writeText("Rooms/Garden.room.json", gardenRoom);
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

AssetPath asset(std::string_view text)
{
    return AssetPath::parse(text).value();
}

std::unique_ptr<RoomInstance> instantiate(RoomLibrary& library, script::ScriptHost& host, std::string_view room)
{
    auto loaded = library.load(asset(room));
    if (!loaded) std::fprintf(stderr, "%s\n", loaded.error().message.c_str());
    REQUIRE(loaded.has_value());
    auto instance = RoomInstance::create(*loaded, host);
    if (!instance) std::fprintf(stderr, "%s\n", instance.error().message.c_str());
    REQUIRE(instance.has_value());
    return std::move(*instance);
}

Result<LoadedRoom> loadRoomText(ActorRoomFixture& fixture, RoomLibrary& library, std::string_view text)
{
    fixture.writeText("Rooms/Test.room.json", text);
    library.unloadAll();
    return library.load(asset("Rooms/Test.room.json"));
}

std::vector<std::byte> compileRoomScript(const std::string& source)
{
    script::test::Compiled compiled = script::test::compileSource(source, true, "Hall.sch");
    if (!compiled.result.success) std::fprintf(stderr, "%s", compiled.diagnostics().c_str());
    REQUIRE(compiled.result.success);
    auto bytes = script::writeBytecode(compiled.result.module);
    REQUIRE(bytes.has_value());
    return std::move(*bytes);
}

} // namespace

SCHUIMM_TEST(RoomActors_RoomsWithoutActorsAreUnchanged)
{
    const auto version1 = decodeRoomResource(
        bytesOf(R"({ "version": 1, "id": "empty", "size": { "width": 10, "height": 10 } })"), "Empty.room.json");
    REQUIRE(version1.has_value());
    CHECK(version1->actors.empty());
    // Actors are a version 2 property.
    const auto actorsInVersion1 = decodeRoomResource(
        bytesOf(R"({ "version": 1, "id": "empty", "size": { "width": 10, "height": 10 }, "actors": [] })"),
        "Empty.room.json");
    REQUIRE(!actorsInVersion1.has_value());
    CHECK(contains(actorsInVersion1.error().message, "Empty.room.json: actors: unknown property"));

    ActorRoomFixture fixture;
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator, fixture.host);
    fixture.writeText("Rooms/Empty.room.json", R"({ "version": 2, "id": "empty", "size": { "width": 10, "height": 10 } })");
    const auto instance = instantiate(library, fixture.host, "Rooms/Empty.room.json");
    CHECK(instance->actors().empty());
    CHECK(instance->drawList().empty());
    instance->update(1.0);
}

SCHUIMM_TEST(RoomActors_PlacementsAndDefaults)
{
    ActorRoomFixture fixture;
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator, fixture.host);
    const auto loaded = library.load(asset("Rooms/Hall.room.json"));
    REQUIRE(loaded.has_value());
    const auto& placements = loaded->resource->actors;
    REQUIRE(placements.size() == 4);
    CHECK(placements[1].facing == Facing::East);
    CHECK(placements[1].scale == 0.85f);
    CHECK(placements[2].visible == false);
    CHECK(!placements[0].facing.has_value());
    REQUIRE(loaded->actors.size() == 4);
    // One shared actor resource for all four placements.
    CHECK(loaded->actors[0] == loaded->actors[3]);
    CHECK(loaded->actors[0]->script == asset("Actors/Hero/hero.schb"));
    CHECK_EQ(library.actors().actorCount(), std::size_t{1});
    CHECK_EQ(library.actors().animationCount(), std::size_t{1});
    CHECK_EQ(library.actors().imageCount(), std::size_t{2});

    const auto room = RoomInstance::create(*loaded, fixture.host).value();
    REQUIRE(room->actors().size() == 4);
    const ActorInstance& hero = room->actors()[0];
    CHECK(hero.id() == "hero");
    CHECK(hero.facing() == Facing::South);   // Resource default.
    CHECK(hero.scale() == 0.5f);              // Resource default.
    CHECK(hero.animation() == "idle");        // Resource default.
    CHECK(hero.visible());                    // Runtime default.
    const ActorInstance& guard = room->actors()[1];
    CHECK(guard.facing() == Facing::East);
    CHECK(guard.flipX());
    CHECK(guard.scale() == 0.85f);
    CHECK(!room->actors()[2].visible());
    CHECK(room->actors()[3].animation() == "glow");
}

SCHUIMM_TEST(RoomActors_InvalidPlacements)
{
    ActorRoomFixture fixture;
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator, fixture.host);
    const auto errorOf = [&](std::string_view actors) {
        const std::string text =
            concat(R"({ "version": 2, "id": "test", "size": { "width": 100, "height": 100 }, "actors": [ )", actors, " ] }");
        const auto result = loadRoomText(fixture, library, text);
        CHECK(!result.has_value());
        return result.has_value() ? std::string("<valid>") : result.error().message;
    };
    const std::string hero = R"("actor": "Actors/Hero/hero.actor.json", "position": [1, 2])";
    CHECK(contains(errorOf(concat(R"({ "id": "a", )", hero, R"( }, { "id": "a", )", hero, " }")),
                   "Rooms/Test.room.json: actors[1].id: duplicate identifier 'a' (first used by actors[0])"));
    CHECK(contains(errorOf(concat(R"({ "id": "1a", )", hero, " }")), "actors[0].id: invalid identifier '1a'"));
    CHECK(contains(errorOf(R"({ "id": "a", "position": [1, 2] })"), "actors[0].actor: missing required property"));
    CHECK(contains(errorOf(R"({ "id": "a", "actor": "Actors/Hero/hero.actor.json" })"),
                   "actors[0].position: missing required property"));
    CHECK(contains(errorOf(R"({ "id": "a", "actor": "Actors/Hero/hero.actor.json", "position": [1e9, 2] })"),
                   "actors[0].position[0]: coordinate 1000000000.0 is out of range"));
    CHECK(contains(errorOf(concat(R"({ "id": "a", )", hero, R"(, "facing": "up" })")), "actors[0].facing: unknown facing 'up'"));
    CHECK(contains(errorOf(concat(R"({ "id": "a", )", hero, R"(, "scale": 0 })")), "actors[0].scale: must be a positive number"));
    CHECK(contains(errorOf(concat(R"({ "id": "a", )", hero, R"(, "scale": 1e-50 })")),
                   "actors[0].scale: must remain positive when stored as a float"));
    CHECK(contains(errorOf(concat(R"({ "id": "a", )", hero, R"(, "visible": "no" })")),
                   "actors[0].visible: expected true or false, found a string"));
    CHECK(contains(errorOf(concat(R"({ "id": "a", )", hero, R"(, "colour": "red" })")), "actors[0].colour: unknown property"));
    // Asset references.
    const std::string missing = errorOf(R"({ "id": "a", "actor": "Actors/Nobody/nobody.actor.json", "position": [1, 2] })");
    CHECK(contains(missing, "Rooms/Test.room.json: actors[0].actor: asset 'Actors/Nobody/nobody.actor.json'"));
    // Resolution against the actor resource.
    CHECK(contains(errorOf(concat(R"({ "id": "a", )", hero, R"(, "animation": "dance" })")),
                   "Rooms/Test.room.json: actors[0] ('a'): actor 'hero' has no animation 'dance'"));
    CHECK(contains(errorOf(concat(R"({ "id": "a", )", hero, R"(, "animation": "wave", "facing": "north" })")),
                   "actors[0] ('a'): actor 'hero' animation 'wave' has no clip for facing 'north' and no fallback"));
    // Actors may start outside the room.
    CHECK(loadRoomText(fixture, library,
                       concat(R"({ "version": 2, "id": "test", "size": { "width": 100, "height": 100 }, "actors": [ { "id": "a", "actor": "Actors/Hero/hero.actor.json", "position": [-500, 9000] } ] })"))
              .has_value());
}

SCHUIMM_TEST(RoomActors_BrokenActorResourcesFailTheRoom)
{
    ActorRoomFixture fixture;
    fixture.writeText("Actors/Hero/hero.actor.json", R"({ "version": 1, "id": "hero" })");
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator, fixture.host);
    const auto result = library.load(asset("Rooms/Garden.room.json"));
    REQUIRE(!result.has_value());
    CHECK(contains(result.error().message,
                   "Rooms/Garden.room.json: actors[0] ('hero'): Actors/Hero/hero.actor.json: animation_resources: missing required property"));
    // Its script missing, or an image missing.
    fixture.writeText("Actors/Hero/hero.actor.json", heroWithScript);
    REQUIRE(fs::remove(path::join(fixture.root, "Actors/Hero/hero.schb")).has_value());
    const auto noScript = library.load(asset("Rooms/Garden.room.json"));
    REQUIRE(!noScript.has_value());
    CHECK(noScript.error().code == ErrorCode::NotFound);
    CHECK(contains(noScript.error().message, "Actors/Hero/hero.actor.json: script: "));
    fixture.writeText("Actors/Hero/hero.schb", "x");
    REQUIRE(fs::remove(path::join(fixture.root, "Actors/Hero/glow.png")).has_value());
    const auto noImage = library.load(asset("Rooms/Garden.room.json"));
    REQUIRE(!noImage.has_value());
    CHECK(contains(noImage.error().message,
                   "Actors/Hero/hero.anim.json: clips.glow.frames[0].image: "));
}

SCHUIMM_TEST(RoomActors_LookupAndHandles)
{
    ActorRoomFixture fixture;
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator, fixture.host);
    const auto room = instantiate(library, fixture.host, "Rooms/Hall.room.json");

    ActorInstance* guard = room->actor("guard_01");
    REQUIRE(guard != nullptr);
    CHECK(room->actor(guard->handle()) == guard);
    CHECK(room->actor("nobody") == nullptr);
    CHECK(room->actor(script::ObjectRef{}) == nullptr);

    // "door" is both an object and an actor: separate kinds, unambiguous lookups.
    const RoomObject* doorObject = room->object("door");
    const ActorInstance* doorActor = room->actor("door");
    REQUIRE(doorObject != nullptr);
    REQUIRE(doorActor != nullptr);
    CHECK(doorObject->handle() != doorActor->handle());
    CHECK(room->object(doorActor->handle()) == nullptr);
    CHECK(room->actor(doorObject->handle()) == nullptr);

    // Handles are unique across all objects and actors.
    std::set<std::uint64_t> handles;
    for (const RoomObject& object : room->objects()) handles.insert(object.handle().id);
    for (const ActorInstance& actor : room->actors()) handles.insert(actor.handle().id);
    CHECK_EQ(handles.size(), room->objects().size() + room->actors().size());
    CHECK(handles.count(0) == 0);
}

SCHUIMM_TEST(RoomActors_RecreatingARoomStartsFromItsPlacements)
{
    ActorRoomFixture fixture;
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator, fixture.host);

    auto hall = instantiate(library, fixture.host, "Rooms/Hall.room.json");
    ActorInstance* hero = hall->actor("hero");
    REQUIRE(hero != nullptr);
    const script::ObjectRef oldHero = hero->handle();
    const script::ObjectRef oldDoor = hall->object("door")->handle();
    const auto sharedActor = hero->sharedResource();
    // Change everything a visit can change.
    REQUIRE(hero->setPosition({1, 1}).has_value());
    REQUIRE(hero->setFacing(Facing::West).has_value());
    REQUIRE(hero->setScale(3.0f).has_value());
    hero->setVisible(false);
    REQUIRE(hero->playAnimation("glow").has_value());
    hall->update(0.5);

    // Leave: the room (and its actors) is destroyed before the next one is created.
    std::weak_ptr<const RoomResource> hallResource = hall->sharedResource();
    hall.reset();
    auto garden = instantiate(library, fixture.host, "Rooms/Garden.room.json");
    ActorInstance* gardenHero = garden->actor("hero");
    REQUIRE(gardenHero != nullptr);
    // The same textual id and resource in another room: another instance.
    CHECK(gardenHero->handle() != oldHero);
    CHECK(gardenHero->sharedResource() == sharedActor);
    CHECK(gardenHero->position() == (Vec2{120, 550}));
    CHECK(gardenHero->facing() == Facing::West);
    CHECK(garden->actor(oldHero) == nullptr);
    garden.reset();

    // Back to the hall: fresh actors from the authored placements, nothing retained.
    hall = instantiate(library, fixture.host, "Rooms/Hall.room.json");
    hero = hall->actor("hero");
    REQUIRE(hero != nullptr);
    CHECK(hero->handle() != oldHero);
    CHECK(hall->actor(oldHero) == nullptr);
    CHECK(hall->object(oldDoor) == nullptr);
    CHECK(hero->position() == (Vec2{400, 500}));
    CHECK(hero->facing() == Facing::South);
    CHECK(hero->scale() == 0.5f);
    CHECK(hero->visible());
    CHECK(hero->animation() == "idle");
    CHECK_EQ(hero->player().elapsedTicks(), std::uint64_t{0});
    // The immutable resources were reused, not reloaded.
    CHECK(hero->sharedResource() == sharedActor);
    CHECK(hall->sharedResource() == hallResource.lock());
    CHECK_EQ(library.actors().actorCount(), std::size_t{1});
}

SCHUIMM_TEST(RoomActors_DestroyingARoomReleasesItsActors)
{
    ActorRoomFixture fixture;
    const AssetLocator locator = fixture.locator();
    std::weak_ptr<const ActorResource> actorResource;
    {
        RoomLibrary library(locator, fixture.host);
        auto hall = instantiate(library, fixture.host, "Rooms/Hall.room.json");
        actorResource = hall->actors()[0].sharedResource();
        library.unloadAll();
        CHECK(!actorResource.expired()); // The room's actors keep it alive.
        hall.reset();
        CHECK(actorResource.expired()); // Released with the actors.
    }
}

SCHUIMM_TEST(RoomActors_FailedActorCreationPublishesNoRoom)
{
    ActorRoomFixture fixture;
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator, fixture.host);
    auto loaded = library.load(asset("Rooms/Hall.room.json")).value();

    LoadedRoom missingActors = loaded;
    missingActors.actors.pop_back();
    const auto mismatch = RoomInstance::create(missingActors, fixture.host);
    REQUIRE(!mismatch.has_value());
    CHECK(contains(mismatch.error().message, "room 'hall': 4 actor placements but 3 actor resources"));

    RoomResource broken = *loaded.resource;
    broken.actors[2].animation = "dance";
    LoadedRoom brokenRoom = loaded;
    brokenRoom.resource = std::make_shared<const RoomResource>(broken);
    const auto failed = RoomInstance::create(brokenRoom, fixture.host);
    REQUIRE(!failed.has_value());
    CHECK(contains(failed.error().message, "room 'hall': actors[2] ('guard_02'): actor 'hero' has no animation 'dance'"));

    LoadedRoom nullActor = loaded;
    nullActor.actors[1] = nullptr;
    CHECK(!RoomInstance::create(nullActor, fixture.host).has_value());
}

SCHUIMM_TEST(RoomActors_ActorsDoNotSatisfyObjectLinking)
{
    ActorRoomFixture fixture;
    // The script declares "hero", which the room has only as an actor.
    fixture.writeBytes("Rooms/Hall.schb", compileRoomScript("extern func say(text: string)\n"
                                                             "object hero:\n"
                                                             "\tfunc look_at():\n"
                                                             "\t\tsay(\"me\")\n"));
    fixture.writeText("Rooms/Garden.room.json", R"({
      "version": 2, "id": "garden", "size": { "width": 800, "height": 600 }, "script": "Rooms/Hall.schb",
      "actors": [ { "id": "hero", "actor": "Actors/Hero/hero.actor.json", "position": [120, 550] } ] })");
    REQUIRE(fixture.host.natives()
                .add({"say", {script::ScriptType::String}, script::ScriptType::None,
                      [](script::NativeContext&, std::span<const script::Value>) { return script::NativeResult::returnValue(); },
                      {}})
                .has_value());
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator, fixture.host);
    const auto result = library.load(asset("Rooms/Garden.room.json"));
    REQUIRE(!result.has_value());
    CHECK(contains(result.error().message, "declares object 'hero', which room 'garden' does not define"));
}

SCHUIMM_TEST(RoomActors_UpdateAdvancesEveryActorByTheSameTicks)
{
    ActorRoomFixture fixture;
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator, fixture.host);
    const auto room = instantiate(library, fixture.host, "Rooms/Hall.room.json");
    for (int i = 0; i < 7; ++i) room->update(1.0 / 60.0);
    // guard_02 is invisible but animates all the same.
    for (const ActorInstance& actor : room->actors()) {
        CHECK_EQ(actor.player().elapsedTicks(), actor.animation() == "glow" ? std::uint64_t{2} : std::uint64_t{7});
    }
    // Fractions of a tick carry over between updates.
    room->update(0.5 / 60.0);
    CHECK_EQ(room->actors()[0].player().elapsedTicks(), std::uint64_t{7});
    room->update(0.6 / 60.0);
    CHECK_EQ(room->actors()[0].player().elapsedTicks(), std::uint64_t{8});
}

SCHUIMM_TEST(RoomActors_DrawListIsSortedAndSkipsInvisibleActors)
{
    ActorRoomFixture fixture;
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator, fixture.host);
    const auto room = instantiate(library, fixture.host, "Rooms/Hall.room.json");
    // y: hero 500, guard_01 450, guard_02 450 (invisible), door 300.
    RoomDrawList list = room->drawList();
    REQUIRE(list.size() == 3);
    CHECK_EQ(list[0].sequence, std::size_t{3});
    CHECK_EQ(list[1].sequence, std::size_t{1});
    CHECK_EQ(list[2].sequence, std::size_t{0});
    CHECK(list[2].sprite.position == (Vec2{400, 500}));

    // Equal depths keep placement order.
    room->actors()[2].setVisible(true);
    list = room->drawList();
    REQUIRE(list.size() == 4);
    CHECK_EQ(list[1].sequence, std::size_t{1});
    CHECK_EQ(list[2].sequence, std::size_t{2});

    // Moving an actor changes the order; drawing twice changes nothing.
    REQUIRE(room->actors()[3].setPosition({700, 599}).has_value());
    const RoomDrawList first = room->drawList();
    const RoomDrawList second = room->drawList();
    CHECK_EQ(first.back().sequence, std::size_t{3});
    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK_EQ(first[i].sequence, second[i].sequence);
        CHECK(first[i].sprite.source == second[i].sprite.source);
        CHECK(first[i].sprite.destination == second[i].sprite.destination);
    }
    CHECK_EQ(room->actors()[0].player().elapsedTicks(), std::uint64_t{0});
}

// --- The ActorDemo's authored content ---

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

SCHUIMM_TEST(RoomActors_DemoRoomsAreValid)
{
    // A copy of the demo's assets, with a stand-in for the walker script that the build compiles.
    const std::string root = test::makeScratchDirectory("actordemo");
    copyTree(SCHUIMM_ACTOR_DEMO_ASSET_DIR, root);
    REQUIRE(fs::writeFile(path::join(root, "Actors/Walker/walker.schb"), bytesOf("stand-in")).has_value());

    Logger logger{LogLevel::Error};
    script::ScriptHost host{logger};
    const AssetLocator locator = AssetLocator::create(root).value();
    RoomLibrary library(locator, host);
    auto hall = instantiate(library, host, "Rooms/Hall.room.json");
    CHECK_EQ(hall->actors().size(), std::size_t{5});
    const script::ObjectRef hallHero = hall->actor("hero")->handle();
    hall.reset();
    auto garden = instantiate(library, host, "Rooms/Garden.room.json");
    CHECK_EQ(garden->actors().size(), std::size_t{3});
    // The same actor resource and instance id in both rooms; separate instances.
    CHECK(garden->actor("hero")->handle() != hallHero);
    CHECK(garden->actor("hero")->resource().id == "walker");
    CHECK(!garden->actor("lamp")->visible());
    CHECK_EQ(library.actors().actorCount(), std::size_t{2});
    CHECK_EQ(library.actors().animationCount(), std::size_t{2});
    CHECK_EQ(library.actors().imageCount(), std::size_t{3});
    // Every directional combination the demo uses resolves.
    ActorInstance& hero = *garden->actor("hero");
    for (const Facing facing : allFacings) CHECK(hero.setFacing(facing).has_value());
    CHECK(hero.playAnimation("wave").has_value());
}
