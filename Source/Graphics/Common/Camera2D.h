#pragma once

#include "Core/Common/Geometry.h"
#include "Core/Common/Transform2D.h"

#include <optional>

namespace schuimm {

// A 2D camera: describes which part of the world is visible and how it maps onto a render
// target. It is independent of the graphics backend; the renderer only consumes the resulting
// transform (Renderer2D::setView(camera.viewTransform())).
//
// Coordinate systems (see Documentation/Architecture/Graphics.md):
//   world   game units; at zoom 1 one world unit is one screen pixel.
//   screen  pixels of the render target the camera draws into (normally the virtual
//           resolution), origin top-left, y down. The viewport is the size of that area.
//
// position() is the world point shown at the centre of the viewport. Zoom > 1 magnifies.
// The camera does not rotate.
class Camera2D {
public:
    Camera2D() = default;
    explicit Camera2D(Extent2D viewport) noexcept;

    // --- Viewport --------------------------------------------------------------------------
    void setViewport(Extent2D viewport) noexcept;
    [[nodiscard]] Extent2D viewport() const noexcept { return viewport_; }

    // --- Position --------------------------------------------------------------------------
    // Positions are clamped to the bounds, if set.
    void setPosition(Vec2 worldCentre) noexcept;
    void move(Vec2 worldDelta) noexcept { setPosition(position_ + worldDelta); }
    [[nodiscard]] Vec2 position() const noexcept { return position_; }

    // Moves towards `target` with exponential smoothing: after `deltaSeconds` the remaining
    // distance is multiplied by exp(-smoothing * deltaSeconds), so the motion does not depend
    // on the frame rate. smoothing <= 0 (or a non-finite value) snaps to the target.
    // Offsets smaller than the dead zone (half extents, world units) are ignored.
    void follow(Vec2 target, double deltaSeconds, float smoothing) noexcept;
    void setDeadZone(Vec2 halfExtents) noexcept;
    [[nodiscard]] Vec2 deadZone() const noexcept { return deadZone_; }

    // --- Zoom ------------------------------------------------------------------------------
    // Clamped to [minZoom, maxZoom]; both limits must be positive.
    void setZoom(float zoom) noexcept;
    [[nodiscard]] float zoom() const noexcept { return zoom_; }
    void setZoomLimits(float minZoom, float maxZoom) noexcept;
    [[nodiscard]] float minZoom() const noexcept { return minZoom_; }
    [[nodiscard]] float maxZoom() const noexcept { return maxZoom_; }

    // --- Limits ----------------------------------------------------------------------------
    // Keeps the visible area inside `bounds` (world units). If the bounds are smaller than
    // the visible area along an axis, the camera is centred on them along that axis.
    void setBounds(std::optional<RectF> bounds) noexcept;
    [[nodiscard]] const std::optional<RectF>& bounds() const noexcept { return bounds_; }

    // Rounds the screen-space translation to whole pixels, which keeps nearest-filtered pixel
    // art from shimmering while the camera moves. Off by default.
    void setPixelSnap(bool enabled) noexcept { pixelSnap_ = enabled; }
    [[nodiscard]] bool pixelSnap() const noexcept { return pixelSnap_; }

    // --- Transformations -------------------------------------------------------------------
    // world -> screen
    [[nodiscard]] Transform2D viewTransform() const noexcept;
    // screen -> world
    [[nodiscard]] Transform2D inverseViewTransform() const noexcept;
    [[nodiscard]] Vec2 worldToScreen(Vec2 world) const noexcept { return viewTransform().apply(world); }
    [[nodiscard]] Vec2 screenToWorld(Vec2 screen) const noexcept { return inverseViewTransform().apply(screen); }

    // The world rectangle currently visible in the viewport.
    [[nodiscard]] RectF visibleWorldRect() const noexcept;

private:
    void applyLimits() noexcept;

    Extent2D viewport_{};
    Vec2 target_{};   // requested position, before clamping
    Vec2 position_{}; // effective position
    float zoom_ = 1.0f;
    float minZoom_ = 0.05f;
    float maxZoom_ = 64.0f;
    Vec2 deadZone_{};
    std::optional<RectF> bounds_;
    bool pixelSnap_ = false;
};

} // namespace schuimm
