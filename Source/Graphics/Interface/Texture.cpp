#include "Graphics/Interface/Texture.h"

#include "Core/Common/CheckedMath.h"
#include "Core/Common/StringBuilder.h"

namespace schuimm {

const char* toString(TextureFilter filter) noexcept
{
    switch (filter) {
    case TextureFilter::Nearest: return "Nearest";
    case TextureFilter::Linear: return "Linear";
    }
    return "?";
}

const char* toString(TextureWrap wrap) noexcept
{
    switch (wrap) {
    case TextureWrap::ClampToEdge: return "ClampToEdge";
    case TextureWrap::Repeat: return "Repeat";
    case TextureWrap::MirroredRepeat: return "MirroredRepeat";
    }
    return "?";
}

Status validatePixelLayout(Extent2D size, std::size_t byteCount, std::size_t rowStride)
{
    if (size.isEmpty()) {
        return makeError(ErrorCode::InvalidArgument, concat("invalid texture size ", size.width, "x", size.height));
    }
    const std::optional<std::size_t> rowBytes = checkedMul(static_cast<std::size_t>(size.width), 4);
    if (!rowBytes) {
        return makeError(ErrorCode::InvalidArgument, concat("pixel row of ", size.width, " texels is too large"));
    }
    const std::size_t stride = rowStride == 0 ? *rowBytes : rowStride;
    if (stride < *rowBytes) {
        return makeError(ErrorCode::InvalidArgument,
                         concat("texture row stride ", stride, " is smaller than a row (", *rowBytes, " bytes)"));
    }
    const std::optional<std::size_t> lastRow = checkedMul(stride, static_cast<std::size_t>(size.height - 1));
    const std::optional<std::size_t> required = lastRow ? checkedAdd(*lastRow, *rowBytes) : std::nullopt;
    if (!required) {
        return makeError(ErrorCode::InvalidArgument, concat("texture row stride too large (", stride, " bytes for ",
                                                            size.height, " rows)"));
    }
    if (byteCount < *required) {
        return makeError(ErrorCode::InvalidArgument, concat("texture ", size.width, "x", size.height, " expects at least ",
                                                            *required, " bytes of RGBA8 data, got ", byteCount));
    }
    return {};
}

Status validateRegion(Extent2D size, TextureRegion region)
{
    // Subtraction only: size.width - region.x cannot overflow once 0 <= region.x <= size.width.
    const bool inside = region.x >= 0 && region.y >= 0 && region.width > 0 && region.height > 0 && region.x <= size.width &&
                        region.width <= size.width - region.x && region.y <= size.height &&
                        region.height <= size.height - region.y;
    if (!inside) {
        return makeError(ErrorCode::InvalidArgument, concat("update region ", region.x, ",", region.y, " ", region.width, "x",
                                                            region.height, " is outside the ", size.width, "x", size.height,
                                                            " texture"));
    }
    return {};
}

} // namespace schuimm
