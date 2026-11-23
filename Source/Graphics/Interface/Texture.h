#pragma once

#include "Core/Common/Geometry.h"

#include <cstdint>
#include <span>

namespace schuimm {

enum class TextureFilter {
    Nearest, // Crisp pixel art.
    Linear,
};

struct TextureDescription {
    Extent2D size;
    std::span<const std::uint8_t> rgbaPixels; // width * height * 4 bytes, rows top to bottom
    TextureFilter filter = TextureFilter::Nearest;
};

// GPU texture owned through std::unique_ptr (RAII). A texture must be destroyed before
// the GraphicsDevice that created it.
class Texture {
public:
    virtual ~Texture() = default;
    [[nodiscard]] virtual Extent2D size() const = 0;
};

} // namespace schuimm
