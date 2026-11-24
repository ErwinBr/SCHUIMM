#pragma once

// CPU-side image decoding. No graphics API is involved: decoding works without a window or an
// OpenGL context, and the result (Core/Image/Image.h) is later turned into a GPU texture by the
// graphics layer.

#include "Core/Common/Error.h"
#include "Core/Image/Image.h"

#include <cstddef>
#include <span>
#include <string_view>

namespace schuimm {

enum class ImageFileFormat {
    Unknown,
    Png,
};

// Identifies the file format from its signature bytes (not from a file name).
[[nodiscard]] ImageFileFormat detectImageFileFormat(std::span<const std::byte> data) noexcept;

// Decodes an encoded image file in memory to RGBA8 (straight alpha, sRGB, rows top to bottom).
//
// Supported: PNG of every colour type and bit depth (greyscale, RGB, palette, with or without
// alpha or tRNS transparency, 1-16 bits, interlaced). Palette, greyscale and 16-bit images are
// converted; transparency is preserved as alpha; opaque images get alpha 255.
//
// Errors: InvalidData for unrecognised, corrupt or truncated data and for images larger than
// Image::maxDimension; `sourceName` (an asset path or file name) is included in messages.
[[nodiscard]] Result<Image> decodeImage(std::span<const std::byte> data, std::string_view sourceName);

} // namespace schuimm
