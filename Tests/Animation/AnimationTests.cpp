#include "TestFramework.h"

#include "Animation/AnimationClock.h"
#include "Animation/AnimationPlayer.h"
#include "Animation/AnimationResourceLoader.h"
#include "Core/Common/StringBuilder.h"
#include "Platform/FileSystem/FileSystem.h"
#include "Platform/FileSystem/Path.h"
#include "Resource/Image/ImageLoader.h"
#include "TestAssets.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <string_view>

using namespace schuimm;
using namespace schuimm::animation;

namespace {

std::span<const std::byte> bytesOf(std::string_view text)
{
    return std::as_bytes(std::span(text.data(), text.size()));
}

// Image sizes for the fake: everything under "Images/" exists.
Result<Extent2D> fakeImageSize(const AssetPath& image)
{
    static const std::map<std::string, Extent2D, std::less<>> sizes = {
        {"Images/hero.png", {256, 128}},
        {"Images/wave.png", {64, 64}},
        {"Images/single.png", {30, 40}},
    };
    const auto found = sizes.find(image.view());
    if (found == sizes.end()) return makeError(ErrorCode::NotFound, concat("asset '", image.view(), "' does not exist"));
    return found->second;
}

Result<AnimationResource> decode(std::string_view text, const ImageSizeFunction& sizes = fakeImageSize)
{
    return decodeAnimationResource(bytesOf(text), "Hero.anim.json", sizes);
}

constexpr std::string_view heroAnimation = R"({
  "version": 1,
  "clips": {
    "idle": { "loop": true, "frames": [
      { "image": "Images/hero.png", "source": [0, 0, 32, 64], "pivot": [16, 60], "duration": 10 },
      { "image": "Images/hero.png", "source": [32, 0, 40, 70], "pivot": [20, 66], "duration": 5 },
      { "image": "Images/hero.png", "source": [72, 0, 32, 64], "pivot": [16, 60], "duration": 15 } ] },
    "wave": { "loop": false, "frames": [
      { "image": "Images/wave.png", "duration": 4 },
      { "image": "Images/wave.png", "source": [0, 0, 32, 32], "duration": 6 } ] },
    "single": { "loop": true, "frames": [ { "image": "Images/single.png", "duration": 1 } ] }
  }
})";

std::shared_ptr<const AnimationResource> heroResource()
{
    auto resource = decode(heroAnimation);
    if (!resource) std::fprintf(stderr, "%s\n", resource.error().message.c_str());
    return std::make_shared<const AnimationResource>(std::move(resource).value());
}

AnimationClipRef clipOf(const std::shared_ptr<const AnimationResource>& resource, std::string_view name)
{
    return AnimationClipRef::find(resource, name).value();
}

bool contains(std::string_view text, std::string_view part)
{
    if (text.find(part) != std::string_view::npos) return true;
    std::fprintf(stderr, "    message: %.*s\n    expected to contain: %.*s\n", static_cast<int>(text.size()),
                 text.data(), static_cast<int>(part.size()), part.data());
    return false;
}

#define CHECK_ERROR(text, expected)                                                                \
    do {                                                                                           \
        const auto result_ = decode(text);                                                         \
        CHECK(!result_.has_value());                                                               \
        if (!result_.has_value()) {                                                                \
            CHECK(result_.error().code == ErrorCode::InvalidData);                                 \
            CHECK(contains(result_.error().message, expected));                                    \
        }                                                                                          \
    } while (false)

// A one-clip resource whose single frame is `frame` (a JSON object).
std::string withFrame(std::string_view frame, std::string_view loop = "true")
{
    return concat(R"({ "version": 1, "clips": { "c": { "loop": )", loop, R"(, "frames": [ )", frame, " ] } } }");
}

} // namespace

// --- Resource format ---

