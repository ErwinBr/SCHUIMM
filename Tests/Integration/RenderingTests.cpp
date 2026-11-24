// Rendering integration tests: real window, OpenGL context and renderer. Each test draws into
// a tiny virtual resolution (16x9), captures the presented frame and checks pixel values, so
// texture upload, premultiplied blending, sampling, batching and render targets are verified
// on the actual driver (Mesa llvmpipe on Linux CI, the local GPU elsewhere).

#include "Core/Common/StringBuilder.h"
#include "Core/Logging/Logger.h"
#include "Core/Logging/StandardSinks.h"
#include "Graphics/OpenGL/OpenGLGraphicsDevice.h"
#include "Platform/NativePlatform.h"
#include "TestFramework.h"

#include <array>
#include <cstdlib>
#include <functional>
#include <memory>

using namespace schuimm;

namespace {

constexpr Extent2D virtualSize{16, 9};
using Rgb = std::array<int, 3>;

struct TestLogger {
    TestLogger() { logger.addSink(std::make_unique<ConsoleSink>()); }
    Logger logger{LogLevel::Warning};
};

// Platform, window and device for one test. Torn down in reverse order of creation.
struct Context {
    TestLogger testLogger;
    std::unique_ptr<Platform> platform;
    std::unique_ptr<Window> window;
    std::unique_ptr<GraphicsDevice> device;

    bool create()
    {
        auto createdPlatform = createNativePlatform(testLogger.logger);
        if (!createdPlatform) {
            std::fprintf(stderr, "  platform: %s\n", describe(createdPlatform.error()).c_str());
            return false;
        }
        platform = std::move(*createdPlatform);
        WindowDescription description;
        description.title = "SCHUIMM RenderingTests";
        description.size = {320, 180};
        auto createdWindow = platform->createWindow(description);
        if (!createdWindow) {
            std::fprintf(stderr, "  window: %s\n", describe(createdWindow.error()).c_str());
            return false;
        }
        window = std::move(*createdWindow);
        auto surface = window->createOpenGLSurface(defaultOpenGLSurfaceDescription());
        if (!surface) {
            std::fprintf(stderr, "  surface: %s\n", describe(surface.error()).c_str());
            return false;
        }
        GraphicsDeviceSettings settings;
        settings.vsync = false;
        settings.presentation.virtualResolution = virtualSize;
        settings.presentation.scalingFilter = TextureFilter::Nearest;
        auto createdDevice = createOpenGLGraphicsDevice(std::move(*surface), settings, testLogger.logger);
        if (!createdDevice) {
            std::fprintf(stderr, "  device: %s\n", describe(createdDevice.error()).c_str());
            return false;
        }
        device = std::move(*createdDevice);
        return true;
    }

    ~Context()
    {
        device.reset();
        window.reset();
        platform.reset();
    }

    Renderer2D& renderer() { return device->renderer(); }

    // Renders one frame with `draw` and returns the presented frame.
    Image renderFrame(const std::function<void(Renderer2D&)>& draw)
    {
        std::vector<WindowEvent> events;
        platform->pollEvents();
        window->drainEvents(events);
        device->requestFrameCapture();
        device->beginFrame();
        draw(device->renderer());
        device->endFrame();
        device->present();
        return device->takeCapturedFrame().value_or(Image{});
    }
};

// Colour of virtual pixel (x, y) in a captured frame, sampled at the centre of its block.
Rgb scenePixel(const Image& frame, int x, int y)
{
    const PixelRect rect = computePresentationRect(virtualSize, frame.size(), false);
    const int px = rect.x + static_cast<int>((static_cast<float>(x) + 0.5f) * static_cast<float>(rect.width) / static_cast<float>(virtualSize.width));
    const int py = rect.y + static_cast<int>((static_cast<float>(y) + 0.5f) * static_cast<float>(rect.height) / static_cast<float>(virtualSize.height));
    const auto p = frame.pixel(px, py);
    return {p[0], p[1], p[2]};
}

void checkPixel(const Image& frame, int x, int y, Rgb expected, int tolerance = 2)
{
    const Rgb actual = scenePixel(frame, x, y);
    bool ok = true;
    for (std::size_t i = 0; i < 3; ++i) {
        ok = ok && std::abs(actual[i] - expected[i]) <= tolerance;
    }
    if (!ok) {
        SCHUIMM_TEST_FAIL_(concat("pixel (", x, ",", y, ") is (", actual[0], ",", actual[1], ",", actual[2], "), expected (",
                                  expected[0], ",", expected[1], ",", expected[2], ")"),
                           false);
    }
}

std::unique_ptr<Texture> makeTexture(Renderer2D& renderer, Extent2D size, std::vector<std::uint8_t> rgba,
                                     TextureSettings settings = {}, TextureUsage usage = TextureUsage::Static)
{
    TextureDescription description;
    description.size = size;
    description.pixels = rgba;
    description.settings = settings;
    description.usage = usage;
    auto texture = renderer.createTexture(description);
    if (!texture) {
        std::fprintf(stderr, "  texture: %s\n", describe(texture.error()).c_str());
        return nullptr;
    }
    return std::move(*texture);
}

std::unique_ptr<Texture> solid(Renderer2D& renderer, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    return makeTexture(renderer, {1, 1}, {r, g, b, 255});
}

} // namespace

