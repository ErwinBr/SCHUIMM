#pragma once

#include "Core/Common/Error.h"
#include "Core/Common/Geometry.h"
#include "Graphics/Interface/Color.h"
#include "Graphics/Interface/Texture.h"

#include <array>
#include <memory>

namespace schuimm {

struct ColoredVertex {
    Vec2 position; // Virtual-resolution pixels, origin top-left.
    Color color;
};

// Minimal backend-neutral 2D drawing API used by application code. Coordinates are in
// virtual-resolution pixels (see PresentationSettings), origin top-left, y pointing down.
// Every draw call is alpha-blended (straight alpha).
//
// Deliberately small: sprite batching, resource management and text rendering belong to
// later milestones.
class Renderer2D {
public:
    virtual ~Renderer2D() = default;

    [[nodiscard]] virtual Extent2D virtualResolution() const = 0;

    virtual void clear(Color color) = 0;
    virtual void drawTriangle(const std::array<ColoredVertex, 3>& vertices) = 0;

    // Draws `texture` into `destination`. `source` is in normalised texture coordinates
    // (0..1, origin top-left). `tint` multiplies the texel colour including alpha.
    virtual void drawTexture(const Texture& texture, const RectF& destination,
                             const RectF& source = {0.0f, 0.0f, 1.0f, 1.0f}, Color tint = Color::white()) = 0;

    [[nodiscard]] virtual Result<std::unique_ptr<Texture>> createTexture(const TextureDescription& description) = 0;
};

} // namespace schuimm
