#pragma once

#include "Graphics/Common/SpriteBatcher.h"
#include "Graphics/Interface/Renderer2D.h"
#include "Graphics/OpenGL/GLObjects.h"

#include <memory>
#include <vector>

namespace schuimm::gl {

class GLTexture final : public Texture {
public:
    // `liveCounter` (may be null) is incremented for the texture's lifetime.
    GLTexture(TextureHandle handle, Extent2D size, TextureSettings settings, TextureUsage usage, int* liveCounter);
    ~GLTexture() override;

    GLTexture(const GLTexture&) = delete;
    GLTexture& operator=(const GLTexture&) = delete;

    [[nodiscard]] Extent2D size() const override { return size_; }
    [[nodiscard]] PixelFormat format() const override { return PixelFormat::RGBA8; }
    [[nodiscard]] TextureSettings settings() const override { return settings_; }
    [[nodiscard]] TextureUsage usage() const override { return usage_; }
    [[nodiscard]] GLuint id() const noexcept { return handle_.id(); }

private:
    TextureHandle handle_;
    Extent2D size_;
    TextureSettings settings_;
    TextureUsage usage_;
    int* liveCounter_;
};

class GLRenderTarget final : public RenderTarget {
public:
    GLRenderTarget(FramebufferHandle framebuffer, std::unique_ptr<GLTexture> texture, int& liveCounter);
    ~GLRenderTarget() override;

    GLRenderTarget(const GLRenderTarget&) = delete;
    GLRenderTarget& operator=(const GLRenderTarget&) = delete;

    [[nodiscard]] Extent2D size() const override { return texture_->size(); }
    [[nodiscard]] const Texture& texture() const override { return *texture_; }
    [[nodiscard]] GLuint framebuffer() const noexcept { return framebuffer_.id(); }

private:
    // The texture is the colour attachment, so it is destroyed after the framebuffer.
    std::unique_ptr<GLTexture> texture_;
    FramebufferHandle framebuffer_;
    int& liveCounter_;
};

struct GLRendererLimits {
    int maxTextureSize = 0;
    int textureSlots = 1; // texture image units used per batch
};

// Sprite-batching 2D renderer for OpenGL 3.3 core.
//
// The CPU side (ordering, batching, vertex generation) is SpriteBatcher; this class uploads
// the vertices into one streamed buffer and issues one draw call per batch, binding the
// batch's textures to units 0..N-1. The fragment shader selects the unit with a per-vertex
// slot index (GLSL 3.30 cannot index sampler arrays dynamically, so it uses a branch chain).
//
// Render state is established explicitly whenever the target changes (framebuffer, viewport,
// projection, blending); nothing relies on state left behind by earlier code.
class GLRenderer2D final : public Renderer2D {
public:
    static Result<std::unique_ptr<GLRenderer2D>> create(const GLFunctions& gl, Extent2D virtualResolution,
                                                        GLRendererLimits limits);

    ~GLRenderer2D() override;

    // Renderer2D
    [[nodiscard]] Extent2D virtualResolution() const override { return virtualResolution_; }
    [[nodiscard]] Extent2D targetSize() const override;
    [[nodiscard]] int textureSlots() const override { return batcher_.settings().textureSlots; }
    void clear(Color color) override;
    void drawSprite(const Sprite& sprite) override;
    void drawTriangle(const std::array<ColoredVertex, 3>& vertices, int layer) override;
    void flush() override;
    void setView(const Transform2D& view) override;
    [[nodiscard]] const Transform2D& view() const override { return view_; }
    using Renderer2D::createTexture;
    [[nodiscard]] Result<std::unique_ptr<Texture>> createTexture(const TextureDescription& description) override;
    Status updateTexture(Texture& texture, TextureRegion region, std::span<const std::uint8_t> pixels,
                         std::size_t rowStride, AlphaMode alphaMode) override;
    [[nodiscard]] Result<std::unique_ptr<RenderTarget>> createRenderTarget(const RenderTargetDescription& description) override;
    void beginRenderTarget(RenderTarget& target) override;
    void endRenderTarget() override;
    [[nodiscard]] RenderStatistics lastFrameStatistics() const override { return lastStatistics_; }

    // --- Device-internal frame protocol ---------------------------------------------------
    // Renders the frame into `scene` (the virtual-resolution target).
    void beginFrame(GLRenderTarget& scene);
    // Flushes the scene; every beginRenderTarget() must have been ended.
    void endFrame();
    // Draws into the window's default framebuffer (no blending), e.g. to composite the scene.
    void beginWindowPass(Extent2D framebufferSize);
    void endWindowPass();

    [[nodiscard]] int liveTextureCount() const noexcept { return liveTextures_; }
    [[nodiscard]] int liveRenderTargetCount() const noexcept { return liveRenderTargets_; }

private:
    struct TargetState {
        GLuint framebuffer = 0;
        Extent2D size;
        bool window = false; // default framebuffer: y flipped, no blending
        const GLRenderTarget* target = nullptr;
        Transform2D savedView; // view of the parent, restored on end
    };

    GLRenderer2D(const GLFunctions& gl, Extent2D virtualResolution, GLRendererLimits limits);
    Status initialise();
    void applyTarget(const TargetState& state);
    void pushTarget(const TargetState& state);
    void popTarget();
    const Texture& resolveTexture(const Sprite& sprite) const;
    void flushIfFull();
    std::span<const std::uint8_t> preparePixels(Extent2D size, std::span<const std::uint8_t> pixels,
                                                std::size_t rowStride, AlphaMode alphaMode);
    void applySampling(TextureSettings settings);
    Result<std::unique_ptr<GLTexture>> makeTexture(const TextureDescription& description, int* liveCounter);
    Status validateDescription(const TextureDescription& description) const;

    const GLFunctions& gl_;
    Extent2D virtualResolution_;
    GLRendererLimits limits_;
    SpriteBatcher batcher_;
    Transform2D view_;
    std::vector<TargetState> targets_;

    ProgramHandle program_;
    GLint projectionLocation_ = -1;
    VertexArrayHandle vertexArray_;
    BufferHandle vertexBuffer_;
    BufferHandle indexBuffer_;
    std::unique_ptr<GLTexture> whiteTexture_;
    std::vector<std::uint8_t> uploadScratch_;

    RenderStatistics statistics_;
    RenderStatistics lastStatistics_;
    int liveTextures_ = 0;
    int liveRenderTargets_ = 0;
};

} // namespace schuimm::gl
