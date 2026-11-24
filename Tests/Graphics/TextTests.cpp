// Text: shelf packing, layout with a fake glyph source, and Font (FreeType + atlas) on a fake
// renderer. Headless.

#include "FakeRenderer.h"
#include "Graphics/Common/ShelfPacker.h"
#include "Graphics/Common/TextLayout.h"
#include "GraphicsResources/Font.h"
#include "TestFramework.h"

#include <map>

using namespace schuimm;
using namespace schuimm::test;

namespace {

// Every glyph is 10 units wide (advance 10, image 8x12) except ' ' (no image) and 'W' (20).
// The pair "AV" kerns by -3.
class FakeGlyphs final : public GlyphSource {
public:
    FakeTexture atlas{TextureDescription{{64, 64}, PixelFormat::RGBA8, {}, 0, AlphaMode::Premultiplied, {}, TextureUsage::Dynamic},
                      liveTextures};
    int liveTextures = 0;
    std::map<char32_t, GlyphInfo> glyphs;

    const GlyphInfo& glyph(char32_t codepoint) override
    {
        auto it = glyphs.find(codepoint);
        if (it == glyphs.end()) {
            GlyphInfo info;
            info.glyphIndex = static_cast<std::uint32_t>(codepoint);
            info.advance = codepoint == U'W' ? 20.0f : 10.0f;
            if (codepoint != U' ') {
                info.texture = &atlas;
                info.source = {0, 0, 8, 12};
                info.offset = {1, -12};
            }
            it = glyphs.emplace(codepoint, info).first;
        }
        return it->second;
    }
    float kerning(const GlyphInfo& left, const GlyphInfo& right) override
    {
        return left.glyphIndex == U'A' && right.glyphIndex == U'V' ? -3.0f : 0.0f;
    }
    TextLineMetrics lineMetrics() const override { return {15.0f, 20.0f}; }
};

AssetLocator demoAssets()
{
    return AssetLocator::create(SCHUIMM_DEMO_ASSET_DIR).value();
}

std::shared_ptr<const FontFace> noto()
{
    auto face = loadFontFace(demoAssets(), AssetPath::parse("Fonts/NotoSans-Regular.ttf").value());
    return std::make_shared<const FontFace>(std::move(face).value());
}

bool overlaps(const TextureRegion& a, const TextureRegion& b)
{
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

} // namespace

SCHUIMM_TEST(ShelfPacker_PacksWithoutOverlapAndRespectsBounds)
{
    ShelfPacker packer({64, 64}, 1);
    std::vector<TextureRegion> placed;
    for (int i = 0; i < 40; ++i) {
        auto region = packer.insert(5 + i % 7, 6 + i % 5);
        if (!region) {
            break;
        }
        CHECK(region->x >= 1 && region->y >= 1);
        CHECK(region->x + region->width + 1 <= 64);
        CHECK(region->y + region->height + 1 <= 64);
        for (const TextureRegion& other : placed) {
            CHECK(!overlaps(*region, other));
        }
        placed.push_back(*region);
    }
    CHECK(placed.size() >= 30u);
    CHECK(!packer.insert(100, 4).has_value()); // wider than the area
    CHECK(!packer.insert(0, 4).has_value());
}

SCHUIMM_TEST(ShelfPacker_ReportsFullAndResets)
{
    ShelfPacker packer({16, 16}, 0);
    CHECK(packer.insert(16, 8).has_value());
    CHECK(packer.insert(16, 8).has_value());
    CHECK(!packer.insert(1, 1).has_value());
    packer.reset();
    CHECK(packer.insert(16, 16).has_value());
}

SCHUIMM_TEST(TextLayout_MeasuresLinesWithKerning)
{
    FakeGlyphs glyphs;
    CHECK_NEAR(measureLine(glyphs, "AB"), 20.0, 1e-6);
    CHECK_NEAR(measureLine(glyphs, "AV"), 17.0, 1e-6);
    CHECK_NEAR(measureLine(glyphs, "W W"), 50.0, 1e-6);
    const Vec2 block = measureText(glyphs, "AB\nABCD\n");
    CHECK_NEAR(block.x, 40.0, 1e-6);
    CHECK_NEAR(block.y, 60.0, 1e-6); // three lines (the last one empty)
    CHECK_NEAR(measureText(glyphs, "A", 1.5f).y, 30.0, 1e-6);
    CHECK(measureText(glyphs, "") == (Vec2{0, 0}));
    // Multi-byte UTF-8 counts as one glyph per code point.
    CHECK_NEAR(measureLine(glyphs, "\xC3\xA9\xC3\xA7"), 20.0, 1e-6);
}

SCHUIMM_TEST(TextLayout_DrawsGlyphSpritesOnBaselines)
{
    FakeGlyphs glyphs;
    FakeRenderer renderer;
    drawText(renderer, glyphs, "AV B\nC", {100, 50}, {Color{1, 0, 0, 0.5f}, TextAlign::Left, 7});
    // A, V, B, C: the space has no image.
    REQUIRE(renderer.sprites.size() == 4);
    const Sprite& a = renderer.sprites[0];
    CHECK(a.texture == &glyphs.atlas);
    CHECK(a.destination == (RectF{101, 53, 8, 12})); // baseline 50 + 15, offset (1, -12)
    CHECK(a.source == std::optional<RectF>(RectF{0, 0, 8, 12}));
    CHECK(a.tint == (Color{1, 0, 0, 0.5f}));
    CHECK_EQ(a.layer, 7);
    CHECK_NEAR(renderer.sprites[1].destination.x, 108.0, 1e-6); // 100 + 10 - 3 (kerning) + 1
    CHECK_NEAR(renderer.sprites[2].destination.x, 128.0, 1e-6); // after the space
    CHECK_NEAR(renderer.sprites[3].destination.x, 101.0, 1e-6); // next line starts at x again
    CHECK_NEAR(renderer.sprites[3].destination.y, 73.0, 1e-6);  // one line height lower
}

SCHUIMM_TEST(TextLayout_AlignsEachLine)
{
    FakeGlyphs glyphs;
    FakeRenderer renderer;
    drawText(renderer, glyphs, "AB\nABCD", {200, 0}, {Color::white(), TextAlign::Center});
    REQUIRE(renderer.sprites.size() == 6);
    CHECK_NEAR(renderer.sprites[0].destination.x, 191.0, 1e-6); // 200 - 20 / 2 + 1
    CHECK_NEAR(renderer.sprites[2].destination.x, 181.0, 1e-6); // 200 - 40 / 2 + 1
    renderer.sprites.clear();
    drawText(renderer, glyphs, "AB", {200, 0}, {Color::white(), TextAlign::Right});
    CHECK_NEAR(renderer.sprites[0].destination.x, 181.0, 1e-6);
}

SCHUIMM_TEST(Font_PacksGlyphsIntoAtlasPages)
{
    FakeRenderer renderer;
    auto font = Font::create(renderer, noto(), {24, TextureFilter::Linear, {512, 512}, 4, true});
    REQUIRE(font.has_value());
    Font& f = **font;
    CHECK_EQ(f.pageCount(), std::size_t{1});
    CHECK(f.cachedGlyphCount() >= 190u); // ASCII + Latin-1 preloaded
    CHECK(f.missingCodepoints().empty());
    const auto& page = static_cast<const FakeTexture&>(f.page(0));
    CHECK(page.usage() == TextureUsage::Dynamic);
    CHECK(page.settings().filter == TextureFilter::Linear);
    CHECK(page.updates > 150);

    const GlyphInfo& eAcute = f.glyph(U'é');
    REQUIRE(eAcute.texture == &f.page(0));
    CHECK(eAcute.source.width > 5 && eAcute.source.height > 10);
    CHECK(eAcute.offset.y < 0.0f); // above the baseline
    // The atlas holds the coverage as premultiplied white: some fully covered texel exists.
    bool sawOpaque = false;
    for (int y = 0; y < static_cast<int>(eAcute.source.height); ++y) {
        for (int x = 0; x < static_cast<int>(eAcute.source.width); ++x) {
            const std::size_t i = (static_cast<std::size_t>(eAcute.source.y + y) * 512 + static_cast<std::size_t>(eAcute.source.x + x)) * 4;
            CHECK_EQ(page.pixels[i], page.pixels[i + 3]);
            sawOpaque = sawOpaque || page.pixels[i + 3] == 255;
        }
    }
    CHECK(sawOpaque);
    // Cached: no new upload for a known glyph.
    const int updates = page.updates;
    (void)f.glyph(U'é');
    CHECK_EQ(page.updates, updates);
}

SCHUIMM_TEST(Font_GrowsByAddingPagesAndHandlesMissingGlyphs)
{
    FakeRenderer renderer;
    auto font = Font::create(renderer, noto(), {48, TextureFilter::Linear, {128, 128}, 3, false});
    REQUIRE(font.has_value());
    Font& f = **font;
    const GlyphInfo& first = f.glyph(U'A');
    const Texture* firstPage = first.texture;
    REQUIRE(firstPage != nullptr);
    const RectF firstSource = first.source;
    for (char32_t c = U'B'; c <= U'z'; ++c) {
        (void)f.glyph(c);
    }
    CHECK_EQ(f.pageCount(), std::size_t{3});
    CHECK(f.failedGlyphCount() > 0); // three 128x128 pages cannot hold every 48 px glyph
    // Existing glyphs never move.
    CHECK(f.glyph(U'A').texture == firstPage);
    CHECK(f.glyph(U'A').source == firstSource);

    const GlyphInfo& cjk = f.glyph(0x4E2D);
    CHECK(cjk.advance > 0.0f); // .notdef box
    REQUIRE(f.missingCodepoints().size() == 1);
    CHECK(f.missingCodepoints()[0] == char32_t{0x4E2D});
    (void)f.glyph(0x4E2D);
    CHECK_EQ(f.missingCodepoints().size(), std::size_t{1});
    CHECK_EQ(renderer.liveTextures, 3);
    font->reset();
    CHECK_EQ(renderer.liveTextures, 0);
}

SCHUIMM_TEST(Font_RejectsInvalidSettings)
{
    FakeRenderer renderer;
    CHECK(!Font::create(renderer, nullptr).has_value());
    CHECK(!Font::create(renderer, noto(), {0, TextureFilter::Linear, {64, 64}, 1, false}).has_value());
    CHECK(!Font::create(renderer, noto(), {16, TextureFilter::Linear, {0, 64}, 1, false}).has_value());
}