SCHUIMM_TEST(Rendering_TextureUploadAndPremultipliedBlending)
{
    Context ctx;
    REQUIRE(ctx.create());
    // Straight alpha: an opaque red texel and a half-transparent white one.
    auto texture = makeTexture(ctx.renderer(), {2, 1}, {255, 0, 0, 255, 255, 255, 255, 128});
    REQUIRE(texture != nullptr);
    const Image frame = ctx.renderFrame([&](Renderer2D& r) {
        r.clear(Color::black());
        r.drawTexture(*texture, {0, 0, 2, 1});
        // A half-transparent tint over an opaque texture.
        r.drawTexture(*texture, {4, 0, 1, 1}, {0, 0, 0.5f, 1}, {1, 1, 1, 0.5f});
    });
    REQUIRE(!frame.isEmpty());
    checkPixel(frame, 0, 0, {255, 0, 0});
    checkPixel(frame, 1, 0, {128, 128, 128}); // white at alpha 0.5 over black
    checkPixel(frame, 4, 0, {128, 0, 0});
    checkPixel(frame, 2, 0, {0, 0, 0});
}

SCHUIMM_TEST(Rendering_NearestLinearAndWrapModes)
{
    Context ctx;
    REQUIRE(ctx.create());
    const std::vector<std::uint8_t> blackWhite{0, 0, 0, 255, 255, 255, 255, 255};
    auto nearest = makeTexture(ctx.renderer(), {2, 1}, blackWhite, {TextureFilter::Nearest});
    auto linear = makeTexture(ctx.renderer(), {2, 1}, blackWhite, {TextureFilter::Linear});
    auto repeat = makeTexture(ctx.renderer(), {2, 1}, blackWhite, {TextureFilter::Nearest, TextureWrap::Repeat, TextureWrap::Repeat});
    REQUIRE(nearest && linear && repeat);
    const Image frame = ctx.renderFrame([&](Renderer2D& r) {
        r.clear(Color::black());
        r.drawTexture(*nearest, {0, 0, 8, 1});
        r.drawTexture(*linear, {0, 1, 8, 1});
        // Source u from 0 to 2: the texture repeats (Repeat) or its edge is stretched (Clamp).
        r.drawTexture(*repeat, {0, 2, 8, 1}, {0, 0, 2, 1});
        r.drawTexture(*nearest, {0, 3, 8, 1}, {0, 0, 2, 1});
    });
    REQUIRE(!frame.isEmpty());
    checkPixel(frame, 3, 0, {0, 0, 0});
    checkPixel(frame, 4, 0, {255, 255, 255});
    // Linear filtering blends the two texels around the middle.
    const int left = scenePixel(frame, 3, 1)[0];
    const int right = scenePixel(frame, 4, 1)[0];
    CHECK(left > 40 && left < 128);
    CHECK(right > 128 && right < 215);
    for (int x = 0; x < 8; ++x) {
        const int expected = (x % 4) < 2 ? 0 : 255;
        checkPixel(frame, x, 2, {expected, expected, expected});
        const int clamped = x < 2 ? 0 : 255;
        checkPixel(frame, x, 3, {clamped, clamped, clamped});
    }
}

SCHUIMM_TEST(Rendering_SpriteFlipRotationAndView)
{
    Context ctx;
    REQUIRE(ctx.create());
    // Red on the left, green on the right.
    auto texture = makeTexture(ctx.renderer(), {2, 1}, {255, 0, 0, 255, 0, 255, 0, 255});
    REQUIRE(texture != nullptr);
    const Image frame = ctx.renderFrame([&](Renderer2D& r) {
        r.clear(Color::black());
        Sprite flipped;
        flipped.texture = texture.get();
        flipped.destination = {0, 0, 2, 1};
        flipped.flipX = true;
        r.drawSprite(flipped);

        // Half a turn around the centre mirrors both axes.
        Sprite rotated = flipped;
        rotated.flipX = false;
        rotated.destination = {0, 2, 2, 1};
        rotated.origin = {1, 0.5f};
        rotated.rotation = 3.14159265f;
        r.drawSprite(rotated);

        // A view transform moves world coordinates on the target.
        r.setView(Transform2D::translation({10, 4}) * Transform2D::scaling(2, 2));
        Sprite world;
        world.texture = texture.get();
        world.destination = {0, 0, 1, 1};
        world.source = RectF{0, 0, 1, 1};
        r.drawSprite(world);
        r.resetView();
    });
    REQUIRE(!frame.isEmpty());
    checkPixel(frame, 0, 0, {0, 255, 0});
    checkPixel(frame, 1, 0, {255, 0, 0});
    checkPixel(frame, 0, 2, {0, 255, 0});
    checkPixel(frame, 1, 2, {255, 0, 0});
    for (const auto& [x, y] : {std::pair{10, 4}, std::pair{11, 4}, std::pair{10, 5}, std::pair{11, 5}}) {
        checkPixel(frame, x, y, {255, 0, 0});
    }
    checkPixel(frame, 12, 4, {0, 0, 0});
}

