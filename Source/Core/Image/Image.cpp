#include "Core/Image/Image.h"

#include "Core/Common/Assert.h"
#include "Core/Common/StringBuilder.h"

namespace schuimm {

const char* toString(PixelFormat format) noexcept
{
    switch (format) {
    case PixelFormat::RGBA8: return "RGBA8";
    }
    return "Unknown";
}

Result<Image> Image::create(Extent2D size, PixelFormat format)
{
    if (size.isEmpty() || size.width > maxDimension || size.height > maxDimension) {
        return makeError(ErrorCode::InvalidArgument, concat("invalid image size ", size.width, "x", size.height,
                                                            " (each dimension must be 1..", maxDimension, ")"));
    }
    return Image(size, format, static_cast<std::size_t>(size.width) * bytesPerPixel(format));
}

Image::Image(Extent2D size, PixelFormat format, std::size_t rowStride)
    : size_(size)
    , format_(format)
    , rowStride_(rowStride)
    , pixels_(rowStride * static_cast<std::size_t>(size.height), std::uint8_t{0})
{
}

std::span<const std::uint8_t> Image::row(int y) const noexcept
{
    SCHUIMM_ASSERT(y >= 0 && y < size_.height, "image row out of range");
    return std::span<const std::uint8_t>(pixels_).subspan(static_cast<std::size_t>(y) * rowStride_,
                                                          static_cast<std::size_t>(size_.width) * bytesPerPixel(format_));
}

std::span<std::uint8_t> Image::row(int y) noexcept
{
    SCHUIMM_ASSERT(y >= 0 && y < size_.height, "image row out of range");
    return std::span<std::uint8_t>(pixels_).subspan(static_cast<std::size_t>(y) * rowStride_,
                                                    static_cast<std::size_t>(size_.width) * bytesPerPixel(format_));
}

std::span<const std::uint8_t> Image::pixel(int x, int y) const noexcept
{
    SCHUIMM_ASSERT(x >= 0 && x < size_.width, "image column out of range");
    const std::size_t bpp = bytesPerPixel(format_);
    return row(y).subspan(static_cast<std::size_t>(x) * bpp, bpp);
}

std::span<std::uint8_t> Image::pixel(int x, int y) noexcept
{
    SCHUIMM_ASSERT(x >= 0 && x < size_.width, "image column out of range");
    const std::size_t bpp = bytesPerPixel(format_);
    return row(y).subspan(static_cast<std::size_t>(x) * bpp, bpp);
}

} // namespace schuimm
