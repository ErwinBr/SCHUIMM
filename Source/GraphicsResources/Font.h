#pragma once

#include "Core/Common/Error.h"
#include "Graphics/Common/ShelfPacker.h"
#include "Graphics/Common/TextLayout.h"
#include "Graphics/Interface/Renderer2D.h"
#include "Resource/Font/FontFace.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace schuimm {

// A font face at one pixel size, ready for drawing: glyphs are rasterised on first use (CPU,
// FontFace) and packed into atlas pages, which are dynamic textures created through Renderer2D.
//
// Atlas: pages of a fixed size. A new glyph goes into the current page; when it is full a new
// page is added (up to maxPages). Glyphs never move, so the atlas is never rebuilt and existing
// texture coordinates stay valid. Glyph images are stored as premultiplied white with the
// coverage as alpha, so the text colour is simply the sprite tint.
//
// Missing glyphs: a code point the face does not cover is drawn with the face's .notdef glyph
// (glyph 0), or '?' if .notdef is empty. Such code points are listed in missingCodepoints().
// A glyph that does not fit into the atlas any more is drawn as nothing (but still advances).
//
// Ownership: the Font owns its atlas textures and shares the FontFace. It must be destroyed
// before the GraphicsDevice. Not thread-safe.
class Font final : public GlyphSource {
public:
    struct Settings {
        int pixelSize = 16;
        TextureFilter filter = TextureFilter::Linear;
        Extent2D pageSize{512, 512};
        int maxPages = 4;
        // Rasterise printable ASCII and Latin-1 (U+0020..U+007E, U+00A0..U+00FF) up front, so
        // typical Western European text does not add glyphs while a frame is drawn.
        bool preloadLatin1 = true;
    };

    [[nodiscard]] static Result<std::unique_ptr<Font>> create(Renderer2D& renderer, std::shared_ptr<const FontFace> face,
                                                              Settings settings);
    [[nodiscard]] static Result<std::unique_ptr<Font>> create(Renderer2D& renderer, std::shared_ptr<const FontFace> face)
    {
        return create(renderer, std::move(face), Settings{});
    }

    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    // GlyphSource
    [[nodiscard]] const GlyphInfo& glyph(char32_t codepoint) override;
    [[nodiscard]] float kerning(const GlyphInfo& left, const GlyphInfo& right) override;
    [[nodiscard]] TextLineMetrics lineMetrics() const override { return lineMetrics_; }

    [[nodiscard]] int pixelSize() const noexcept { return settings_.pixelSize; }
    [[nodiscard]] const FontFace& face() const noexcept { return *face_; }
    [[nodiscard]] std::size_t pageCount() const noexcept { return pages_.size(); }
    [[nodiscard]] const Texture& page(std::size_t index) const { return *pages_.at(index); }
    [[nodiscard]] std::size_t cachedGlyphCount() const noexcept { return glyphs_.size(); }
    [[nodiscard]] const std::vector<char32_t>& missingCodepoints() const noexcept { return missing_; }
    // Glyphs that could not be rasterised or did not fit into the atlas.
    [[nodiscard]] int failedGlyphCount() const noexcept { return failedGlyphs_; }

private:
    Font(Renderer2D& renderer, std::shared_ptr<const FontFace> face, Settings settings);
    Status addPage();
    GlyphInfo makeGlyph(std::uint32_t glyphIndex);

    Renderer2D& renderer_;
    std::shared_ptr<const FontFace> face_;
    Settings settings_;
    TextLineMetrics lineMetrics_;
    std::vector<std::unique_ptr<Texture>> pages_;
    ShelfPacker packer_;
    std::unordered_map<char32_t, GlyphInfo> glyphs_;
    std::unordered_map<std::uint32_t, GlyphInfo> glyphsByIndex_; // shared by code points with the same glyph
    std::vector<char32_t> missing_;
    std::vector<std::uint8_t> scratch_;
    int failedGlyphs_ = 0;
};

} // namespace schuimm
