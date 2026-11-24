// Texture cache on top of the image cache, with a fake renderer (no GPU).

#include "FakeRenderer.h"
#include "GraphicsResources/TextureCache.h"
#include "Resource/Image/ImageLoader.h"
#include "TestAssets.h"
#include "TestFramework.h"

using namespace schuimm;
using namespace schuimm::test;

namespace {
ResourceId<Image> imageId(std::string_view path)
{
    return ResourceId<Image>::parse(path).value();
}

struct Fixture {
    AssetLocator locator = testAssetLocator();
    ResourceCache<Image> images{locator, loadImage};
    FakeRenderer renderer;
};
} // namespace

SCHUIMM_TEST(TextureCache_LoadsTextureWithImageDimensionsAndAlpha)
{
    Fixture f;
    TextureCache textures(f.renderer, f.images);
    auto texture = textures.load(imageId("Images/rgba_4x2.png"), {TextureFilter::Linear});
    REQUIRE(texture.has_value());
    const Texture& t = **texture;
    CHECK(t.size() == (Extent2D{4, 2}));
    CHECK(t.format() == PixelFormat::RGBA8);
    CHECK(t.settings().filter == TextureFilter::Linear);
    CHECK(t.usage() == TextureUsage::Static);
    // The uploaded data is the decoded image, alpha included (straight; the renderer premultiplies).
    const auto& fake = static_cast<const FakeTexture&>(t);
    const Image& image = *f.images.find(imageId("Images/rgba_4x2.png"));
    CHECK(fake.pixels == std::vector<std::uint8_t>(image.pixels().begin(), image.pixels().end()));
}

SCHUIMM_TEST(TextureCache_DeduplicatesPerImageAndSettings)
{
    Fixture f;
    TextureCache textures(f.renderer, f.images);
    const auto id = imageId("Images/rgba_4x2.png");
    auto first = textures.load(id);
    auto second = textures.load(imageId("Images/../Images/rgba_4x2.png"));
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(first->get() == second->get());
    CHECK_EQ(f.renderer.texturesCreated, 1);

    // Different sampling: a second texture, but the decoded image is reused.
    auto linear = textures.load(id, {TextureFilter::Linear, TextureWrap::Repeat, TextureWrap::Repeat});
    REQUIRE(linear.has_value());
    CHECK(linear->get() != first->get());
    CHECK_EQ(f.renderer.texturesCreated, 2);
    CHECK_EQ(f.images.size(), std::size_t{1});
    CHECK_EQ(textures.size(), std::size_t{2});
}

SCHUIMM_TEST(TextureCache_ReportsMissingAndInvalidImages)
{
    Fixture f;
    TextureCache textures(f.renderer, f.images);
    auto missing = textures.load(imageId("Images/does_not_exist.png"));
    REQUIRE(!missing.has_value());
    CHECK(missing.error().code == ErrorCode::NotFound);
    CHECK(missing.error().message.find("Images/does_not_exist.png") != std::string::npos);

    auto invalid = textures.load(imageId("Images/not_an_image.png"));
    REQUIRE(!invalid.has_value());
    CHECK(invalid.error().code == ErrorCode::InvalidData);
    CHECK_EQ(f.renderer.texturesCreated, 0);
    CHECK_EQ(textures.size(), std::size_t{0});
}

SCHUIMM_TEST(TextureCache_ReportsTextureCreationFailure)
{
    Fixture f;
    f.renderer.maxTextureSize = 2; // smaller than the 4x2 image
    TextureCache textures(f.renderer, f.images);
    auto texture = textures.load(imageId("Images/rgba_4x2.png"));
    REQUIRE(!texture.has_value());
    CHECK(texture.error().code == ErrorCode::InvalidArgument);
    CHECK(texture.error().message.find("rgba_4x2.png") != std::string::npos);
}

SCHUIMM_TEST(TextureCache_UnloadReleasesGpuTextures)
{
    Fixture f;
    {
        TextureCache textures(f.renderer, f.images);
        const auto id = imageId("Images/rgba_4x2.png");
        (void)textures.load(id);
        (void)textures.load(id, {TextureFilter::Linear});
        auto held = textures.load(imageId("Images/rgb_3x1.png"));
        CHECK_EQ(f.renderer.liveTextures, 3);

        CHECK_EQ(textures.unload(id), std::size_t{2});
        CHECK_EQ(f.renderer.liveTextures, 1);
        CHECK(!textures.isLoaded(id));

        // A holder keeps its texture alive after unloading.
        textures.unloadAll();
        CHECK_EQ(f.renderer.liveTextures, 1);
        held = makeError(ErrorCode::Unknown, "released");
        CHECK_EQ(f.renderer.liveTextures, 0);

        (void)textures.load(id);
        CHECK_EQ(f.renderer.liveTextures, 1);
    }
    // Destroying the cache releases the rest.
    CHECK_EQ(f.renderer.liveTextures, 0);
}
