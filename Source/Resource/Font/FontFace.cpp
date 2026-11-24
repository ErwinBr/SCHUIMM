// The only engine source file that includes FreeType headers.

#include "Resource/Font/FontFace.h"

#include "Core/Common/StringBuilder.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cmath>
#include <cstring>

namespace schuimm {

namespace {

std::string freeTypeError(FT_Error error)
{
    // FT_Error_String needs FT_CONFIG_OPTION_ERROR_STRINGS (off); the code is still useful.
    return concat("FreeType error ", error);
}

float fromF26Dot6(FT_Pos value)
{
    return static_cast<float>(value) / 64.0f;
}

} // namespace

struct FontFace::Impl {
    std::string name;
    std::vector<std::byte> data; // must outlive `face`
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    int currentSize = 0;

    ~Impl()
    {
        if (face != nullptr) {
            FT_Done_Face(face);
        }
        if (library != nullptr) {
            FT_Done_FreeType(library);
        }
    }

    Status setSize(int pixelSize)
    {
        if (pixelSize <= 0 || pixelSize > maxPixelSize) {
            return makeError(ErrorCode::InvalidArgument,
                             concat("font '", name, "': pixel size ", pixelSize, " outside 1..", maxPixelSize));
        }
        if (pixelSize == currentSize) {
            return {};
        }
        if (const FT_Error error = FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixelSize)); error != 0) {
            return makeError(ErrorCode::InvalidData,
                             concat("font '", name, "': cannot set pixel size ", pixelSize, ": ", freeTypeError(error)));
        }
        currentSize = pixelSize;
        return {};
    }
};

FontFace::FontFace(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl))
{
}

FontFace::FontFace(FontFace&&) noexcept = default;
FontFace& FontFace::operator=(FontFace&&) noexcept = default;
FontFace::~FontFace() = default;

Result<FontFace> FontFace::load(std::vector<std::byte> data, std::string name)
{
    auto impl = std::make_unique<Impl>();
    impl->name = std::move(name);
    impl->data = std::move(data);
    if (impl->data.empty()) {
        return makeError(ErrorCode::InvalidData, concat("font '", impl->name, "' is empty"));
    }
    // One FreeType library per face: no global state, and faces are independent.
    if (const FT_Error error = FT_Init_FreeType(&impl->library); error != 0) {
        impl->library = nullptr;
        return makeError(ErrorCode::Unknown, concat("cannot initialise FreeType: ", freeTypeError(error)));
    }
    const FT_Error error = FT_New_Memory_Face(impl->library, reinterpret_cast<const FT_Byte*>(impl->data.data()),
                                              static_cast<FT_Long>(impl->data.size()), 0, &impl->face);
    if (error != 0) {
        impl->face = nullptr;
        return makeError(ErrorCode::InvalidData,
                         concat("font '", impl->name, "' is not a supported font file (", freeTypeError(error), ")"));
    }
    if (!FT_IS_SCALABLE(impl->face)) {
        return makeError(ErrorCode::InvalidData, concat("font '", impl->name, "' has no scalable outlines"));
    }
    // Prefer a Unicode character map; FreeType selects one by default when the font has it.
    if (impl->face->charmap == nullptr || impl->face->charmap->encoding != FT_ENCODING_UNICODE) {
        if (FT_Select_Charmap(impl->face, FT_ENCODING_UNICODE) != 0) {
            return makeError(ErrorCode::InvalidData, concat("font '", impl->name, "' has no Unicode character map"));
        }
    }
    return FontFace(std::move(impl));
}

const std::string& FontFace::name() const noexcept
{
    return impl_->name;
}

std::string FontFace::familyName() const
{
    return impl_->face->family_name != nullptr ? impl_->face->family_name : "";
}

std::string FontFace::styleName() const
{
    return impl_->face->style_name != nullptr ? impl_->face->style_name : "";
}

