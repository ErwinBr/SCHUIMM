#pragma once

#include "Core/Common/Error.h"
#include "Core/Common/Geometry.h"
#include "Core/Common/Transform2D.h"
#include "Core/Image/Image.h"
#include "Graphics/Interface/Color.h"
#include "Graphics/Interface/RenderTarget.h"
#include "Graphics/Interface/Sprite.h"
#include "Graphics/Interface/Texture.h"

#include <array>
#include <cstdint>
#include <memory>

namespace schuimm {

struct ColoredVertex {
    Vec2 position; // view space (see Renderer2D)
    Color color;
};

// Counters of one frame (between GraphicsDevice::beginFrame and endFrame), for overlays and tests.
struct RenderStatistics {
    std::uint32_t quads = 0;     // sprites, triangles and glyphs submitted
    std::uint32_t drawCalls = 0; // GPU draw calls (one per batch)
    std::uint32_t flushes = 0;   // times the pending sprites were sorted and submitted
    std::uint32_t renderTargetSwitches = 0;
};

// Backend-neutral 2D drawing API used by application code.
//
// Coordinates. Every draw is in *view space*: the current view transform (setView) maps it to
// the pixels of the current target (origin top-left, y down). The default view is the identity,
// so without a camera coordinates are target pixels. The current target is the virtual-
// resolution scene (see PresentationSettings) unless a render target is active.
//
// Blending. All drawing is alpha blended with premultiplied alpha. Colours passed in (tint,
// clear colours, vertex colours) use straight alpha; images are converted when uploaded.
//
// Batching and ordering. Draws are collected and submitted in batches. When pending draws are
// flushed, they are drawn ordered by Sprite::layer and, within a layer, in submission order.
// A flush happens on flush(), clear(), setView()/resetView(), beginRenderTarget(),
// endRenderTarget(), at the end of the frame, and when the pending-draw capacity is reached.
// Layer ordering therefore applies to the draws between two flushes.
//
// Textures and render targets are owned by the caller (std::unique_ptr) and must be destroyed
// before the GraphicsDevice. All calls must be made on the graphics device's thread.
class Renderer2D {
public:
    virtual ~Renderer2D() = default;

    [[nodiscard]] virtual Extent2D virtualResolution() const = 0;
    // Size in pixels of the current target (scene or render target).
    [[nodiscard]] virtual Extent2D targetSize() const = 0;
    // Number of different textures that can be used within one batch (draw call).
    [[nodiscard]] virtual int textureSlots() const = 0;

    // --- Drawing ---------------------------------------------------------------------------
    // Clears the whole current target (flushes pending draws first).
    virtual void clear(Color color) = 0;
    virtual void drawSprite(const Sprite& sprite) = 0;
    virtual void drawTriangle(const std::array<ColoredVertex, 3>& vertices, int layer = 0) = 0;

    // Draws `texture` into `destination`. `source` is in normalised texture coordinates
    // (0..1, origin top-left). `tint` multiplies the texel colour including alpha.
    void drawTexture(const Texture& texture, const RectF& destination,
                     const RectF& source = {0.0f, 0.0f, 1.0f, 1.0f}, Color tint = Color::white())
    {
        const Extent2D size = texture.size();
        Sprite sprite;
        sprite.texture = &texture;
        sprite.destination = destination;
        sprite.source = RectF{source.x * static_cast<float>(size.width), source.y * static_cast<float>(size.height),
                              source.width * static_cast<float>(size.width), source.height * static_cast<float>(size.height)};
        sprite.tint = tint;
        drawSprite(sprite);
    }

    void fillRect(const RectF& rect, Color color, int layer = 0)
    {
        Sprite sprite;
        sprite.destination = rect;
        sprite.tint = color;
        sprite.layer = layer;
        drawSprite(sprite);
    }

    // Submits all pending draws to the GPU.
    virtual void flush() = 0;

    // --- View ------------------------------------------------------------------------------
    // Sets the view-space -> target-pixel transform for subsequent draws (flushes first).
    // Typically camera.viewTransform(); resetView() returns to target pixels, e.g. for UI.
    virtual void setView(const Transform2D& view) = 0;
    void resetView() { setView(Transform2D::identity()); }
    [[nodiscard]] virtual const Transform2D& view() const = 0;

    // --- Textures --------------------------------------------------------------------------
    // Fails with InvalidArgument for an empty or too large size (see
    // GraphicsCapabilities::maxTextureSize), an unsupported format or a wrong amount of data.
    [[nodiscard]] virtual Result<std::unique_ptr<Texture>> createTexture(const TextureDescription& description) = 0;

    // Uploads a decoded image (straight alpha, honouring its row stride).
    [[nodiscard]] Result<std::unique_ptr<Texture>> createTexture(const Image& image, TextureSettings settings = {},
                                                                  TextureUsage usage = TextureUsage::Static)
    {
        TextureDescription description;
        description.size = image.size();
        description.format = image.format();
        description.pixels = image.pixels();
        description.rowStride = image.rowStride();
        description.alphaMode = AlphaMode::Straight;
        description.settings = settings;
        description.usage = usage;
        return createTexture(description);
    }

    // Replaces `region` of a dynamic texture. `pixels` holds region.height rows of
    // `rowStride` bytes (0 = tightly packed). Pending draws are flushed first, so draws
    // submitted earlier still see the old contents.
    virtual Status updateTexture(Texture& texture, TextureRegion region, std::span<const std::uint8_t> pixels,
                                 std::size_t rowStride = 0, AlphaMode alphaMode = AlphaMode::Straight) = 0;

    // --- Render targets --------------------------------------------------------------------
    [[nodiscard]] virtual Result<std::unique_ptr<RenderTarget>> createRenderTarget(
        const RenderTargetDescription& description) = 0;

    // Redirects drawing into `target` until the matching endRenderTarget(). Calls nest; the
    // view is reset to the identity (target pixels) and restored by endRenderTarget(). Every
    // beginRenderTarget() must be ended within the same frame. A target's texture must not be
    // drawn while that target is being rendered into.
    virtual void beginRenderTarget(RenderTarget& target) = 0;
    virtual void endRenderTarget() = 0;

    // --- Statistics ------------------------------------------------------------------------
    // Counters of the last completed frame.
    [[nodiscard]] virtual RenderStatistics lastFrameStatistics() const = 0;
};

} // namespace schuimm