SCHUIMM_TEST(Animation_DecodesClipsFramesAndDefaults)
{
    const auto resource = heroResource();
    REQUIRE(resource->clips.size() == 3);
    // Sorted by name.
    CHECK(resource->clips[0].name == "idle");
    CHECK(resource->clips[1].name == "single");
    CHECK(resource->clips[2].name == "wave");
    CHECK(resource->findClip("wave") == std::optional<std::size_t>(2));
    CHECK(!resource->findClip("walk").has_value());

    const AnimationClip& idle = resource->clips[0];
    CHECK(idle.loop);
    CHECK_EQ(idle.frames.size(), std::size_t{3});
    CHECK_EQ(idle.totalDuration, std::uint64_t{30});
    CHECK(idle.frameStarts == (std::vector<std::uint64_t>{0, 10, 15}));
    CHECK(idle.frames[1].source == (RectI{32, 0, 40, 70}));
    CHECK(idle.frames[1].pivot == (Vec2{20, 66}));
    CHECK_EQ(idle.frames[1].duration, std::uint32_t{5});

    const AnimationClip& wave = resource->clips[2];
    CHECK(!wave.loop);
    // An omitted source is the whole image, an omitted pivot [0, 0].
    CHECK(wave.frames[0].source == (RectI{0, 0, 64, 64}));
    CHECK(wave.frames[0].pivot == (Vec2{0, 0}));
    CHECK(wave.frames[1].source == (RectI{0, 0, 32, 32}));
}

SCHUIMM_TEST(Animation_ImageSizesAreAskedOncePerImage)
{
    int calls = 0;
    const ImageSizeFunction counting = [&calls](const AssetPath& image) {
        ++calls;
        return fakeImageSize(image);
    };
    REQUIRE(decode(heroAnimation, counting).has_value());
    CHECK_EQ(calls, 3);
}

SCHUIMM_TEST(Animation_InvalidJsonVersionAndStructure)
{
    CHECK_ERROR("{ \"version\": 1, \"clips\": ", "Hero.anim.json: invalid JSON at line 1");
    CHECK_ERROR("[]", "an animation resource must be a JSON object");
    CHECK_ERROR(R"({ "clips": {} })", "version: missing required property");
    CHECK_ERROR(R"({ "version": 2, "clips": {} })",
                "version: unsupported animation resource version 2 (this engine reads version 1)");
    CHECK_ERROR(R"({ "version": 1 })", "clips: missing required property");
    CHECK_ERROR(R"({ "version": 1, "clips": [] })", "clips: expected an object, found an array");
    CHECK_ERROR(R"({ "version": 1, "clips": {} })", "clips: an animation resource needs at least one clip");
    CHECK_ERROR(R"({ "version": 1, "clips": {}, "fps": 12 })", "fps: unknown property");
    CHECK_ERROR(R"({ "version": 1, "clips": { "2fast": { "loop": true, "frames": [] } } })",
                "clips.2fast: invalid clip name '2fast'");
    // Duplicate clip names and duplicate frame properties.
    CHECK_ERROR(R"({ "version": 1, "clips": { "c": { "loop": true, "frames": [] }, "c": { "loop": true, "frames": [] } } })",
                "clips.c: duplicate property 'c'");
    CHECK_ERROR(withFrame(R"({ "image": "Images/single.png", "duration": 1, "duration": 2 })"),
                "clips.c.frames[0].duration: duplicate property 'duration'");
}

SCHUIMM_TEST(Animation_InvalidClips)
{
    CHECK_ERROR(R"({ "version": 1, "clips": { "c": { "frames": [] } } })", "clips.c.loop: missing required property");
    CHECK_ERROR(R"({ "version": 1, "clips": { "c": { "loop": 1, "frames": [] } } })",
                "clips.c.loop: expected true or false, found the number 1");
    CHECK_ERROR(R"({ "version": 1, "clips": { "c": { "loop": true, "frames": [] } } })",
                "clips.c.frames: a clip needs at least one frame");
    CHECK_ERROR(R"({ "version": 1, "clips": { "c": { "loop": true, "frames": [], "speed": 2 } } })",
                "clips.c.speed: unknown property");
    std::string many = R"({ "version": 1, "clips": { "c": { "loop": true, "frames": [ )";
    for (std::size_t i = 0; i <= maxFramesPerClip; ++i) {
        many += concat(i == 0 ? "" : ", ", R"({ "image": "Images/single.png", "duration": 1 })");
    }
    many += " ] } } }";
    CHECK_ERROR(many, "clips.c.frames: a clip has at most 1024 frames, found 1025");
}

