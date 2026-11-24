// FontFace (FreeType) with the vendored Noto Sans Regular from the EngineDemo assets.

#include "Resource/Font/FontFace.h"
#include "Resource/ResourceCache.h"
#include "TestFramework.h"

#include <algorithm>

using namespace schuimm;

namespace {

AssetLocator demoAssets()
{
    return AssetLocator::create(SCHUIMM_DEMO_ASSET_DIR).value();
}

FontFace loadNoto()
{
    auto face = loadFontFace(demoAssets(), AssetPath::parse("Fonts/NotoSans-Regular.ttf").value());
    if (!face) {
        std::fprintf(stderr, "  %s\n", describe(face.error()).c_str());
    }
    return std::move(face).value();
}

int coveredPixels(const GlyphBitmap& glyph)
{
    return static_cast<int>(std::count_if(glyph.coverage.begin(), glyph.coverage.end(), [](std::uint8_t c) { return c > 0; }));
}

} // namespace

SCHUIMM_TEST(FontFace_LoadsFontAndReportsNames)
{
    const FontFace face = loadNoto();
    CHECK_EQ(face.familyName(), std::string("Noto Sans"));
    CHECK_EQ(face.styleName(), std::string("Regular"));
    CHECK(face.glyphCount() > 1000u);
    CHECK_EQ(face.name(), std::string("Fonts/NotoSans-Regular.ttf"));
}

SCHUIMM_TEST(FontFace_HasGlyphsForWesternEuropeanLetters)
{
    const FontFace face = loadNoto();
    for (const char32_t c : {U'A', U'z', U'é', U'è', U'ë', U'ï', U'ü', U'ö', U'ä', U'ç', U'ñ', U'Ñ', U'ß', U'€'}) {
        CHECK(face.glyphIndex(c) != 0u);
    }
}

SCHUIMM_TEST(FontFace_RasterisesGlyphsWithMetrics)
{
    const FontFace face = loadNoto();
    auto metrics = face.metrics(32);
    REQUIRE(metrics.has_value());
    CHECK(metrics->ascender > 25.0f && metrics->ascender < 40.0f);
    CHECK(metrics->descender > 5.0f && metrics->descender < 15.0f);
    CHECK(metrics->lineHeight >= metrics->ascender + metrics->descender - 1.0f);

    auto a = face.rasterize(face.glyphIndex(U'A'), 32);
    REQUIRE(a.has_value());
    CHECK(a->width > 10 && a->width < 32);
    CHECK(a->height > 15 && a->height <= 32);
    CHECK(a->bearingY > 15 && a->bearingY <= 32); // sits on the baseline
    CHECK(a->advance > 15.0f && a->advance < 30.0f);
    CHECK_EQ(a->coverage.size(), static_cast<std::size_t>(a->width * a->height));
    CHECK(coveredPixels(*a) > 50);

    // An accented letter reaches higher than its base letter.
    auto e = face.rasterize(face.glyphIndex(U'e'), 32);
    auto eAcute = face.rasterize(face.glyphIndex(U'é'), 32);
    REQUIRE(e.has_value() && eAcute.has_value());
    CHECK(eAcute->bearingY > e->bearingY);
    auto cCedilla = face.rasterize(face.glyphIndex(U'ç'), 32);
    REQUIRE(cCedilla.has_value());
    CHECK(cCedilla->height - cCedilla->bearingY > 0); // the cedilla goes below the baseline

    // Larger size, larger glyph.
    auto big = face.rasterize(face.glyphIndex(U'A'), 64);
    REQUIRE(big.has_value());
    CHECK(big->height > a->height);

    auto space = face.rasterize(face.glyphIndex(U' '), 32);
    REQUIRE(space.has_value());
    CHECK(space->isEmpty());
    CHECK(space->advance > 0.0f);
}

SCHUIMM_TEST(FontFace_KerningPairs)
{
    const FontFace face = loadNoto();
    REQUIRE(face.hasKerning());
    const float av = face.kerning(face.glyphIndex(U'A'), face.glyphIndex(U'V'), 32);
    CHECK(av < 0.0f);
    CHECK_EQ(face.kerning(face.glyphIndex(U'H'), face.glyphIndex(U'H'), 32), 0.0f);
}

SCHUIMM_TEST(FontFace_MissingGlyphsAndInvalidRequests)
{
    const FontFace face = loadNoto();
    CHECK_EQ(face.glyphIndex(0x4E2D), 0u);  // CJK: not in Noto Sans
    CHECK_EQ(face.glyphIndex(0x10FFFD), 0u);
    auto notdef = face.rasterize(0, 32);
    REQUIRE(notdef.has_value());
    CHECK(notdef->advance > 0.0f);

    CHECK(!face.rasterize(face.glyphCount(), 32).has_value());
    auto badSize = face.rasterize(face.glyphIndex(U'A'), 0);
    REQUIRE(!badSize.has_value());
    CHECK(badSize.error().code == ErrorCode::InvalidArgument);
    CHECK(!face.metrics(FontFace::maxPixelSize + 1).has_value());
}

SCHUIMM_TEST(FontFace_RejectsInvalidData)
{
    auto empty = FontFace::load({}, "empty.ttf");
    REQUIRE(!empty.has_value());
    CHECK(empty.error().code == ErrorCode::InvalidData);

    std::vector<std::byte> garbage(256, std::byte{0x5A});
    auto invalid = FontFace::load(garbage, "garbage.ttf");
    REQUIRE(!invalid.has_value());
    CHECK(invalid.error().code == ErrorCode::InvalidData);
    CHECK(invalid.error().message.find("garbage.ttf") != std::string::npos);

    // A PNG is not a font.
    auto locator = AssetLocator::create(SCHUIMM_TEST_ASSET_DIR).value();
    auto png = loadFontFace(locator, AssetPath::parse("Images/rgba_4x2.png").value());
    REQUIRE(!png.has_value());
    CHECK(png.error().code == ErrorCode::InvalidData);

    auto missing = loadFontFace(demoAssets(), AssetPath::parse("Fonts/Missing.ttf").value());
    REQUIRE(!missing.has_value());
    CHECK(missing.error().code == ErrorCode::NotFound);
}

SCHUIMM_TEST(FontFace_WorksWithResourceCache)
{
    const AssetLocator locator = demoAssets();
    ResourceCache<FontFace> fonts(locator, loadFontFace);
    const auto id = ResourceId<FontFace>::parse("Fonts/NotoSans-Regular.ttf").value();
    auto first = fonts.load(id);
    auto second = fonts.load(id);
    REQUIRE(first.has_value() && second.has_value());
    CHECK(first->get() == second->get());
}