SCHUIMM_TEST(Rendering_MultipleTexturesInOneBatchKeepOrder)
{
    Context ctx;
    REQUIRE(ctx.create());
    auto red = solid(ctx.renderer(), 255, 0, 0);
    auto green = solid(ctx.renderer(), 0, 255, 0);
    auto blue = solid(ctx.renderer(), 0, 0, 255);
    REQUIRE(red && green && blue);
    const Image frame = ctx.renderFrame([&](Renderer2D& r) {
        r.clear(Color::black());
        r.drawTexture(*red, {0, 0, 1, 1});
        r.drawTexture(*green, {1, 0, 1, 1});
        r.drawTexture(*blue, {2, 0, 1, 1});
        // Same layer: submission order. Green covers red.
        r.drawTexture(*red, {4, 0, 1, 1});
        r.drawTexture(*green, {4, 0, 1, 1});
        // Lower layer submitted later is still drawn first: red stays on top of blue.
        Sprite top;
        top.texture = red.get();
        top.destination = {6, 0, 1, 1};
        r.drawSprite(top);
        Sprite below = top;
        below.texture = blue.get();
        below.layer = -1;
        r.drawSprite(below);
        r.fillRect({8, 0, 1, 1}, {1, 1, 0, 1});
    });
    REQUIRE(!frame.isEmpty());
    checkPixel(frame, 0, 0, {255, 0, 0});
    checkPixel(frame, 1, 0, {0, 255, 0});
    checkPixel(frame, 2, 0, {0, 0, 255});
    checkPixel(frame, 4, 0, {0, 255, 0});
    checkPixel(frame, 6, 0, {255, 0, 0});
    checkPixel(frame, 8, 0, {255, 255, 0});
    const RenderStatistics stats = ctx.renderer().lastFrameStatistics();
    CHECK_EQ(stats.quads, 8u);
    // Three textures plus the white one fit into one batch when 4+ slots are available.
    if (ctx.renderer().textureSlots() >= 4) {
        CHECK_EQ(stats.drawCalls, 1u);
    }
}

SCHUIMM_TEST(Rendering_RenderTargetRenderAndSample)
{
    Context ctx;
    REQUIRE(ctx.create());
    auto created = ctx.renderer().createRenderTarget({{4, 4}, {TextureFilter::Nearest}});
    REQUIRE(created.has_value());
    RenderTarget& target = **created;
    CHECK(target.size() == (Extent2D{4, 4}));
    CHECK(target.texture().size() == (Extent2D{4, 4}));

    const Image frame = ctx.renderFrame([&](Renderer2D& r) {
        r.beginRenderTarget(target);
        CHECK(r.targetSize() == (Extent2D{4, 4}));
        r.clear({0, 0, 0, 0});
        r.fillRect({0, 0, 4, 2}, {1, 0, 0, 1});      // top half red
        r.fillRect({0, 2, 2, 2}, {0, 1, 0, 0.5f});   // bottom-left half-transparent green
        r.endRenderTarget();                          // bottom-right stays transparent
        CHECK(r.targetSize() == virtualSize);

        r.clear({0, 0, 1, 1});
        r.drawTexture(target.texture(), {0, 0, 4, 4});
    });
    REQUIRE(!frame.isEmpty());
    checkPixel(frame, 0, 0, {255, 0, 0}); // top of the target is at the top (orientation)
    checkPixel(frame, 3, 1, {255, 0, 0});
    checkPixel(frame, 1, 3, {0, 128, 127});  // premultiplied green over blue, no dark fringe
    checkPixel(frame, 3, 3, {0, 0, 255});    // transparent
    checkPixel(frame, 5, 0, {0, 0, 255});
}