SCHUIMM_TEST(Animation_InvalidFrames)
{
    CHECK_ERROR(withFrame(R"({ "duration": 1 })"), "clips.c.frames[0].image: missing required property");
    CHECK_ERROR(withFrame(R"({ "image": "Images/single.png" })"), "clips.c.frames[0].duration: missing required property");
    CHECK_ERROR(withFrame(R"({ "image": "Images/single.png", "duration": 0 })"),
                "clips.c.frames[0].duration: must be an integer from 1 to 2147483647, found 0");
    CHECK_ERROR(withFrame(R"({ "image": "Images/single.png", "duration": -3 })"), "found -3");
    CHECK_ERROR(withFrame(R"({ "image": "Images/single.png", "duration": 1.5 })"), "expected an integer");
    CHECK_ERROR(withFrame(R"({ "image": "Images/single.png", "duration": 2147483648 })"), "found 2147483648");
    CHECK_ERROR(withFrame(R"({ "image": "../x.png", "duration": 1 })"), "clips.c.frames[0].image: ");
    CHECK_ERROR(withFrame(R"({ "image": "Images/single.png", "duration": 1, "flip": true })"),
                "clips.c.frames[0].flip: unknown property");
    CHECK_ERROR(withFrame(R"({ "image": "Images/single.png", "pivot": [1], "duration": 1 })"),
                "clips.c.frames[0].pivot: expected a point [x, y]");
    CHECK_ERROR(withFrame(R"({ "image": "Images/single.png", "pivot": [1, 1e300], "duration": 1 })"),
                "clips.c.frames[0].pivot[1]: coordinate 1e+300 is out of range");

    // The total duration of a clip is limited (and computed without overflow).
    CHECK_ERROR(withFrame(R"({ "image": "Images/single.png", "duration": 2147483647 },
                             { "image": "Images/single.png", "duration": 2147483647 },
                             { "image": "Images/single.png", "duration": 2 })"),
                "clips.c.frames[2].duration: the clip's total duration exceeds 4294967295 ticks");
    // Just at the limit is fine.
    CHECK(decode(withFrame(R"({ "image": "Images/single.png", "duration": 2147483647 },
                              { "image": "Images/single.png", "duration": 2147483647 },
                              { "image": "Images/single.png", "duration": 1 })"))
              .has_value());
}

SCHUIMM_TEST(Animation_PivotsOutsideTheFrameAreValid)
{
    const auto resource = decode(withFrame(R"({ "image": "Images/single.png", "pivot": [-10, 500], "duration": 1 })"));
    REQUIRE(resource.has_value());
    CHECK(resource->clips[0].frames[0].pivot == (Vec2{-10, 500}));
}

SCHUIMM_TEST(Animation_InvalidSourceRectangles)
{
    // single.png is 30 x 40.
    CHECK_ERROR(withFrame(R"({ "image": "Images/single.png", "source": [0, 0, 30], "duration": 1 })"),
                "clips.c.frames[0].source: expected a rectangle [x, y, width, height]");
    CHECK_ERROR(withFrame(R"({ "image": "Images/single.png", "source": [-1, 0, 4, 4], "duration": 1 })"),
                "clips.c.frames[0].source[0]: must be an integer from 0 to 16384, found -1");
    CHECK_ERROR(withFrame(R"({ "image": "Images/single.png", "source": [0, 0, 0, 4], "duration": 1 })"),
                "clips.c.frames[0].source[2]: must be an integer from 1 to 16384, found 0");
    CHECK_ERROR(withFrame(R"({ "image": "Images/single.png", "source": [0, 0, 4, 2.5], "duration": 1 })"),
                "clips.c.frames[0].source[3]: expected an integer");
    CHECK_ERROR(withFrame(R"({ "image": "Images/single.png", "source": [20, 0, 11, 40], "duration": 1 })"),
                "clips.c.frames[0].source: rectangle [20, 0, 11, 40] is not inside the 30x40 image");
    CHECK_ERROR(withFrame(R"({ "image": "Images/single.png", "source": [0, 1, 30, 40], "duration": 1 })"),
                "is not inside the 30x40 image");
    CHECK(decode(withFrame(R"({ "image": "Images/single.png", "source": [29, 39, 1, 1], "duration": 1 })")).has_value());
}

SCHUIMM_TEST(Animation_MissingImagesKeepTheirErrorCode)
{
    const auto result = decode(withFrame(R"({ "image": "Images/missing.png", "duration": 1 })"));
    REQUIRE(!result.has_value());
    CHECK(result.error().code == ErrorCode::NotFound);
    CHECK(contains(result.error().message, "Hero.anim.json: clips.c.frames[0].image: asset 'Images/missing.png'"));
}

SCHUIMM_TEST(Animation_LoadsFromAssetsUsingImageHeaders)
{
    const std::string root = test::makeScratchDirectory("animation");
    const auto png = test::testAssetLocator().readBytes(test::assetPath("Images/rgba_4x2.png"));
    REQUIRE(png.has_value());
    REQUIRE(fs::createDirectories(path::join(root, "Anim")).has_value());
    REQUIRE(fs::writeFile(path::join(root, "Anim/sheet.png"), *png).has_value());
    const std::string text = R"({ "version": 1, "clips": { "spin": { "loop": true, "frames": [
        { "image": "Anim/sheet.png", "source": [0, 0, 2, 2], "duration": 3 },
        { "image": "Anim/sheet.png", "duration": 3 } ] } } })";
    REQUIRE(fs::writeFile(path::join(root, "Anim/spin.anim.json"), bytesOf(text)).has_value());
    REQUIRE(fs::writeFile(path::join(root, "Anim/bad.anim.json"),
                          bytesOf(R"({ "version": 1, "clips": { "x": { "loop": true, "frames": [
        { "image": "Anim/sheet.png", "source": [3, 0, 2, 2], "duration": 3 } ] } } })"))
                .has_value());

    const AssetLocator locator = AssetLocator::create(root).value();
    ResourceCache<ImageInfo> images(locator, loadImageInfo);
    ResourceCache<AnimationResource> animations(
        locator, [&images](const AssetLocator& l, const AssetPath& p) { return loadAnimationResource(l, p, images); });

    const auto spin = animations.load(ResourceId<AnimationResource>(test::assetPath("Anim/spin.anim.json")));
    REQUIRE(spin.has_value());
    CHECK((*spin)->clips[0].frames[1].source == (RectI{0, 0, 4, 2}));
    CHECK_EQ(images.size(), std::size_t{1});

    const auto bad = animations.load(ResourceId<AnimationResource>(test::assetPath("Anim/bad.anim.json")));
    REQUIRE(!bad.has_value());
    CHECK(contains(bad.error().message, "Anim/bad.anim.json: clips.x.frames[0].source: rectangle [3, 0, 2, 2] is not inside the 4x2 image"));
    // A failed load is not cached; the good one is reused.
    CHECK_EQ(animations.size(), std::size_t{1});
}

