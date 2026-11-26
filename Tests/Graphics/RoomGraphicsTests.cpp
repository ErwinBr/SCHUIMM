// Drawing rooms: textures for a room's actors and sprite submission of its draw list, on the fake
// renderer (no GPU).

#include "FakeRenderer.h"
#include "TestAssets.h"
#include "TestFramework.h"

#include "Animation/AnimationResourceLoader.h"
#include "Core/Logging/Logger.h"
#include "Resource/Image/ImageLoader.h"
#include "Room/ActorResourceLoader.h"
#include "Room/RoomInstance.h"
#include "RoomGraphics/RoomRenderer.h"
#include "RoomGraphics/RoomTextures.h"
#include "Script/Runtime/ScriptHost.h"

#include <memory>
#include <string_view>

using namespace schuimm;
using namespace schuimm::room;
using namespace schuimm::test;

namespace {

std::span<const std::byte> bytesOf(std::string_view text)
{
    return std::as_bytes(std::span(text.data(), text.size()));
}

// rgba_4x2.png is 4 x 2 pixels; rgb_3x1.png 3 x 1.
constexpr std::string_view animationText = R"({ "version": 1, "clips": {
  "idle": { "loop": true, "frames": [
    { "image": "Images/rgba_4x2.png", "source": [0, 0, 2, 2], "pivot": [1, 2], "duration": 2 },
    { "image": "Images/rgba_4x2.png", "source": [2, 0, 2, 2], "pivot": [0, 2], "duration": 2 } ] },
  "glow": { "loop": true, "frames": [ { "image": "Images/rgb_3x1.png", "duration": 1 } ] } } })";

constexpr std::string_view actorText = R"({ "version": 1, "id": "blob",
  "animation_resources": { "main": "Anim/blob.anim.json" },
  "animations": {
    "idle": { "west": { "resource": "main", "clip": "idle" },
              "east": { "resource": "main", "clip": "idle", "flip_x": true }, "fallback": "west" },
    "glow": { "any": { "resource": "main", "clip": "glow" } } },
  "default_animation": "idle", "default_facing": "west" })";

struct Scene {
    Logger logger{LogLevel::Error};
    script::ScriptHost host{logger};
    LoadedRoom room;

    explicit Scene(std::string_view animation = animationText)
    {
        const AssetLocator locator = testAssetLocator();
        const animation::ImageSizeFunction sizes = [&locator](const AssetPath& image) -> Result<Extent2D> {
            auto info = loadImageInfo(locator, image);
            if (!info) return std::unexpected(info.error());
            return info->size;
        };
        auto decoded = animation::decodeAnimationResource(bytesOf(animation), "blob.anim.json", sizes);
        REQUIRE(decoded.has_value());
        const auto shared = std::make_shared<const animation::AnimationResource>(std::move(*decoded));
        const AnimationResolver resolver = [shared](const AssetPath&) -> Result<std::shared_ptr<const animation::AnimationResource>> {
            return shared;
        };
        const AssetCheck noScripts = [](const AssetPath&) -> Status { return {}; };
        auto actor = decodeActorResource(bytesOf(actorText), "blob.actor.json", resolver, noScripts);
        REQUIRE(actor.has_value());
        const auto actorResource = std::make_shared<const ActorResource>(std::move(*actor));

        RoomResource resource;
        resource.id = "test";
        resource.size = {100, 100};
        const AssetPath actorPath = AssetPath::parse("Actors/blob.actor.json").value();
        resource.actors.push_back(ActorPlacement{"front", actorPath, {10, 50}, std::nullopt, std::nullopt, 2.0f, std::nullopt, std::nullopt});
        resource.actors.push_back(ActorPlacement{"back", actorPath, {30, 20}, Facing::East, std::nullopt, std::nullopt, std::nullopt, std::nullopt});
        resource.actors.push_back(ActorPlacement{"hidden", actorPath, {50, 90}, std::nullopt, std::string("glow"), std::nullopt, false, std::nullopt});
        room = LoadedRoom{std::make_shared<const RoomResource>(std::move(resource)), nullptr,
                          {actorResource, actorResource, actorResource}, nullptr, {}, {}};
    }
};

} // namespace

