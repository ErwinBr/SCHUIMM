#pragma once

#include "Core/Common/Error.h"
#include "Core/Common/Geometry.h"
#include "Core/Image/Image.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace schuimm {

enum class TextureFilter {
    Nearest, // Crisp pixel art.
    Linear,  // Smoothly scaled artwork.
};

// How texture coordinates outside [0, 1] are resolved.
enum class TextureWrap {
    ClampToEdge,
    Repeat,
    MirroredRepeat,
};

// Sampling state of one texture. Filtering and wrapping are per texture, not global.
struct TextureSettings {
    TextureFilter filter = TextureFilter::Nearest;
    TextureWrap wrapU = TextureWrap::ClampToEdge;
    TextureWrap wrapV = TextureWrap::ClampToEdge;

    friend constexpr bool operator==(const TextureSettings&, const TextureSettings&) noexcept = default;
};

[[nodiscard]] const char* toString(TextureFilter filter) noexcept;
[[nodiscard]] const char* toString(TextureWrap wrap) noexcept;

// Static textures are immutable after creation. Dynamic textures accept partial updates
// (Renderer2D::updateTexture), e.g. for glyph atlases.
enum class TextureUsage {
    Static,
    Dynamic,
};

// Alpha representation of pixel data handed to the renderer. Images (Core/Image) use straight
// alpha. GPU textures are always stored premultiplied; straight data is converted on upload.
enum class AlphaMode {
    Straight,
    Premultiplied,
};

struct TextureDescription {
    Extent2D size;
    PixelFormat format = PixelFormat::RGBA8;
    // Rows top to bottom, `rowStride` bytes apart (0 means tightly packed). May be empty for
    // a dynamic texture, which then starts fully transparent.
    std::span<const std::uint8_t> pixels;
    std::size_t rowStride = 0;
    AlphaMode alphaMode = AlphaMode::Straight;
    TextureSettings settings;
    TextureUsage usage = TextureUsage::Static;
};

// Integer rectangle in texels, origin top-left.
struct TextureRegion {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    friend constexpr bool operator==(const TextureRegion&, const TextureRegion&) noexcept = default;
};

// Validation shared by every Renderer2D backend. Both are overflow-safe for any input.
//
// validatePixelLayout: `byteCount` bytes of RGBA8 rows for a `size` image, `rowStride` bytes
// apart (0 means tightly packed), must cover stride * (height - 1) + width * 4 bytes. Once it
// succeeds, stride * y + width * 4 for every row y < height fits in std::size_t and in the data.
// validateRegion: `region` must be non-empty and lie inside a texture of `size`.
// Both return InvalidArgument on failure.
[[nodiscard]] Status validatePixelLayout(Extent2D size, std::size_t byteCount, std::size_t rowStride);
[[nodiscard]] Status validateRegion(Extent2D size, TextureRegion region);

// A GPU texture. Created by a Renderer2D, owned through std::unique_ptr or std::shared_ptr
// (RAII) and destroyed before the GraphicsDevice that created it. The backend object (e.g. an
// OpenGL texture name) is never exposed.
//
// Texture coordinates: (0, 0) is the top-left texel corner, for uploaded images and render
// target contents alike. Texels are premultiplied RGBA8.
class Texture {
public:
    virtual ~Texture() = default;
    [[nodiscard]] virtual Extent2D size() const = 0;
    [[nodiscard]] virtual PixelFormat format() const = 0;
    [[nodiscard]] virtual TextureSettings settings() const = 0;
    [[nodiscard]] virtual TextureUsage usage() const = 0;
};

} // namespace schuimm
