// CPU-side sprite batching: vertex generation, texture slots, batch splitting and ordering.
// No graphics context is involved.

#include "Graphics/Common/SpriteBatcher.h"
#include "TestFramework.h"

#include <numbers>
#include <vector>

using namespace schuimm;

namespace {

class TestTexture final : public Texture {
public:
    explicit TestTexture(Extent2D size = {64, 32})
        : size_(size)
    {
    }
    Extent2D size() const override { return size_; }
    PixelFormat format() const override { return PixelFormat::RGBA8; }
    TextureSettings settings() const override { return {}; }
    TextureUsage usage() const override { return TextureUsage::Static; }

private:
    Extent2D size_;
};

Sprite spriteAt(const Texture& texture, float x, int layer = 0)
{
    Sprite sprite;
    sprite.texture = &texture;
    sprite.destination = {x, 0, 10, 10};
    sprite.layer = layer;
    return sprite;
}

void checkVertex(const SpriteVertex& v, float x, float y, float u, float vv)
{
    CHECK_NEAR(v.x, x, 1e-4);
    CHECK_NEAR(v.y, y, 1e-4);
    CHECK_NEAR(v.u, u, 1e-6);
    CHECK_NEAR(v.v, vv, 1e-6);
}

// x of the first vertex of every quad, in draw order.
std::vector<float> drawOrder(const SpriteBatcher& batcher)
{
    std::vector<float> xs;
    for (std::size_t i = 0; i < batcher.vertices().size(); i += 4) {
        xs.push_back(batcher.vertices()[i].x);
    }
    return xs;
}

} // namespace

SCHUIMM_TEST(SpriteBatcher_GeneratesQuadVerticesAndTexCoords)
{
    TestTexture texture({64, 32});
    SpriteBatcher batcher;
    Sprite sprite;
    sprite.texture = &texture;
    sprite.destination = {10, 20, 30, 40};
    sprite.source = RectF{16, 8, 32, 16};
    batcher.add(sprite, texture);
    batcher.build();

    REQUIRE(batcher.vertices().size() == 4);
    const auto v = batcher.vertices();
    checkVertex(v[0], 10, 20, 0.25f, 0.25f); // top-left
    checkVertex(v[1], 40, 20, 0.75f, 0.25f); // top-right
    checkVertex(v[2], 40, 60, 0.75f, 0.75f); // bottom-right
    checkVertex(v[3], 10, 60, 0.25f, 0.75f); // bottom-left
    REQUIRE(batcher.batches().size() == 1);
    CHECK_EQ(batcher.batches()[0].quadCount, 1u);
    CHECK_EQ(batcher.batches()[0].textureCount, 1);
    CHECK(batcher.batches()[0].textures[0] == &texture);
}

SCHUIMM_TEST(SpriteBatcher_FlipsSwapTexCoords)
{
    TestTexture texture;
    SpriteBatcher batcher;
    Sprite sprite = spriteAt(texture, 0);
    sprite.flipX = true;
    sprite.flipY = true;
    batcher.add(sprite, texture);
    batcher.build();
    const auto v = batcher.vertices();
    checkVertex(v[0], 0, 0, 1, 1);
    checkVertex(v[2], 10, 10, 0, 0);
}

SCHUIMM_TEST(SpriteBatcher_RotatesAroundOrigin)
{
    TestTexture texture;
    SpriteBatcher batcher;
    Sprite sprite;
    sprite.texture = &texture;
    sprite.destination = {100, 100, 20, 10};
    sprite.origin = {10, 5}; // centre
    sprite.rotation = std::numbers::pi_v<float> / 2;
    batcher.add(sprite, texture);
    batcher.build();
    const auto v = batcher.vertices();
    // Centre (110, 105); a quarter turn clockwise maps the top-left corner (-10, -5) to (5, -10).
    CHECK_NEAR(v[0].x, 115, 1e-4);
    CHECK_NEAR(v[0].y, 95, 1e-4);
    CHECK_NEAR(v[2].x, 105, 1e-4);
    CHECK_NEAR(v[2].y, 115, 1e-4);
}

SCHUIMM_TEST(SpriteBatcher_AppliesViewTransform)
{
    TestTexture texture;
    SpriteBatcher batcher;
    batcher.setTransform(Transform2D::translation({5, 7}) * Transform2D::scaling(2, 2));
    batcher.add(spriteAt(texture, 1), texture);
    batcher.build();
    checkVertex(batcher.vertices()[0], 7, 7, 0, 0);
    checkVertex(batcher.vertices()[2], 27, 27, 1, 1);
}

SCHUIMM_TEST(SpriteBatcher_TintIsPremultiplied)
{
    TestTexture texture;
    SpriteBatcher batcher;
    Sprite sprite = spriteAt(texture, 0);
    sprite.tint = {1.0f, 0.5f, 0.0f, 0.5f};
    batcher.add(sprite, texture);
    batcher.build();
    const SpriteVertex& v = batcher.vertices()[0];
    CHECK_EQ(int{v.r}, 128);
    CHECK_EQ(int{v.g}, 64);
    CHECK_EQ(int{v.b}, 0);
    CHECK_EQ(int{v.a}, 128);
}

