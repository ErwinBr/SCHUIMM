#pragma once

// CPU-side image data: the decoded form of an image asset, independent of any graphics API.
//
// It lives in Core because both the resource layer (which decodes image files into it) and the
// graphics layer (which reads it back from the GPU today and will upload it as a texture in
// Milestone 2) use it, and neither should depend on the other.

#include "Core/Common/Error.h"
#include "Core/Common/Geometry.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace schuimm {

enum class PixelFormat {
    // 4 bytes per pixel in the order R, G, B, A; 8 bits per channel; colour values are
    // sRGB-encoded; alpha is straight (NOT premultiplied): a pixel with alpha 0 still carries
    // its colour.
    RGBA8,
};

[[nodiscard]] constexpr std::size_t bytesPerPixel(PixelFormat format) noexcept
{
    switch (format) {
    case PixelFormat::RGBA8: return 4;
    }
    return 0;
}

[[nodiscard]] const char* toString(PixelFormat format) noexcept;

// Owns the pixel memory of one 2D image. Rows are stored top to bottom; row y starts at byte
// y * rowStride(). Images created by this class are tightly packed
// (rowStride() == width * bytesPerPixel()), which is what Renderer2D::createTexture expects;
// consumers should nevertheless honour rowStride() so padded layouts can be added later.
//
// An Image is a value: copyable (deep copy) and cheaply movable. A default-constructed Image is
// empty (0x0, no pixels).
class Image {
public:
    // Largest accepted width or height. Guards against absurd allocations from corrupt headers.
    static constexpr int maxDimension = 16384;

    Image() = default;

    // A zero-initialised (fully transparent black) image. Fails with InvalidArgument if a
    // dimension is not positive or exceeds maxDimension.
    [[nodiscard]] static Result<Image> create(Extent2D size, PixelFormat format = PixelFormat::RGBA8);

    [[nodiscard]] Extent2D size() const noexcept { return size_; }
    [[nodiscard]] int width() const noexcept { return size_.width; }
    [[nodiscard]] int height() const noexcept { return size_.height; }
    [[nodiscard]] PixelFormat format() const noexcept { return format_; }
    [[nodiscard]] std::size_t rowStride() const noexcept { return rowStride_; }
    [[nodiscard]] bool isEmpty() const noexcept { return pixels_.empty(); }

    [[nodiscard]] std::span<const std::uint8_t> pixels() const noexcept { return pixels_; }
    [[nodiscard]] std::span<std::uint8_t> pixels() noexcept { return pixels_; }

    // The bytes of one row (width * bytesPerPixel), y in [0, height).
    [[nodiscard]] std::span<const std::uint8_t> row(int y) const noexcept;
    [[nodiscard]] std::span<std::uint8_t> row(int y) noexcept;

    // The bytes of one pixel (bytesPerPixel), x in [0, width), y in [0, height).
    [[nodiscard]] std::span<const std::uint8_t> pixel(int x, int y) const noexcept;
    [[nodiscard]] std::span<std::uint8_t> pixel(int x, int y) noexcept;

private:
    Image(Extent2D size, PixelFormat format, std::size_t rowStride);

    Extent2D size_{};
    PixelFormat format_ = PixelFormat::RGBA8;
    std::size_t rowStride_ = 0;
    std::vector<std::uint8_t> pixels_;
};

} // namespace schuimm
