#pragma once

#include "Core/Common/Geometry.h"
#include "Graphics/Interface/Texture.h"

namespace schuimm {

struct RenderTargetDescription {
    Extent2D size;           // pixels; independent of window and virtual resolution
    TextureSettings settings; // sampling of the resulting texture
};

// An off-screen image that can be rendered into (Renderer2D::beginRenderTarget) and then drawn
// like any other texture (texture()). Its contents are premultiplied RGBA8 and start fully
// transparent. Created by a Renderer2D; must be destroyed before the GraphicsDevice.
class RenderTarget {
public:
    virtual ~RenderTarget() = default;
    [[nodiscard]] virtual Extent2D size() const = 0;
    [[nodiscard]] virtual const Texture& texture() const = 0;
};

} // namespace schuimm