SCHUIMM_TEST(RoomGraphics_PreparesOneTexturePerImage)
{
    Scene scene;
    FakeRenderer renderer;
    const AssetLocator locator = testAssetLocator();
    ResourceCache<Image> images(locator, loadImage);
    TextureCache textures(renderer, images);
    {
        auto prepared = RoomTextures::prepare(textures, scene.room);
        REQUIRE(prepared.has_value());
        CHECK_EQ(prepared->size(), std::size_t{2}); // Three actors, three clips, two images.
        CHECK_EQ(renderer.texturesCreated, 2);
        CHECK(prepared->find(AssetPath::parse("Images/rgba_4x2.png").value()) != nullptr);
        CHECK(prepared->find(AssetPath::parse("Images/other.png").value()) == nullptr);
        // Preparing again (another room using the same images) reuses the textures.
        auto again = RoomTextures::prepare(textures, scene.room);
        REQUIRE(again.has_value());
        CHECK_EQ(renderer.texturesCreated, 2);
        // The decoded images are not needed any more.
        images.unloadAll();
        textures.unloadAll();
        CHECK_EQ(renderer.liveTextures, 2); // Kept alive by the RoomTextures.
    }
    CHECK_EQ(renderer.liveTextures, 0);
}

SCHUIMM_TEST(RoomGraphics_RejectsFramesThatDoNotFitTheTexture)
{
    // The header said 8 x 2 (a stale size); the texture is 4 x 2.
    Scene scene;
    RoomResource resource = *scene.room.resource;
    auto clip = *scene.room.actors[0]->animationResources.at("main").resource;
    clip.clips[1].frames[0].source = RectI{2, 0, 4, 2};
    auto changedAnimation = std::make_shared<const animation::AnimationResource>(clip);
    ActorResource actor = *scene.room.actors[0];
    actor.animationResources.at("main").resource = changedAnimation;
    LoadedRoom room = scene.room;
    room.actors = {std::make_shared<const ActorResource>(actor)};
    room.resource = std::make_shared<const RoomResource>(RoomResource{resource.id, resource.size, {}, {}, {}, {}, {}, {},
                                                                      {}, {resource.actors[0]}});

    FakeRenderer renderer;
    const AssetLocator locator = testAssetLocator();
    ResourceCache<Image> images(locator, loadImage);
    TextureCache textures(renderer, images);
    const auto prepared = RoomTextures::prepare(textures, room);
    REQUIRE(!prepared.has_value());
    CHECK(prepared.error().code == ErrorCode::InvalidData);
    CHECK(prepared.error().message.find("clips.idle.frames[0]: source rectangle does not fit the 4x2 texture") !=
          std::string::npos);
}

SCHUIMM_TEST(RoomGraphics_SubmitsVisibleActorsBackToFront)
{
    Scene scene;
    FakeRenderer renderer;
    const AssetLocator locator = testAssetLocator();
    ResourceCache<Image> images(locator, loadImage);
    TextureCache textures(renderer, images);
    const auto prepared = RoomTextures::prepare(textures, scene.room).value();
    const auto instance = RoomInstance::create(scene.room, scene.host).value();

    REQUIRE(drawRoom(renderer, instance->drawList(), prepared, 5).has_value());
    // "hidden" is invisible; "back" (y 20) is drawn before "front" (y 50).
    REQUIRE(renderer.sprites.size() == 2);
    const Sprite& back = renderer.sprites[0];
    const Sprite& front = renderer.sprites[1];
    const Texture* sheet = prepared.find(AssetPath::parse("Images/rgba_4x2.png").value());
    CHECK(back.texture == sheet);
    CHECK(front.texture == sheet);
    CHECK_EQ(back.layer, 5);
    // back: facing east, idle mirrored; frame 0 source [0, 0, 2, 2], pivot [1, 2] -> mirrored 1.
    CHECK(back.flipX);
    CHECK(back.source == (RectF{0, 0, 2, 2}));
    CHECK(back.destination == (RectF{29, 18, 2, 2}));
    // front: scale 2, facing west; pivot [1, 2].
    CHECK(!front.flipX);
    CHECK(front.destination == (RectF{8, 46, 4, 4}));

    // Drawing again without updating gives the same sprites; updating changes frames.
    renderer.sprites.clear();
    REQUIRE(drawRoom(renderer, instance->drawList(), prepared).has_value());
    CHECK(renderer.sprites[1].source == (RectF{0, 0, 2, 2}));
    instance->update(2.0 / 60.0);
    renderer.sprites.clear();
    REQUIRE(drawRoom(renderer, instance->drawList(), prepared).has_value());
    // Frame 1: source [2, 0, 2, 2], pivot [0, 2]: the ground point (10, 50) stays put.
    CHECK(renderer.sprites[1].source == (RectF{2, 0, 2, 2}));
    CHECK(renderer.sprites[1].destination == (RectF{10, 46, 4, 4}));
    // The invisible actor animated too.
    CHECK_EQ(instance->actor("hidden")->player().elapsedTicks(), std::uint64_t{0}); // 1-tick loop: 2 % 1.
    CHECK_EQ(instance->actor("front")->player().elapsedTicks(), std::uint64_t{2});
}

