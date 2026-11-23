#pragma once

#include "Core/Common/Geometry.h"
#include "Graphics/Interface/Color.h"
#include "Graphics/Interface/Texture.h"

#include <algorithm>
#include <cmath>

namespace schuimm {

// The scene is rendered into an offscreen target of fixed virtual resolution and then
// scaled into the window, preserving aspect ratio (letterboxing / pillarboxing).
struct PresentationSettings {
    Extent2D virtualResolution{640, 360};
    TextureFilter scalingFilter = TextureFilter::Nearest;
    bool integerScaling = false; // Only scale by whole multiples (sharpest pixel art).
    Color borderColor = Color::black();
};

// Destination rectangle in framebuffer pixels, origin top-left.
struct PixelRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    friend constexpr bool operator==(const PixelRect&, const PixelRect&) noexcept = default;
};

[[nodiscard]] inline PixelRect computePresentationRect(Extent2D virtualResolution, Extent2D framebuffer, bool integerScaling)
{
    if (virtualResolution.isEmpty() || framebuffer.isEmpty()) {
        return {};
    }
    double scale = std::min(static_cast<double>(framebuffer.width) / virtualResolution.width,
                            static_cast<double>(framebuffer.height) / virtualResolution.height);
    if (integerScaling && scale >= 1.0) {
        scale = std::floor(scale);
    }
    const int width = std::max(1, static_cast<int>(std::lround(virtualResolution.width * scale)));
    const int height = std::max(1, static_cast<int>(std::lround(virtualResolution.height * scale)));
    return {(framebuffer.width - width) / 2, (framebuffer.height - height) / 2, width, height};
}

// Converts a window-space point (e.g. the mouse position) to virtual-resolution coordinates.
// Points in the border area map outside [0, virtualResolution).
[[nodiscard]] inline Vec2 windowToVirtual(Vec2 windowPoint, Extent2D windowSize, Extent2D framebufferSize,
                                          Extent2D virtualResolution, bool integerScaling)
{
    if (windowSize.isEmpty() || framebufferSize.isEmpty()) {
        return {};
    }
    const PixelRect rect = computePresentationRect(virtualResolution, framebufferSize, integerScaling);
    if (rect.width == 0 || rect.height == 0) {
        return {};
    }
    const float pixelX = windowPoint.x * static_cast<float>(framebufferSize.width) / static_cast<float>(windowSize.width);
    const float pixelY = windowPoint.y * static_cast<float>(framebufferSize.height) / static_cast<float>(windowSize.height);
    return {(pixelX - static_cast<float>(rect.x)) * static_cast<float>(virtualResolution.width) / static_cast<float>(rect.width),
            (pixelY - static_cast<float>(rect.y)) * static_cast<float>(virtualResolution.height) / static_cast<float>(rect.height)};
}

} // namespace schuimm
