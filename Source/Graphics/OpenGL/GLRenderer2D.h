#pragma once

#include "Graphics/Interface/Renderer2D.h"
#include "Graphics/OpenGL/GLObjects.h"

#include <memory>

namespace schuimm::gl {

class GLTexture final : public Texture {
public:
    GLTexture(TextureHandle handle, Extent2D size, int& liveCounter);
    ~GLTexture() override;

    GLTexture(const GLTexture&) = delete;
    GLTexture& operator=(const GLTexture&) = delete;

    [[nodiscard]] Extent2D size() const override { return size_; }
    [[nodiscard]] GLuint id() const noexcept { return handle_.id(); }

private:
    TextureHandle handle_;
    Extent2D size_;
    int& liveCounter_;
};

// Straightforward shader-based 2D renderer: one draw call per primitive (no batching yet).
class GLRenderer2D final : public Renderer2D {
public:
    static Result<std::unique_ptr<GLRenderer2D>> create(const GLFunctions& gl, Extent2D virtualResolution);

    ~GLRenderer2D() override;

    // Renderer2D
    [[nodiscard]] Extent2D virtualResolution() const override { return virtualResolution_; }
    void clear(Color color) override;
    void drawTriangle(const std::array<ColoredVertex, 3>& vertices) override;
    void drawTexture(const Texture& texture, const RectF& destination, const RectF& source, Color tint) override;
    [[nodiscard]] Result<std::unique_ptr<Texture>> createTexture(const TextureDescription& description) override;

    // Device-internal: the pixel size of the render target that subsequent draws map onto.
    void setTargetSize(Extent2D size) noexcept { targetSize_ = size; }
    void drawTextureId(GLuint texture, const RectF& destination, const RectF& source, Color tint);
    [[nodiscard]] int liveTextureCount() const noexcept { return liveTextures_; }

private:
    explicit GLRenderer2D(const GLFunctions& gl, Extent2D virtualResolution);
    Status initialise();

    const GLFunctions& gl_;
    Extent2D virtualResolution_;
    Extent2D targetSize_;

    ProgramHandle colorProgram_;
    GLint colorTargetSizeLocation_ = -1;
    VertexArrayHandle colorVertexArray_;
    BufferHandle colorVertexBuffer_;

    ProgramHandle textureProgram_;
    GLint textureTargetSizeLocation_ = -1;
    VertexArrayHandle quadVertexArray_;
    BufferHandle quadVertexBuffer_;
    BufferHandle quadIndexBuffer_;

    int liveTextures_ = 0;
};

} // namespace schuimm::gl