// --- Playback ---

SCHUIMM_TEST(AnimationPlayer_StartsStoppedWithoutClip)
{
    AnimationPlayer player;
    CHECK(!player.hasClip());
    CHECK(player.status() == PlaybackStatus::Stopped);
    CHECK(!player.advance(100).finished);
    player.restart();
    CHECK(player.status() == PlaybackStatus::Stopped);
}

SCHUIMM_TEST(AnimationPlayer_SingleFrameClip)
{
    const auto resource = heroResource();
    AnimationPlayer player;
    CHECK(player.play(clipOf(resource, "single")));
    for (int i = 0; i < 5; ++i) {
        CHECK(!player.advance(1).finished);
        CHECK_EQ(player.frameIndex(), std::size_t{0});
        CHECK_EQ(player.elapsedTicks(), std::uint64_t{0});
    }
}

SCHUIMM_TEST(AnimationPlayer_FrameBoundariesAndPartialFrames)
{
    const auto resource = heroResource();
    AnimationPlayer player;
    player.play(clipOf(resource, "idle")); // Frames of 10, 5 and 15 ticks.
    CHECK_EQ(player.frameIndex(), std::size_t{0});
    player.advance(9);
    CHECK_EQ(player.frameIndex(), std::size_t{0}); // Partial frame.
    player.advance(1);
    CHECK_EQ(player.frameIndex(), std::size_t{1}); // Exactly on the boundary.
    player.advance(4);
    CHECK_EQ(player.frameIndex(), std::size_t{1});
    player.advance(1);
    CHECK_EQ(player.frameIndex(), std::size_t{2});
    CHECK(player.currentFrame().source == (RectI{72, 0, 32, 64}));
}

