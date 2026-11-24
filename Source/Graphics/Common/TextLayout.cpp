#include "Graphics/Common/TextLayout.h"

#include "Core/Text/Utf8.h"

#include <algorithm>
#include <cmath>

namespace schuimm {

namespace {

// Splits off the first line of `text` (without the '\n'); `text` keeps the rest.
std::string_view takeLine(std::string_view& text)
{
    const std::size_t end = text.find('\n');
    const std::string_view line = text.substr(0, end);
    text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
    return line;
}

} // namespace

float measureLine(GlyphSource& font, std::string_view text)
{
    std::string_view line = takeLine(text);
    float pen = 0.0f;
    const GlyphInfo* previous = nullptr;
    char32_t codepoint = 0;
    while (utf8::decodeNext(line, codepoint)) {
        if (codepoint == '\r') {
            continue;
        }
        const GlyphInfo& glyph = font.glyph(codepoint);
        if (previous != nullptr) {
            pen += font.kerning(*previous, glyph);
        }
        pen += glyph.advance;
        previous = &glyph;
    }
    return pen;
}

Vec2 measureText(GlyphSource& font, std::string_view text, float lineSpacing)
{
    if (text.empty()) {
        return {};
    }
    float width = 0.0f;
    int lines = 0;
    std::string_view rest = text;
    bool more = true;
    while (more) {
        more = rest.find('\n') != std::string_view::npos;
        width = std::max(width, measureLine(font, rest));
        (void)takeLine(rest);
        ++lines;
    }
    // Same line advance as drawText().
    return {width, static_cast<float>(lines) * std::round(font.lineMetrics().lineHeight * lineSpacing)};
}

void drawText(Renderer2D& renderer, GlyphSource& font, std::string_view text, Vec2 position, const TextStyle& style)
{
    const TextLineMetrics metrics = font.lineMetrics();
    float baseline = std::round(position.y + metrics.ascender);
    std::string_view rest = text;
    bool more = !text.empty();
    while (more) {
        more = rest.find('\n') != std::string_view::npos;
        std::string_view line = takeLine(rest);

        float pen = position.x;
        if (style.align != TextAlign::Left) {
            const float width = measureLine(font, line);
            pen -= style.align == TextAlign::Center ? width * 0.5f : width;
        }

        const GlyphInfo* previous = nullptr;
        char32_t codepoint = 0;
        while (utf8::decodeNext(line, codepoint)) {
            if (codepoint == '\r') {
                continue;
            }
            const GlyphInfo& glyph = font.glyph(codepoint);
            if (previous != nullptr) {
                pen += font.kerning(*previous, glyph);
            }
            if (glyph.texture != nullptr) {
                Sprite sprite;
                sprite.texture = glyph.texture;
                sprite.destination = {std::round(pen + glyph.offset.x), baseline + glyph.offset.y, glyph.source.width,
                                      glyph.source.height};
                sprite.source = glyph.source;
                sprite.tint = style.color;
                sprite.layer = style.layer;
                renderer.drawSprite(sprite);
            }
            pen += glyph.advance;
            previous = &glyph;
        }
        baseline += std::round(metrics.lineHeight * style.lineSpacing);
    }
}

} // namespace schuimm
