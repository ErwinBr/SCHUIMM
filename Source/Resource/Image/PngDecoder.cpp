#include "Resource/Image/PngDecoder.h"

#include "Core/Common/StringBuilder.h"

#include <png.h>

#include <limits>

namespace schuimm::detail {

// Uses libpng's "simplified API" (png_image_*), which handles every PNG colour type and bit depth,
// converts to the requested format and reports errors through png_image::message. Unlike the
// classic API it needs no setjmp/longjmp in this (C++) translation unit.
namespace {

std::unexpected<Error> pngFailure(std::string_view sourceName, std::string_view reason)
{
    return makeError(ErrorCode::InvalidData, concat("cannot decode PNG '", sourceName, "': ", reason));
}

// Reads the header (png_read_info inside libpng's simplified API) and checks the size. On success
// the caller owns `png` and must finish or free it.
Status beginPng(png_image& png, std::span<const std::byte> data, std::string_view sourceName)
{
    png.version = PNG_IMAGE_VERSION;
    // On failure libpng releases its internal state itself.
    if (png_image_begin_read_from_memory(&png, data.data(), data.size()) == 0) {
        return pngFailure(sourceName, png.message);
    }
    if (png.width == 0 || png.height == 0 || png.width > static_cast<png_uint_32>(Image::maxDimension) ||
        png.height > static_cast<png_uint_32>(Image::maxDimension)) {
        const std::string reason = concat("image size ", png.width, "x", png.height, " exceeds ", Image::maxDimension,
                                          "x", Image::maxDimension);
        png_image_free(&png);
        return pngFailure(sourceName, reason);
    }
    return {};
}

} // namespace

Result<Extent2D> readPngSize(std::span<const std::byte> data, std::string_view sourceName)
{
    png_image png{};
    if (auto status = beginPng(png, data, sourceName); !status) return std::unexpected(status.error());
    const Extent2D size{static_cast<int>(png.width), static_cast<int>(png.height)};
    png_image_free(&png);
    return size;
}

Result<Image> decodePng(std::span<const std::byte> data, std::string_view sourceName)
{
    const auto failure = [sourceName](std::string_view reason) { return pngFailure(sourceName, reason); };

    png_image png{};
    if (auto status = beginPng(png, data, sourceName); !status) return std::unexpected(status.error());

    // 8-bit RGBA, sRGB-encoded, straight alpha: palette and greyscale images are expanded, tRNS
    // becomes alpha, 16-bit channels are reduced, and opaque images get alpha 255.
    png.format = PNG_FORMAT_RGBA;
    auto image = Image::create({static_cast<int>(png.width), static_cast<int>(png.height)}, PixelFormat::RGBA8);
    if (!image) {
        png_image_free(&png);
        return failure(image.error().message);
    }
    static_assert(Image::maxDimension * 4 <= std::numeric_limits<png_int_32>::max());
    const auto stride = static_cast<png_int_32>(image->rowStride());
    // Background nullptr: keep the alpha channel. Colormap nullptr: not a colour-mapped format.
    if (png_image_finish_read(&png, nullptr, image->pixels().data(), stride, nullptr) == 0) {
        return failure(png.message);
    }
    return std::move(*image);
}

} // namespace schuimm::detail