SCHUIMM_TEST(AnimationPlayer_AdvancesAcrossSeveralFramesAndLoops)
{
    const auto resource = heroResource();
    AnimationPlayer player;
    player.play(clipOf(resource, "idle")); // 30 ticks per loop.
    player.advance(16);
    CHECK_EQ(player.frameIndex(), std::size_t{2}); // Skipped frame 1 entirely.
    player.advance(14);
    CHECK_EQ(player.elapsedTicks(), std::uint64_t{0}); // Wrapped to the start.
    CHECK_EQ(player.frameIndex(), std::size_t{0});
    player.advance(30 * 7 + 12); // Seven whole loops and a bit, in one update.
    CHECK_EQ(player.elapsedTicks(), std::uint64_t{12});
    CHECK_EQ(player.frameIndex(), std::size_t{1});
    CHECK(player.status() == PlaybackStatus::Playing);
}

SCHUIMM_TEST(AnimationPlayer_VeryLargeUpdates)
{
    const auto resource = heroResource();
    AnimationPlayer looping;
    looping.play(clipOf(resource, "idle"));
    looping.advance(std::uint64_t{1} << 40);
    CHECK_EQ(looping.elapsedTicks(), (std::uint64_t{1} << 40) % 30);
    looping.advance(std::numeric_limits<std::uint64_t>::max());
    CHECK(looping.elapsedTicks() < 30);

    AnimationPlayer once;
    once.play(clipOf(resource, "wave"));
    CHECK(once.advance(std::numeric_limits<std::uint64_t>::max()).finished);
    CHECK(once.status() == PlaybackStatus::Finished);
    CHECK_EQ(once.frameIndex(), std::size_t{1});
}

SCHUIMM_TEST(AnimationPlayer_NonLoopingCompletesOnceAndHoldsTheLastFrame)
{
    const auto resource = heroResource();
    AnimationPlayer player;
    player.play(clipOf(resource, "wave")); // 4 + 6 ticks.
    CHECK(!player.advance(9).finished);
    CHECK(player.status() == PlaybackStatus::Playing);
    CHECK_EQ(player.frameIndex(), std::size_t{1});
    CHECK(player.advance(1).finished);
    CHECK(player.status() == PlaybackStatus::Finished);
    CHECK_EQ(player.frameIndex(), std::size_t{1});
    CHECK_EQ(player.elapsedTicks(), std::uint64_t{10});
    // No further notifications.
    for (int i = 0; i < 3; ++i) CHECK(!player.advance(100).finished);
    CHECK_EQ(player.frameIndex(), std::size_t{1});
    CHECK(player.status() == PlaybackStatus::Finished);
}

SCHUIMM_TEST(AnimationPlayer_PauseAndResume)
{
    const auto resource = heroResource();
    AnimationPlayer player;
    player.play(clipOf(resource, "idle"));
    player.advance(12);
    player.pause();
    CHECK(player.status() == PlaybackStatus::Paused);
    player.advance(100);
    CHECK_EQ(player.elapsedTicks(), std::uint64_t{12});
    player.resume();
    CHECK(player.status() == PlaybackStatus::Playing);
    player.advance(1);
    CHECK_EQ(player.elapsedTicks(), std::uint64_t{13});
    // Pausing a stopped player does nothing.
    player.stop();
    player.pause();
    CHECK(player.status() == PlaybackStatus::Stopped);
}

