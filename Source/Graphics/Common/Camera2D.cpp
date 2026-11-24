#include "Graphics/Common/Camera2D.h"

#include <algorithm>
#include <cmath>

namespace schuimm {

namespace {

float clampAxis(float value, float halfVisible, float boundsStart, float boundsSize)
{
    if (boundsSize <= 2.0f * halfVisible) {
        return boundsStart + boundsSize * 0.5f;
    }
    return std::clamp(value, boundsStart + halfVisible, boundsStart + boundsSize - halfVisible);
}

// Moves `value` towards zero by `deadZone` (a dead zone around the camera centre).
float applyDeadZone(float offset, float deadZone)
{
    if (offset > deadZone) {
        return offset - deadZone;
    }
    if (offset < -deadZone) {
        return offset + deadZone;
    }
    return 0.0f;
}

} // namespace

Camera2D::Camera2D(Extent2D viewport) noexcept
    : viewport_(viewport)
{
}

void Camera2D::setViewport(Extent2D viewport) noexcept
{
    viewport_ = viewport;
    applyLimits();
}

void Camera2D::setPosition(Vec2 worldCentre) noexcept
{
    target_ = worldCentre;
    applyLimits();
}

void Camera2D::follow(Vec2 target, double deltaSeconds, float smoothing) noexcept
{
    // Follow relative to the effective position so that a camera stopped by its bounds does not
    // accumulate a lag that has to be recovered later.
    const Vec2 offset{applyDeadZone(target.x - position_.x, deadZone_.x),
                      applyDeadZone(target.y - position_.y, deadZone_.y)};
    if (!(smoothing > 0.0f) || !std::isfinite(smoothing)) {
        setPosition(position_ + offset);
        return;
    }
    const double dt = std::max(0.0, deltaSeconds);
    const auto fraction = static_cast<float>(1.0 - std::exp(-static_cast<double>(smoothing) * dt));
    setPosition(position_ + offset * fraction);
}

void Camera2D::setDeadZone(Vec2 halfExtents) noexcept
{
    deadZone_ = {std::max(0.0f, halfExtents.x), std::max(0.0f, halfExtents.y)};
}

void Camera2D::setZoom(float zoom) noexcept
{
    if (!std::isfinite(zoom)) {
        return;
    }
    zoom_ = std::clamp(zoom, minZoom_, maxZoom_);
    applyLimits();
}

void Camera2D::setZoomLimits(float minZoom, float maxZoom) noexcept
{
    if (!(minZoom > 0.0f) || !(maxZoom >= minZoom) || !std::isfinite(maxZoom)) {
        return;
    }
    minZoom_ = minZoom;
    maxZoom_ = maxZoom;
    setZoom(zoom_);
}

void Camera2D::setBounds(std::optional<RectF> bounds) noexcept
{
    bounds_ = bounds;
    applyLimits();
}

void Camera2D::applyLimits() noexcept
{
    position_ = target_;
    if (!bounds_) {
        return;
    }
    const float halfWidth = static_cast<float>(viewport_.width) * 0.5f / zoom_;
    const float halfHeight = static_cast<float>(viewport_.height) * 0.5f / zoom_;
    position_.x = clampAxis(position_.x, halfWidth, bounds_->x, bounds_->width);
    position_.y = clampAxis(position_.y, halfHeight, bounds_->y, bounds_->height);
}

Transform2D Camera2D::viewTransform() const noexcept
{
    // screen = (world - position) * zoom + viewport / 2
    float tx = static_cast<float>(viewport_.width) * 0.5f - position_.x * zoom_;
    float ty = static_cast<float>(viewport_.height) * 0.5f - position_.y * zoom_;
    if (pixelSnap_) {
        tx = std::round(tx);
        ty = std::round(ty);
    }
    return Transform2D{zoom_, 0.0f, 0.0f, zoom_, tx, ty};
}

Transform2D Camera2D::inverseViewTransform() const noexcept
{
    // zoom is always positive, so the view transform is invertible.
    return viewTransform().inverse().value_or(Transform2D::identity());
}

RectF Camera2D::visibleWorldRect() const noexcept
{
    const Transform2D inverse = inverseViewTransform();
    const Vec2 topLeft = inverse.apply({0.0f, 0.0f});
    const Vec2 bottomRight = inverse.apply({static_cast<float>(viewport_.width), static_cast<float>(viewport_.height)});
    return {topLeft.x, topLeft.y, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y};
}

} // namespace schuimm
