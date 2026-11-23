#pragma once

#include "Core/Common/Geometry.h"

#include <cstdint>
#include <vector>

namespace schuimm {

// CPU-side 8-bit RGBA image, rows stored top to bottom.
struct Image {
    Extent2D size;
    std::vector<std::uint8_t> pixels; // size.width * size.height * 4 bytes
};

} // namespace schuimm