SCHUIMM_TEST(AnimationPlayer_StopAndRestart)
{
    const auto resource = heroResource();
    AnimationPlayer player;
    player.play(clipOf(resource, "idle"));
    player.advance(17);
    player.stop();
    CHECK(player.status() == PlaybackStatus::Stopped);
    CHECK_EQ(player.frameIndex(), std::size_t{0});
    player.advance(5);
    CHECK_EQ(player.elapsedTicks(), std::uint64_t{0});
    player.restart();
    CHECK(player.status() == PlaybackStatus::Playing);
    player.advance(5);
    CHECK_EQ(player.elapsedTicks(), std::uint64_t{5});
    player.restart(); // Restarting a playing clip goes back to the start.
    CHECK_EQ(player.elapsedTicks(), std::uint64_t{0});
}

SCHUIMM_TEST(AnimationPlayer_SelectingTheActiveClipDoesNotRestartIt)
{
    const auto resource = heroResource();
    AnimationPlayer player;
    CHECK(player.play(clipOf(resource, "idle")));
    player.advance(7);
    CHECK(!player.play(clipOf(resource, "idle")));
    CHECK_EQ(player.elapsedTicks(), std::uint64_t{7});
    player.pause();
    CHECK(!player.play(clipOf(resource, "idle")));
    CHECK(player.status() == PlaybackStatus::Paused); // Still paused.
    CHECK_EQ(player.elapsedTicks(), std::uint64_t{7});
}

SCHUIMM_TEST(AnimationPlayer_SelectingAStoppedOrFinishedClipStartsItAgain)
{
    const auto resource = heroResource();
    AnimationPlayer player;
    player.play(clipOf(resource, "wave"));
    player.advance(50);
    REQUIRE(player.status() == PlaybackStatus::Finished);
    CHECK(player.play(clipOf(resource, "wave")));
    CHECK(player.status() == PlaybackStatus::Playing);
    CHECK_EQ(player.elapsedTicks(), std::uint64_t{0});
    player.advance(3);
    player.stop();
    CHECK(player.play(clipOf(resource, "wave")));
    CHECK(player.status() == PlaybackStatus::Playing);
}

SCHUIMM_TEST(AnimationPlayer_SwitchingClips)
{
    const auto resource = heroResource();
    AnimationPlayer player;
    player.play(clipOf(resource, "idle"));
    player.advance(12);
    CHECK(player.play(clipOf(resource, "wave")));
    CHECK(player.clip() == clipOf(resource, "wave"));
    CHECK_EQ(player.elapsedTicks(), std::uint64_t{0});
    CHECK(player.play(clipOf(resource, "idle")));
    CHECK_EQ(player.frameIndex(), std::size_t{0});
    // An invalid reference clears the clip.
    CHECK(!player.play(AnimationClipRef()));
    CHECK(!player.hasClip());
    CHECK(player.status() == PlaybackStatus::Stopped);
}

SCHUIMM_TEST(AnimationPlayer_PlayersSharingAResourceAreIndependent)
{
    const auto resource = heroResource();
    AnimationPlayer a;
    AnimationPlayer b;
    a.play(clipOf(resource, "idle"));
    b.play(clipOf(resource, "idle"));
    a.advance(11);
    b.advance(3);
    b.pause();
    CHECK_EQ(a.frameIndex(), std::size_t{1});
    CHECK_EQ(b.frameIndex(), std::size_t{0});
    CHECK(a.status() == PlaybackStatus::Playing);
    CHECK(b.status() == PlaybackStatus::Paused);
}