SCHUIMM_TEST(Rendering_NestedRenderTargetsRestoreParentAndView)
{
    Context ctx;
    REQUIRE(ctx.create());
    auto outer = ctx.renderer().createRenderTarget({{4, 4}, {}});
    auto inner = ctx.renderer().createRenderTarget({{2, 2}, {}});
    REQUIRE(outer.has_value() && inner.has_value());

    const Image frame = ctx.renderFrame([&](Renderer2D& r) {
        r.clear(Color::black());
        r.setView(Transform2D::translation({8, 0}));
        r.beginRenderTarget(**outer);
        CHECK(approxEqual(r.view(), Transform2D::identity()));
        r.clear({1, 0, 0, 1});
        r.beginRenderTarget(**inner);
        r.clear({0, 1, 0, 1});
        r.endRenderTarget();
        r.drawTexture((*inner)->texture(), {0, 0, 2, 2});
        r.endRenderTarget();
        // The scene and its view (translation by 8) are back.
        r.drawTexture((*outer)->texture(), {0, 0, 4, 4});
        r.resetView();
    });
    REQUIRE(!frame.isEmpty());
    checkPixel(frame, 8, 0, {0, 255, 0});
    checkPixel(frame, 10, 0, {255, 0, 0});
    checkPixel(frame, 8, 3, {255, 0, 0});
    checkPixel(frame, 0, 0, {0, 0, 0});
    CHECK(ctx.renderer().lastFrameStatistics().renderTargetSwitches >= 4u);
}

SCHUIMM_TEST(Rendering_InvalidRenderTargetsAndTextures)
{
    Context ctx;
    REQUIRE(ctx.create());
    const int tooLarge = ctx.device->capabilities().maxTextureSize + 1;
    for (const Extent2D size : {Extent2D{0, 4}, Extent2D{4, -1}, Extent2D{tooLarge, 1}}) {
        auto target = ctx.renderer().createRenderTarget({size, {}});
        REQUIRE(!target.has_value());
        CHECK(target.error().code == ErrorCode::InvalidArgument);
    }
    TextureDescription description;
    description.size = {2, 2};
    const std::uint8_t bytes[4] = {};
    description.pixels = bytes; // too little data
    auto texture = ctx.renderer().createTexture(description);
    REQUIRE(!texture.has_value());
    CHECK(texture.error().code == ErrorCode::InvalidArgument);
    description.pixels = {};
    CHECK(!ctx.renderer().createTexture(description).has_value()); // static without data
}

SCHUIMM_TEST(Rendering_DynamicTextureUpdate)
{
    Context ctx;
    REQUIRE(ctx.create());
    auto texture = makeTexture(ctx.renderer(), {2, 2}, {}, {}, TextureUsage::Dynamic); // starts transparent
    REQUIRE(texture != nullptr);
    const std::uint8_t white[4] = {255, 255, 255, 255};
    REQUIRE(ctx.renderer().updateTexture(*texture, {1, 0, 1, 1}, white).has_value());
    CHECK(!ctx.renderer().updateTexture(*texture, {2, 0, 1, 1}, white).has_value()); // outside

    auto fixed = solid(ctx.renderer(), 1, 2, 3);
    REQUIRE(fixed != nullptr);
    const auto status = ctx.renderer().updateTexture(*fixed, {0, 0, 1, 1}, white);
    REQUIRE(!status.has_value());
    CHECK(status.error().code == ErrorCode::InvalidState);

    const Image frame = ctx.renderFrame([&](Renderer2D& r) {
        r.clear({0, 0, 1, 1});
        r.drawTexture(*texture, {0, 0, 2, 2});
    });
    REQUIRE(!frame.isEmpty());
    checkPixel(frame, 0, 0, {0, 0, 255});
    checkPixel(frame, 1, 0, {255, 255, 255});
    checkPixel(frame, 1, 1, {0, 0, 255});
}

SCHUIMM_TEST(Rendering_ResourceCountsReturnToZero)
{
    Context ctx;
    REQUIRE(ctx.create());
    CHECK_EQ(ctx.device->liveResources().textures, 0);
    CHECK_EQ(ctx.device->liveResources().renderTargets, 0);
    {
        auto a = solid(ctx.renderer(), 1, 1, 1);
        auto b = solid(ctx.renderer(), 2, 2, 2);
        auto target = ctx.renderer().createRenderTarget({{8, 8}, {}});
        REQUIRE(a && b && target.has_value());
        CHECK_EQ(ctx.device->liveResources().textures, 2);
        CHECK_EQ(ctx.device->liveResources().renderTargets, 1);
        a.reset();
        CHECK_EQ(ctx.device->liveResources().textures, 1);
    }
    CHECK_EQ(ctx.device->liveResources().textures, 0);
    CHECK_EQ(ctx.device->liveResources().renderTargets, 0);
}
