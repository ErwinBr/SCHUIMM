#pragma once

// Internal to the resource layer: the only place that knows about libpng. Use decodeImage().

#include "Core/Common/Error.h"
#include "Core/Image/Image.h"

#include <cstddef>
#include <span>
#include <string_view>

namespace schuimm::detail {

[[nodiscard]] Result<Image> decodePng(std::span<const std::byte> data, std::string_view sourceName);

} // namespace schuimm::detail
