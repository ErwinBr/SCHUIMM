#pragma once

#include "Core/Common/Error.h"
#include "Resource/AssetLocator.h"
#include "Resource/AssetPath.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace schuimm {

// Vertical metrics of a font at one pixel size, in pixels. y points down, so ascender is
// the distance from the top of a line to the baseline and descender the (positive) distance
// from the baseline down to the bottom of the line box.
struct FontMetrics {
    float ascender = 0.0f;
    float descender = 0.0f;
    float lineHeight = 0.0f; // baseline-to-baseline distance
};

// A rasterised glyph: 8-bit coverage (0 = empty, 255 = fully covered), rows top to bottom.
struct GlyphBitmap {
    int width = 0;
    int height = 0;
    int bearingX = 0; // from the pen position to the left edge of the bitmap
    int bearingY = 0; // from the baseline up to the top edge of the bitmap
    float advance = 0.0f; // horizontal pen advance
    std::vector<std::uint8_t> coverage; // width * height bytes

    [[nodiscard]] bool isEmpty() const noexcept { return width == 0 || height == 0; }
};

// A loaded font file (CPU side): glyph lookup, rasterisation, metrics and kerning, backed by
// FreeType. It knows nothing about textures or the GPU; glyph atlases are built from its
// bitmaps by the graphics layer (GraphicsResources/Font).
//
// Supported: TrueType-outline fonts (.ttf, .otf with TrueType outlines, .ttc: the first face).
// Kerning comes from the legacy 'kern' table or GPOS pair adjustment; there is no shaping.
//
// Glyph index 0 is the font's ".notdef" glyph, which glyphIndex() returns for code points the
// font does not cover.
//
// Rasterisation changes internal FreeType state, so a FontFace (even through a const
// reference) must only be used from one thread at a time.
class FontFace {
public:
    // Takes the file's bytes; FreeType reads them for the lifetime of the face. `name` is used
    // in error messages. Fails with InvalidData if the data is not a supported font.
    [[nodiscard]] static Result<FontFace> load(std::vector<std::byte> data, std::string name);

    FontFace(FontFace&&) noexcept;
    FontFace& operator=(FontFace&&) noexcept;
    ~FontFace();

    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] std::string familyName() const;
    [[nodiscard]] std::string styleName() const;
    [[nodiscard]] std::uint32_t glyphCount() const noexcept;
    [[nodiscard]] bool hasKerning() const noexcept;

    // The glyph for a Unicode code point, or 0 (.notdef) if the font has none.
    [[nodiscard]] std::uint32_t glyphIndex(char32_t codepoint) const noexcept;

    [[nodiscard]] Result<FontMetrics> metrics(int pixelSize) const;
    [[nodiscard]] Result<GlyphBitmap> rasterize(std::uint32_t glyphIndex, int pixelSize) const;
    // Horizontal adjustment between two glyphs (usually negative), in pixels.
    [[nodiscard]] float kerning(std::uint32_t leftGlyph, std::uint32_t rightGlyph, int pixelSize) const;

    // Largest accepted pixel size.
    static constexpr int maxPixelSize = 512;

private:
    struct Impl;
    explicit FontFace(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// ResourceCache<FontFace> loader: reads the asset and parses it.
[[nodiscard]] Result<FontFace> loadFontFace(const AssetLocator& locator, const AssetPath& asset);

} // namespace schuimm