SCHUIMM_TEST(RoomGraphics_MissingTexturesSubmitNothing)
{
    Scene scene;
    FakeRenderer renderer;
    const auto instance = RoomInstance::create(scene.room, scene.host).value();
    const RoomTextures empty;
    const auto status = drawRoom(renderer, instance->drawList(), empty);
    REQUIRE(!status.has_value());
    CHECK(status.error().code == ErrorCode::NotFound);
    CHECK(renderer.sprites.empty());
}

SCHUIMM_TEST(RoomGraphics_DrawsTheBackgroundFirstAndRoomArtwork)
{
    Scene scene;
    RoomResource resource = *scene.room.resource;
    const AssetPath background = AssetPath::parse("Images/rgb_3x1.png").value();
    const AssetPath sheet = AssetPath::parse("Images/rgba_4x2.png").value();
    resource.background = background;
    resource.props.push_back(RoomProp{"rock", sheet, {20, 60}, {2, 2}}); // In front of "front" (y 50).
    LoadedRoom room = scene.room;
    room.resource = std::make_shared<const RoomResource>(resource);
    room.imageSizes = {{background, {3, 1}}, {sheet, {4, 2}}};

    FakeRenderer renderer;
    const AssetLocator locator = testAssetLocator();
    ResourceCache<Image> images(locator, loadImage);
    TextureCache textures(renderer, images);
    const auto prepared = RoomTextures::prepare(textures, room).value();
    CHECK_EQ(prepared.size(), std::size_t{2}); // The prop shares the actors' sheet.
    const auto instance = RoomInstance::create(room, scene.host).value();
    REQUIRE(drawRoom(renderer, instance->drawList(), prepared).has_value());
    REQUIRE(renderer.sprites.size() == 4);
    CHECK(renderer.sprites[0].texture == prepared.find(background));
    CHECK(renderer.sprites[0].destination == (RectF{0, 0, 3, 1}));
    CHECK(renderer.sprites[3].destination == (RectF{18, 58, 4, 2})); // The rock, last (deepest y).

    // A header size that does not match the texture is caught when preparing.
    room.imageSizes[background] = {6, 1};
    const auto mismatch = RoomTextures::prepare(textures, room);
    REQUIRE(!mismatch.has_value());
    CHECK(mismatch.error().code == ErrorCode::InvalidData);
}

SCHUIMM_TEST(RoomGraphics_PreparesAndDrawsAnimatedObjects)
{
    // An animated room object (no actors): its frames' images are prepared with the room, and
    // it is drawn with its current frame, pivot on its position.
    Scene scene;
    const auto animation = scene.room.actors[0]->animationResources.at("main").resource;
    ObjectAnimationConfig config;
    config.resources.emplace("main", AssetPath::parse("Anim/blob.anim.json").value());
    config.animations.emplace("idle", ObjectClipMapping{"main", "idle"});
    config.defaultAnimation = "idle";
    const AnimationLoader load = [animation](const AssetPath&) -> Result<std::shared_ptr<const animation::AnimationResource>> {
        return animation;
    };
    auto set = resolveObjectAnimations(config, load);
    REQUIRE(set.has_value());
    RoomResource resource;
    resource.id = "objects";
    resource.size = {100, 100};
    resource.objects.push_back(RoomObjectDefinition{"fan", std::nullopt, config, {40, 60}, {}});
    const LoadedRoom room{std::make_shared<const RoomResource>(std::move(resource)), nullptr, {}, nullptr, {}, {*set}};

    FakeRenderer renderer;
    const AssetLocator locator = testAssetLocator();
    ResourceCache<Image> images(locator, loadImage);
    TextureCache textures(renderer, images);
    const auto prepared = RoomTextures::prepare(textures, room);
    REQUIRE(prepared.has_value());
    CHECK_EQ(prepared->size(), std::size_t{2}); // Every clip of the resource: rgba_4x2 and rgb_3x1.
    const auto instance = RoomInstance::create(room, scene.host).value();
    REQUIRE(drawRoom(renderer, instance->drawList(), *prepared).has_value());
    REQUIRE(renderer.sprites.size() == 1);
    CHECK(renderer.sprites[0].texture == prepared->find(AssetPath::parse("Images/rgba_4x2.png").value()));
    CHECK(renderer.sprites[0].source == (RectF{0, 0, 2, 2}));
    CHECK(renderer.sprites[0].destination == (RectF{39, 58, 2, 2})); // Pivot (1, 2) on (40, 60).
    instance->update(2.0 / 60.0);
    renderer.sprites.clear();
    REQUIRE(drawRoom(renderer, instance->drawList(), *prepared).has_value());
    CHECK(renderer.sprites[0].source == (RectF{2, 0, 2, 2}));
    CHECK(renderer.sprites[0].destination == (RectF{40, 58, 2, 2})); // Pivot (0, 2).
    CHECK_EQ(renderer.texturesCreated, 2); // Playback never creates textures.
}