SCHUIMM_TEST(SpriteBatcher_AssignsTextureSlotsWithinOneBatch)
{
    TestTexture a, b, c;
    SpriteBatcher batcher;
    batcher.add(spriteAt(a, 0), a);
    batcher.add(spriteAt(b, 1), b);
    batcher.add(spriteAt(a, 2), a);
    batcher.add(spriteAt(c, 3), c);
    batcher.build();
    REQUIRE(batcher.batches().size() == 1);
    const SpriteBatch& batch = batcher.batches()[0];
    CHECK_EQ(batch.textureCount, 3);
    CHECK(batch.textures[0] == &a);
    CHECK(batch.textures[1] == &b);
    CHECK(batch.textures[2] == &c);
    const auto v = batcher.vertices();
    CHECK_EQ(int{v[0].slot}, 0);
    CHECK_EQ(int{v[4].slot}, 1);
    CHECK_EQ(int{v[8].slot}, 0);
    CHECK_EQ(int{v[12].slot}, 2);
    CHECK_EQ(int{v[15].slot}, 2); // every vertex of the quad
}

SCHUIMM_TEST(SpriteBatcher_SplitsWhenTextureSlotsRunOut)
{
    TestTexture a, b, c;
    for (const int slots : {1, 2, 16}) {
        SpriteBatcher batcher({slots, 4096, 65536});
        // a b c a: with 2 slots the third texture starts a new batch, which then holds c and a.
        batcher.add(spriteAt(a, 0), a);
        batcher.add(spriteAt(b, 1), b);
        batcher.add(spriteAt(c, 2), c);
        batcher.add(spriteAt(a, 3), a);
        batcher.build();
        const std::size_t expectedBatches = slots == 1 ? 4u : slots == 2 ? 2u : 1u;
        CHECK_EQ(batcher.batches().size(), expectedBatches);
        // Order is never changed to improve batching.
        CHECK(drawOrder(batcher) == (std::vector<float>{0, 1, 2, 3}));
        std::uint32_t total = 0;
        for (const SpriteBatch& batch : batcher.batches()) {
            CHECK(batch.textureCount <= slots);
            CHECK_EQ(batch.firstQuad, total);
            total += batch.quadCount;
        }
        CHECK_EQ(total, 4u);
    }
}

SCHUIMM_TEST(SpriteBatcher_SplitsAtBatchCapacity)
{
    TestTexture texture;
    SpriteBatcher batcher({16, 3, 65536});
    for (int i = 0; i < 8; ++i) {
        batcher.add(spriteAt(texture, static_cast<float>(i)), texture);
    }
    batcher.build();
    REQUIRE(batcher.batches().size() == 3);
    CHECK_EQ(batcher.batches()[0].quadCount, 3u);
    CHECK_EQ(batcher.batches()[1].quadCount, 3u);
    CHECK_EQ(batcher.batches()[2].quadCount, 2u);
    CHECK_EQ(batcher.batches()[2].firstQuad, 6u);
}

SCHUIMM_TEST(SpriteBatcher_ReportsFullAtPendingCapacity)
{
    TestTexture texture;
    SpriteBatcher batcher({16, 4096, 4});
    for (int i = 0; i < 3; ++i) {
        batcher.add(spriteAt(texture, 0), texture);
    }
    CHECK(!batcher.isFull());
    batcher.add(spriteAt(texture, 0), texture);
    CHECK(batcher.isFull());
    batcher.build();
    CHECK(!batcher.isFull());
    CHECK(batcher.isEmpty());
}

SCHUIMM_TEST(SpriteBatcher_SortsByLayerAndKeepsSubmissionOrderWithinLayer)
{
    TestTexture a, b;
    SpriteBatcher batcher;
    batcher.add(spriteAt(a, 0, 2), a);
    batcher.add(spriteAt(b, 1, 1), b);
    batcher.add(spriteAt(a, 2, 2), a);
    batcher.add(spriteAt(b, 3, 0), b);
    batcher.add(spriteAt(b, 4, 1), b);
    batcher.add(spriteAt(a, 5, 1), a);
    batcher.build();
    CHECK(drawOrder(batcher) == (std::vector<float>{3, 1, 4, 5, 0, 2}));
}

SCHUIMM_TEST(SpriteBatcher_ManyEqualLayersKeepOrder)
{
    TestTexture a, b;
    SpriteBatcher batcher;
    std::vector<float> expected;
    for (int i = 0; i < 1000; ++i) {
        const Texture& t = (i % 3 == 0) ? static_cast<const Texture&>(a) : b;
        batcher.add(spriteAt(t, static_cast<float>(i), i % 2), t);
    }
    for (int layer = 0; layer < 2; ++layer) {
        for (int i = layer; i < 1000; i += 2) {
            expected.push_back(static_cast<float>(i));
        }
    }
    batcher.build();
    CHECK(drawOrder(batcher) == expected);
}

SCHUIMM_TEST(SpriteBatcher_TriangleIsDegenerateQuad)
{
    TestTexture white({1, 1});
    SpriteBatcher batcher;
    batcher.addTriangle({{{{0, 0}, {1, 0, 0, 1}}, {{10, 0}, {0, 1, 0, 1}}, {{0, 10}, {0, 0, 1, 1}}}}, white, 0);
    batcher.build();
    const auto v = batcher.vertices();
    REQUIRE(v.size() == 4);
    CHECK_EQ(v[3].x, v[2].x);
    CHECK_EQ(v[3].y, v[2].y);
    CHECK_EQ(int{v[1].g}, 255);
}

SCHUIMM_TEST(SpriteBatcher_ReusesMemoryAfterWarmUp)
{
    TestTexture a, b;
    SpriteBatcher batcher;
    const auto frame = [&] {
        for (int i = 0; i < 3000; ++i) {
            const Texture& t = (i % 2 == 0) ? static_cast<const Texture&>(a) : b;
            batcher.add(spriteAt(t, static_cast<float>(i), 3 - i % 4), t);
        }
        batcher.build();
        batcher.clear();
    };
    frame();
    const std::size_t warm = batcher.capacityBytes();
    for (int i = 0; i < 5; ++i) {
        frame();
    }
    CHECK_EQ(batcher.capacityBytes(), warm);
}