std::uint32_t FontFace::glyphCount() const noexcept
{
    return static_cast<std::uint32_t>(impl_->face->num_glyphs);
}

bool FontFace::hasKerning() const noexcept
{
    return FT_HAS_KERNING(impl_->face);
}

std::uint32_t FontFace::glyphIndex(char32_t codepoint) const noexcept
{
    return FT_Get_Char_Index(impl_->face, static_cast<FT_ULong>(codepoint));
}

Result<FontMetrics> FontFace::metrics(int pixelSize) const
{
    if (auto status = impl_->setSize(pixelSize); !status) {
        return std::unexpected(status.error());
    }
    const FT_Size_Metrics& m = impl_->face->size->metrics;
    FontMetrics result;
    result.ascender = fromF26Dot6(m.ascender);
    result.descender = -fromF26Dot6(m.descender); // FreeType: negative below the baseline
    result.lineHeight = fromF26Dot6(m.height);
    return result;
}

Result<GlyphBitmap> FontFace::rasterize(std::uint32_t glyphIndex, int pixelSize) const
{
    if (auto status = impl_->setSize(pixelSize); !status) {
        return std::unexpected(status.error());
    }
    FT_Face face = impl_->face;
    if (glyphIndex >= static_cast<std::uint32_t>(face->num_glyphs)) {
        return makeError(ErrorCode::InvalidArgument,
                         concat("font '", impl_->name, "' has no glyph ", glyphIndex, " (", face->num_glyphs, " glyphs)"));
    }
    // Light auto-hinting: crisp at small sizes without distorting the glyph shapes.
    if (const FT_Error error = FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT); error != 0) {
        return makeError(ErrorCode::InvalidData,
                         concat("font '", impl_->name, "': cannot load glyph ", glyphIndex, ": ", freeTypeError(error)));
    }
    if (const FT_Error error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL); error != 0) {
        return makeError(ErrorCode::InvalidData,
                         concat("font '", impl_->name, "': cannot render glyph ", glyphIndex, ": ", freeTypeError(error)));
    }
    const FT_GlyphSlot slot = face->glyph;
    const FT_Bitmap& bitmap = slot->bitmap;
    GlyphBitmap glyph;
    glyph.advance = fromF26Dot6(slot->advance.x);
    glyph.bearingX = slot->bitmap_left;
    glyph.bearingY = slot->bitmap_top;
    if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY || bitmap.width == 0 || bitmap.rows == 0) {
        return glyph; // nothing visible (e.g. space)
    }
    glyph.width = static_cast<int>(bitmap.width);
    glyph.height = static_cast<int>(bitmap.rows);
    glyph.coverage.resize(static_cast<std::size_t>(glyph.width) * static_cast<std::size_t>(glyph.height));
    const int pitch = bitmap.pitch;
    for (int y = 0; y < glyph.height; ++y) {
        // A negative pitch means the rows are stored bottom-up.
        const int sourceRow = pitch >= 0 ? y : glyph.height - 1 - y;
        const unsigned char* row = bitmap.buffer + static_cast<std::ptrdiff_t>(sourceRow) * std::abs(pitch);
        std::memcpy(glyph.coverage.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(glyph.width), row,
                    static_cast<std::size_t>(glyph.width));
    }
    return glyph;
}

float FontFace::kerning(std::uint32_t leftGlyph, std::uint32_t rightGlyph, int pixelSize) const
{
    if (!hasKerning() || !impl_->setSize(pixelSize)) {
        return 0.0f;
    }
    FT_Vector delta{};
    if (FT_Get_Kerning(impl_->face, leftGlyph, rightGlyph, FT_KERNING_DEFAULT, &delta) != 0) {
        return 0.0f;
    }
    return fromF26Dot6(delta.x);
}

Result<FontFace> loadFontFace(const AssetLocator& locator, const AssetPath& asset)
{
    auto bytes = locator.readBytes(asset);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    return FontFace::load(std::move(*bytes), asset.string());
}

} // namespace schuimm
