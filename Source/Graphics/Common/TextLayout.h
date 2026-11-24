#pragma once

#include "Core/Common/Geometry.h"
#include "Graphics/Interface/Color.h"
#include "Graphics/Interface/Renderer2D.h"
#include "Graphics/Interface/Texture.h"

#include <cstdint>
#include <string_view>

namespace schuimm {

// Where a glyph's image is and how to place it, at the font's pixel size.
struct GlyphInfo {
    const Texture* texture = nullptr; // nullptr: nothing to draw (e.g. a space)
    RectF source;                     // texels in `texture`
    Vec2 offset;                      // from the pen position on the baseline to the image's top-left corner
    float advance = 0.0f;             // pen advance
    std::uint32_t glyphIndex = 0;     // for kerning
};

struct TextLineMetrics {
    float ascender = 0.0f;   // top of the line box to the baseline
    float lineHeight = 0.0f; // baseline to baseline
};

// The glyphs of one font at one size. Implemented by Font (GraphicsResources/Font.h), which
// rasterises glyphs into an atlas on demand; tests use a fake. glyph() never fails: code points
// the font cannot show map to a replacement glyph.
class GlyphSource {
public:
    virtual ~GlyphSource() = default;
    [[nodiscard]] virtual const GlyphInfo& glyph(char32_t codepoint) = 0;
    [[nodiscard]] virtual float kerning(const GlyphInfo& left, const GlyphInfo& right) = 0;
    [[nodiscard]] virtual TextLineMetrics lineMetrics() const = 0;
};

enum class TextAlign {
    Left,   // position.x is the left edge of every line
    Center, // position.x is the centre of every line
    Right,  // position.x is the right edge of every line
};

struct TextStyle {
    Color color = Color::white();
    TextAlign align = TextAlign::Left;
    int layer = 0;
    float lineSpacing = 1.0f; // multiple of the font's line height
};

// Basic text layout for UTF-8 strings: '\n' starts a new line; kerning between neighbouring
// glyphs; per-line horizontal alignment. No shaping, bidirectional text or automatic wrapping.

// Size of the text block: width of the widest line; height = lines * line advance, where the
// line advance is lineHeight * lineSpacing rounded to whole units.
[[nodiscard]] Vec2 measureText(GlyphSource& font, std::string_view text, float lineSpacing = 1.0f);

// Width of the first line of `text` (up to the first '\n').
[[nodiscard]] float measureLine(GlyphSource& font, std::string_view text);

// Draws `text` through the renderer's sprite batch. `position` is the top of the first line;
// its x is interpreted according to style.align. Glyph positions are rounded to whole units of
// the current view so glyphs map 1:1 onto target pixels when the view is the identity.
void drawText(Renderer2D& renderer, GlyphSource& font, std::string_view text, Vec2 position, const TextStyle& style = {});

} // namespace schuimm
