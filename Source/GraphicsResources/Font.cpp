#include "GraphicsResources/Font.h"

#include "Core/Common/StringBuilder.h"

#include <algorithm>

namespace schuimm {

namespace {
constexpr int atlasPadding = 1;
} // namespace

Font::Font(Renderer2D& renderer, std::shared_ptr<const FontFace> face, Settings settings)
    : renderer_(renderer)
    , face_(std::move(face))
    , settings_(settings)
    , packer_(settings.pageSize, atlasPadding)
{
}

Result<std::unique_ptr<Font>> Font::create(Renderer2D& renderer, std::shared_ptr<const FontFace> face, Settings settings)
{
    if (!face) {
        return makeError(ErrorCode::InvalidArgument, "a font face is required");
    }
    if (settings.pageSize.isEmpty() || settings.maxPages < 1) {
        return makeError(ErrorCode::InvalidArgument, "font atlas pages must have a positive size and count");
    }
    auto metrics = face->metrics(settings.pixelSize);
    if (!metrics) {
        return std::unexpected(metrics.error());
    }
    std::unique_ptr<Font> font(new Font(renderer, std::move(face), settings));
    font->lineMetrics_ = {metrics->ascender, metrics->lineHeight};
    if (auto status = font->addPage(); !status) {
        return std::unexpected(status.error());
    }
    if (settings.preloadLatin1) {
        for (char32_t c = 0x20; c <= 0x7E; ++c) {
            (void)font->glyph(c);
        }
        for (char32_t c = 0xA0; c <= 0xFF; ++c) {
            (void)font->glyph(c);
        }
    }
    return font;
}

Status Font::addPage()
{
    TextureDescription description;
    description.size = settings_.pageSize;
    description.alphaMode = AlphaMode::Premultiplied;
    description.settings = {settings_.filter, TextureWrap::ClampToEdge, TextureWrap::ClampToEdge};
    description.usage = TextureUsage::Dynamic;
    auto page = renderer_.createTexture(description);
    if (!page) {
        return makeError(page.error().code, concat("font '", face_->name(), "': cannot create atlas page: ", page.error().message));
    }
    pages_.push_back(std::move(*page));
    packer_.reset();
    return {};
}

GlyphInfo Font::makeGlyph(std::uint32_t glyphIndex)
{
    GlyphInfo info;
    info.glyphIndex = glyphIndex;
    auto bitmap = face_->rasterize(glyphIndex, settings_.pixelSize);
    if (!bitmap) {
        ++failedGlyphs_;
        return info;
    }
    info.advance = bitmap->advance;
    if (bitmap->isEmpty()) {
        return info;
    }

    std::optional<TextureRegion> region = packer_.insert(bitmap->width, bitmap->height);
    if (!region && static_cast<int>(pages_.size()) < settings_.maxPages && addPage()) {
        region = packer_.insert(bitmap->width, bitmap->height);
    }
    if (!region) {
        ++failedGlyphs_; // atlas full or glyph larger than a page
        return info;
    }

    // Coverage -> premultiplied white.
    scratch_.resize(bitmap->coverage.size() * 4);
    for (std::size_t i = 0; i < bitmap->coverage.size(); ++i) {
        const std::uint8_t c = bitmap->coverage[i];
        scratch_[i * 4 + 0] = c;
        scratch_[i * 4 + 1] = c;
        scratch_[i * 4 + 2] = c;
        scratch_[i * 4 + 3] = c;
    }
    Texture& page = *pages_.back();
    if (!renderer_.updateTexture(page, *region, scratch_, 0, AlphaMode::Premultiplied)) {
        ++failedGlyphs_;
        return info;
    }
    info.texture = &page;
    info.source = {static_cast<float>(region->x), static_cast<float>(region->y), static_cast<float>(region->width),
                   static_cast<float>(region->height)};
    info.offset = {static_cast<float>(bitmap->bearingX), -static_cast<float>(bitmap->bearingY)};
    return info;
}

const GlyphInfo& Font::glyph(char32_t codepoint)
{
    if (const auto it = glyphs_.find(codepoint); it != glyphs_.end()) {
        return it->second;
    }

    std::uint32_t index = face_->glyphIndex(codepoint);
    const bool isMissing = index == 0 && codepoint != 0;
    if (isMissing) {
        missing_.push_back(codepoint);
    }

    auto byIndex = glyphsByIndex_.find(index);
    if (byIndex == glyphsByIndex_.end()) {
        GlyphInfo info = makeGlyph(index);
        if (index == 0 && info.texture == nullptr && info.advance == 0.0f) {
            // Empty .notdef: fall back to the question mark.
            if (const std::uint32_t question = face_->glyphIndex(U'?'); question != 0) {
                info = makeGlyph(question);
            }
        }
        byIndex = glyphsByIndex_.emplace(index, info).first;
    }
    return glyphs_.emplace(codepoint, byIndex->second).first->second;
}

float Font::kerning(const GlyphInfo& left, const GlyphInfo& right)
{
    return face_->kerning(left.glyphIndex, right.glyphIndex, settings_.pixelSize);
}

} // namespace schuimm