SCHUIMM_TEST(AnimationPlayer_KeepsItsClipAliveAfterTheResourceIsReleased)
{
    AnimationPlayer player;
    std::weak_ptr<const AnimationResource> watch;
    {
        auto resource = heroResource();
        watch = resource;
        player.play(clipOf(resource, "idle"));
    } // The cache's (here: the test's) reference is gone.
    CHECK(!watch.expired());
    player.advance(11);
    CHECK(player.currentFrame().source == (RectI{32, 0, 40, 70}));
    player.play(AnimationClipRef());
    CHECK(watch.expired()); // Released with the last player.
}

SCHUIMM_TEST(AnimationPlayer_EquivalentSequencesGiveEquivalentState)
{
    const auto resource = heroResource();
    AnimationPlayer once;
    AnimationPlayer split;
    once.play(clipOf(resource, "idle"));
    split.play(clipOf(resource, "idle"));
    once.advance(77);
    for (const std::uint64_t ticks : {std::uint64_t{1}, std::uint64_t{29}, std::uint64_t{0}, std::uint64_t{40},
                                      std::uint64_t{7}}) {
        split.advance(ticks);
    }
    CHECK_EQ(once.elapsedTicks(), split.elapsedTicks());
    CHECK(once.status() == split.status());
}

SCHUIMM_TEST(AnimationPlayer_StatusNames)
{
    CHECK(std::string_view(playbackStatusName(PlaybackStatus::Finished)) == "finished");
    CHECK(std::string_view(playbackStatusName(PlaybackStatus::Paused)) == "paused");
}

// --- Clock ---

SCHUIMM_TEST(AnimationClock_SixtyFramesPerSecond)
{
    AnimationClock clock;
    std::uint64_t ticks = 0;
    for (int i = 0; i < 60; ++i) ticks += clock.advance(1.0 / 60.0);
    CHECK_EQ(ticks, std::uint64_t{60});
}

SCHUIMM_TEST(AnimationClock_FrameRateDoesNotChangeSpeed)
{
    for (const int rate : {30, 59, 60, 75, 120, 144, 165, 240, 1000}) {
        AnimationClock clock;
        std::uint64_t ticks = 0;
        for (int i = 0; i < rate * 10; ++i) ticks += clock.advance(1.0 / rate);
        // Ten seconds; per-update rounding to whole nanoseconds may shift the total by at most
        // one tick.
        if (ticks < 599 || ticks > 601) {
            SCHUIMM_TEST_FAIL_(concat(rate, " Hz gave ", ticks, " ticks in 10 s"), false);
        }
    }
}

SCHUIMM_TEST(AnimationClock_PreservesFractionalTicks)
{
    AnimationClock clock;
    // Half a tick each: truncating per update would never advance.
    CHECK_EQ(clock.advance(0.5 / 60.0), std::uint64_t{0});
    CHECK(clock.remainder() > 0);
    CHECK_EQ(clock.advance(0.6 / 60.0), std::uint64_t{1});
    std::uint64_t ticks = 0;
    for (int i = 0; i < 1000; ++i) ticks += clock.advance(0.001); // 1 ms updates.
    CHECK_EQ(ticks, std::uint64_t{60});
}

SCHUIMM_TEST(AnimationClock_SplitIntervalsGiveTheSameTotal)
{
    AnimationClock once;
    AnimationClock split;
    const std::uint64_t whole = once.advance(1.25);
    std::uint64_t parts = 0;
    for (const double seconds : {0.1, 0.4, 0.0, 0.35, 0.4}) parts += split.advance(seconds);
    CHECK_EQ(whole, std::uint64_t{75});
    CHECK_EQ(parts, whole);
}

SCHUIMM_TEST(AnimationClock_IgnoresInvalidAndClampsHugeDeltas)
{
    AnimationClock clock;
    CHECK_EQ(clock.advance(-1.0), std::uint64_t{0});
    CHECK_EQ(clock.advance(std::nan("")), std::uint64_t{0});
    CHECK_EQ(clock.advance(std::numeric_limits<double>::infinity()), std::uint64_t{0});
    CHECK_EQ(clock.remainder(), std::uint64_t{0});
    CHECK_EQ(clock.advance(1e300), static_cast<std::uint64_t>(AnimationClock::maxAdvanceSeconds) * 60);
}
