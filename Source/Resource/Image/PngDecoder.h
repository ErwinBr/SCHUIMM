#pragma once

// Internal to the resource layer: the only place that knows about libpng. Use decodeImage().

#include "Core/Common/Error.h"
#include "Core/Common/Geometry.h"
#include "Core/Image/Image.h"

#include <cstddef>
#include <span>
#include <string_view>

namespace schuimm::detail {

[[nodiscard]] Result<Image> decodePng(std::span<const std::byte> data, std::string_view sourceName);

// Reads and validates the PNG header only (libpng's png_read_info): no pixel data is decoded.
[[nodiscard]] Result<Extent2D> readPngSize(std::span<const std::byte> data, std::string_view sourceName);

} // namespace schuimm::detail
