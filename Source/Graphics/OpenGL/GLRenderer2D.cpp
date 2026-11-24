#include "Graphics/OpenGL/GLRenderer2D.h"

#include "Core/Common/Assert.h"
#include "Core/Common/StringBuilder.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace schuimm::gl {

namespace {

constexpr std::uint32_t quadsPerBatch = 4096;

// GLSL 3.30 core: valid on OpenGL 3.3+ and on macOS's 4.1 core profile.
// Positions arrive in target pixels (origin top-left); uProjection maps them to clip space:
// xy = scale, zw = offset. For off-screen targets y is not flipped, so the top row of the
// image lands in the first row of the texture (texture coordinate v = 0 is the top row).
constexpr const char* vertexShader = R"(#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
layout(location = 3) in float aSlot;
uniform vec4 uProjection;
out vec2 vTexCoord;
out vec4 vColor;
flat out int vSlot;
void main()
{
    gl_Position = vec4(aPosition * uProjection.xy + uProjection.zw, 0.0, 1.0);
    vTexCoord = aTexCoord;
    vColor = aColor;
    vSlot = int(aSlot + 0.5);
}
)";

// Sampler arrays may only be indexed with constant expressions in GLSL 3.30, hence the branch
// chain. The slot is constant per primitive and textures have no mipmaps, so sampling inside
// the branches is well defined.
std::string fragmentShaderSource(int slots)
{
    std::string source = concat("#version 330 core\n"
                                "in vec2 vTexCoord;\n"
                                "in vec4 vColor;\n"
                                "flat in int vSlot;\n"
                                "uniform sampler2D uTextures[", slots, "];\n"
                                "out vec4 fragColor;\n"
                                "vec4 sampleSlot(vec2 uv)\n"
                                "{\n");
    for (int i = 0; i + 1 < slots; ++i) {
        source += concat("    if (vSlot == ", i, ") return texture(uTextures[", i, "], uv);\n");
    }
    source += concat("    return texture(uTextures[", slots - 1, "], uv);\n"
                     "}\n"
                     "void main()\n"
                     "{\n"
                     "    fragColor = sampleSlot(vTexCoord) * vColor;\n" // both premultiplied
                     "}\n");
    return source;
}

const void* byteOffset(std::size_t offset)
{
    return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(offset));
}

GLint toGL(TextureFilter filter)
{
    return static_cast<GLint>(filter == TextureFilter::Nearest ? NEAREST : LINEAR);
}

GLint toGL(TextureWrap wrap)
{
    switch (wrap) {
    case TextureWrap::ClampToEdge: return static_cast<GLint>(CLAMP_TO_EDGE);
    case TextureWrap::Repeat: return static_cast<GLint>(REPEAT);
    case TextureWrap::MirroredRepeat: return static_cast<GLint>(MIRRORED_REPEAT);
    }
    return static_cast<GLint>(CLAMP_TO_EDGE);
}

std::string describeSize(Extent2D size)
{
    return concat(size.width, "x", size.height);
}

} // namespace

// --- GLTexture / GLRenderTarget ----------------------------------------------------------

GLTexture::GLTexture(TextureHandle handle, Extent2D size, TextureSettings settings, TextureUsage usage, int* liveCounter)
    : handle_(std::move(handle))
    , size_(size)
    , settings_(settings)
    , usage_(usage)
    , liveCounter_(liveCounter)
{
    if (liveCounter_ != nullptr) {
        ++*liveCounter_;
    }
}

GLTexture::~GLTexture()
{
    if (liveCounter_ != nullptr) {
        --*liveCounter_;
    }
}

GLRenderTarget::GLRenderTarget(FramebufferHandle framebuffer, std::unique_ptr<GLTexture> texture, int& liveCounter)
    : texture_(std::move(texture))
    , framebuffer_(std::move(framebuffer))
    , liveCounter_(liveCounter)
{
    ++liveCounter_;
}

GLRenderTarget::~GLRenderTarget()
{
    --liveCounter_;
}

// --- GLRenderer2D ------------------------------------------------------------------------

Result<std::unique_ptr<GLRenderer2D>> GLRenderer2D::create(const GLFunctions& gl, Extent2D virtualResolution,
                                                           GLRendererLimits limits)
{
    std::unique_ptr<GLRenderer2D> renderer(new GLRenderer2D(gl, virtualResolution, limits));
    if (auto status = renderer->initialise(); !status) {
        return std::unexpected(status.error());
    }
    return renderer;
}

GLRenderer2D::GLRenderer2D(const GLFunctions& gl, Extent2D virtualResolution, GLRendererLimits limits)
    : gl_(gl)
    , virtualResolution_(virtualResolution)
    , limits_(limits)
    , batcher_(SpriteBatcher::Settings{std::clamp(limits.textureSlots, 1, maxSpriteTextureSlots), quadsPerBatch, 65536})
{
    targets_.reserve(8);
}

GLRenderer2D::~GLRenderer2D()
{
    whiteTexture_.reset();
    SCHUIMM_VERIFY(liveTextures_ == 0, "all textures must be destroyed before the renderer");
    SCHUIMM_VERIFY(liveRenderTargets_ == 0, "all render targets must be destroyed before the renderer");
}

Status GLRenderer2D::initialise()
{
    const int slots = batcher_.settings().textureSlots;
    const std::string fragmentSource = fragmentShaderSource(slots);
    auto program = createProgram(gl_, "sprite", vertexShader, fragmentSource.c_str());
    if (!program) {
        return std::unexpected(program.error());
    }
    program_ = std::move(*program);
    projectionLocation_ = gl_.GetUniformLocation(program_.id(), "uProjection");
    std::vector<GLint> units(static_cast<std::size_t>(slots));
    for (int i = 0; i < slots; ++i) {
        units[static_cast<std::size_t>(i)] = i;
    }
    gl_.UseProgram(program_.id());
    gl_.Uniform1iv(gl_.GetUniformLocation(program_.id(), "uTextures"), slots, units.data());
    gl_.UseProgram(0);

    // One streamed vertex buffer sized for a full batch and a static index buffer with the
    // two triangles of every quad.
    vertexArray_ = createVertexArray(gl_);
    vertexBuffer_ = createBuffer(gl_);
    indexBuffer_ = createBuffer(gl_);
    gl_.BindVertexArray(vertexArray_.id());
    gl_.BindBuffer(ARRAY_BUFFER, vertexBuffer_.id());
    gl_.BufferData(ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(SpriteVertex) * 4 * quadsPerBatch), nullptr, STREAM_DRAW);
    gl_.EnableVertexAttribArray(0);
    gl_.VertexAttribPointer(0, 2, FLOAT, FALSE_, sizeof(SpriteVertex), byteOffset(offsetof(SpriteVertex, x)));
    gl_.EnableVertexAttribArray(1);
    gl_.VertexAttribPointer(1, 2, FLOAT, FALSE_, sizeof(SpriteVertex), byteOffset(offsetof(SpriteVertex, u)));
    gl_.EnableVertexAttribArray(2);
    gl_.VertexAttribPointer(2, 4, UNSIGNED_BYTE, TRUE_, sizeof(SpriteVertex), byteOffset(offsetof(SpriteVertex, r)));
    gl_.EnableVertexAttribArray(3);
    gl_.VertexAttribPointer(3, 1, UNSIGNED_BYTE, FALSE_, sizeof(SpriteVertex), byteOffset(offsetof(SpriteVertex, slot)));

    std::vector<std::uint16_t> indices(static_cast<std::size_t>(quadsPerBatch) * 6);
    for (std::uint32_t quad = 0; quad < quadsPerBatch; ++quad) {
        const auto base = static_cast<std::uint16_t>(quad * 4);
        std::uint16_t* out = &indices[static_cast<std::size_t>(quad) * 6];
        out[0] = base;
        out[1] = static_cast<std::uint16_t>(base + 1);
        out[2] = static_cast<std::uint16_t>(base + 2);
        out[3] = static_cast<std::uint16_t>(base + 2);
        out[4] = static_cast<std::uint16_t>(base + 3);
        out[5] = base;
    }
    gl_.BindBuffer(ELEMENT_ARRAY_BUFFER, indexBuffer_.id()); // recorded in the VAO
    gl_.BufferData(ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint16_t)), indices.data(),
                   STATIC_DRAW);
    gl_.BindVertexArray(0);
    gl_.BindBuffer(ARRAY_BUFFER, 0);

    // Solid quads and triangles sample this texture, so they batch with textured sprites.
    // It is internal and therefore not counted as a caller-owned texture.
    const std::uint8_t white[4] = {255, 255, 255, 255};
    TextureDescription description;
    description.size = {1, 1};
    description.pixels = white;
    auto texture = makeTexture(description, nullptr);
    if (!texture) {
        return std::unexpected(texture.error());
    }
    whiteTexture_ = std::move(*texture);

    if (const GLenum error = gl_.GetError(); error != NO_ERROR_) {
        return makeError(ErrorCode::GraphicsFailure, concat("OpenGL error 0x", std::hex, error, " during renderer setup"));
    }
    return {};
}


// --- Targets -----------------------------------------------------------------------------

Extent2D GLRenderer2D::targetSize() const
{
    return targets_.empty() ? virtualResolution_ : targets_.back().size;
}

void GLRenderer2D::applyTarget(const TargetState& state)
{
    gl_.BindFramebuffer(FRAMEBUFFER, state.framebuffer);
    gl_.Viewport(0, 0, state.size.width, state.size.height);
    if (state.window) {
        // The window is presented as is: no blending against the previous contents.
        gl_.Disable(BLEND);
    } else {
        gl_.Enable(BLEND);
        // Premultiplied alpha: textures, render targets and vertex colours are premultiplied.
        gl_.BlendFunc(ONE, ONE_MINUS_SRC_ALPHA);
    }
    batcher_.setTransform(view_);
}

void GLRenderer2D::pushTarget(const TargetState& state)
{
    flush();
    TargetState entry = state;
    entry.savedView = view_;
    targets_.push_back(entry);
    view_ = Transform2D::identity();
    applyTarget(targets_.back());
    ++statistics_.renderTargetSwitches;
}

void GLRenderer2D::popTarget()
{
    SCHUIMM_VERIFY(!targets_.empty(), "render target stack underflow");
    flush();
    view_ = targets_.back().savedView;
    targets_.pop_back();
    if (!targets_.empty()) {
        applyTarget(targets_.back());
        ++statistics_.renderTargetSwitches;
    }
}

void GLRenderer2D::beginFrame(GLRenderTarget& scene)
{
    SCHUIMM_ASSERT(targets_.empty(), "beginFrame() while a frame is in progress");
    targets_.clear();
    statistics_ = {};
    view_ = Transform2D::identity();
    pushTarget({scene.framebuffer(), scene.size(), false, &scene, {}});
    statistics_.renderTargetSwitches = 0;
}

void GLRenderer2D::endFrame()
{
    SCHUIMM_VERIFY(targets_.size() == 1, "every beginRenderTarget() must be ended before the end of the frame");
    popTarget();
    lastStatistics_ = statistics_;
}

void GLRenderer2D::beginWindowPass(Extent2D framebufferSize)
{
    SCHUIMM_ASSERT(targets_.empty(), "window pass inside a frame");
    view_ = Transform2D::identity();
    pushTarget({0, framebufferSize, true, nullptr, {}});
}

void GLRenderer2D::endWindowPass()
{
    popTarget();
}

void GLRenderer2D::beginRenderTarget(RenderTarget& target)
{
    SCHUIMM_VERIFY(!targets_.empty(), "beginRenderTarget() outside a frame");
    auto& glTarget = static_cast<GLRenderTarget&>(target);
    pushTarget({glTarget.framebuffer(), glTarget.size(), false, &glTarget, {}});
}

void GLRenderer2D::endRenderTarget()
{
    SCHUIMM_VERIFY(targets_.size() > 1, "endRenderTarget() without beginRenderTarget()");
    popTarget();
}

// --- Drawing -----------------------------------------------------------------------------

void GLRenderer2D::clear(Color color)
{
    flush();
    // Target contents are premultiplied.
    const float a = std::clamp(color.a, 0.0f, 1.0f);
    gl_.ClearColor(color.r * a, color.g * a, color.b * a, a);
    gl_.Clear(COLOR_BUFFER_BIT);
}

const Texture& GLRenderer2D::resolveTexture(const Sprite& sprite) const
{
    if (sprite.texture == nullptr) {
        return *whiteTexture_;
    }
    SCHUIMM_ASSERT(targets_.empty() || targets_.back().target == nullptr ||
                       sprite.texture != &targets_.back().target->texture(),
                   "a render target's texture cannot be drawn into that same target");
    return *sprite.texture;
}

void GLRenderer2D::flushIfFull()
{
    if (batcher_.isFull()) {
        flush();
    }
}

void GLRenderer2D::drawSprite(const Sprite& sprite)
{
    batcher_.add(sprite, resolveTexture(sprite));
    ++statistics_.quads;
    flushIfFull();
}

void GLRenderer2D::drawTriangle(const std::array<ColoredVertex, 3>& vertices, int layer)
{
    batcher_.addTriangle(vertices, *whiteTexture_, layer);
    ++statistics_.quads;
    flushIfFull();
}

void GLRenderer2D::setView(const Transform2D& view)
{
    flush();
    view_ = view;
    batcher_.setTransform(view_);
}

void GLRenderer2D::flush()
{
    if (batcher_.isEmpty()) {
        return;
    }
    batcher_.build();
    ++statistics_.flushes;

    const Extent2D size = targetSize();
    const float sx = 2.0f / static_cast<float>(std::max(1, size.width));
    const float sy = 2.0f / static_cast<float>(std::max(1, size.height));
    const bool window = !targets_.empty() && targets_.back().window;

    gl_.UseProgram(program_.id());
    // Off-screen: y = 0 -> clip -1 (first texture row). Window: y = 0 -> clip +1 (top).
    gl_.Uniform4f(projectionLocation_, sx, window ? -sy : sy, -1.0f, window ? 1.0f : -1.0f);
    gl_.BindVertexArray(vertexArray_.id());
    gl_.BindBuffer(ARRAY_BUFFER, vertexBuffer_.id());

    const int slots = batcher_.settings().textureSlots;
    const std::span<const SpriteVertex> vertices = batcher_.vertices();
    for (const SpriteBatch& batch : batcher_.batches()) {
        // Every sampler of the array is active in the shader, so unused slots get the white
        // texture: some drivers (Apple's) reject draws with samplers bound to no texture.
        for (int slot = 0; slot < slots; ++slot) {
            // Every Texture handed out by this renderer is a GLTexture.
            const auto* texture = slot < batch.textureCount
                                      ? static_cast<const GLTexture*>(batch.textures[static_cast<std::size_t>(slot)])
                                      : whiteTexture_.get();
            gl_.ActiveTexture(TEXTURE0 + static_cast<GLenum>(slot));
            gl_.BindTexture(TEXTURE_2D, texture->id());
        }
        const std::size_t first = static_cast<std::size_t>(batch.firstQuad) * 4;
        const std::size_t count = static_cast<std::size_t>(batch.quadCount) * 4;
        // Orphan the previous storage so the driver does not wait for draws still using it.
        gl_.BufferData(ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(SpriteVertex) * 4 * quadsPerBatch), nullptr, STREAM_DRAW);
        gl_.BufferSubData(ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(count * sizeof(SpriteVertex)), vertices.data() + first);
        gl_.DrawElements(TRIANGLES, static_cast<GLsizei>(batch.quadCount * 6), UNSIGNED_SHORT, nullptr);
        ++statistics_.drawCalls;
    }

    gl_.BindVertexArray(0);
    gl_.ActiveTexture(TEXTURE0);
    batcher_.clear();
}

// --- Textures ----------------------------------------------------------------------------

Status GLRenderer2D::validateDescription(const TextureDescription& description) const
{
    const Extent2D size = description.size;
    if (size.isEmpty() || size.width > limits_.maxTextureSize || size.height > limits_.maxTextureSize) {
        return makeError(ErrorCode::InvalidArgument, concat("invalid texture size ", describeSize(size),
                                                            " (must be 1..", limits_.maxTextureSize, " per side)"));
    }
    if (description.format != PixelFormat::RGBA8) {
        return makeError(ErrorCode::Unsupported, concat("unsupported texture format ", toString(description.format)));
    }
    const std::size_t rowBytes = static_cast<std::size_t>(size.width) * 4;
    const std::size_t stride = description.rowStride == 0 ? rowBytes : description.rowStride;
    if (stride < rowBytes) {
        return makeError(ErrorCode::InvalidArgument, concat("texture row stride ", stride, " is smaller than a row (", rowBytes, " bytes)"));
    }
    if (description.pixels.empty()) {
        if (description.usage == TextureUsage::Static) {
            return makeError(ErrorCode::InvalidArgument, "a static texture needs pixel data");
        }
        return {};
    }
    const std::size_t expected = stride * static_cast<std::size_t>(size.height - 1) + rowBytes;
    if (description.pixels.size() < expected) {
        return makeError(ErrorCode::InvalidArgument, concat("texture ", describeSize(size), " expects at least ", expected,
                                                            " bytes of RGBA8 data, got ", description.pixels.size()));
    }
    return {};
}

std::span<const std::uint8_t> GLRenderer2D::preparePixels(Extent2D size, std::span<const std::uint8_t> pixels,
                                                          std::size_t rowStride, AlphaMode alphaMode)
{
    const std::size_t rowBytes = static_cast<std::size_t>(size.width) * 4;
    const std::size_t stride = rowStride == 0 ? rowBytes : rowStride;
    if (alphaMode == AlphaMode::Premultiplied && stride == rowBytes) {
        return pixels.first(rowBytes * static_cast<std::size_t>(size.height));
    }
    // Repack tightly and premultiply. The scratch buffer is reused between uploads.
    uploadScratch_.resize(rowBytes * static_cast<std::size_t>(size.height));
    for (int y = 0; y < size.height; ++y) {
        const std::uint8_t* in = pixels.data() + stride * static_cast<std::size_t>(y);
        std::uint8_t* out = uploadScratch_.data() + rowBytes * static_cast<std::size_t>(y);
        if (alphaMode == AlphaMode::Premultiplied) {
            std::copy(in, in + rowBytes, out);
            continue;
        }
        for (std::size_t x = 0; x < rowBytes; x += 4) {
            const unsigned alpha = in[x + 3];
            // Rounded (c * a) / 255.
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const unsigned value = in[x + channel] * alpha + 128u;
                out[x + channel] = static_cast<std::uint8_t>((value + (value >> 8)) >> 8);
            }
            out[x + 3] = static_cast<std::uint8_t>(alpha);
        }
    }
    return uploadScratch_;
}

void GLRenderer2D::applySampling(TextureSettings settings)
{
    gl_.TexParameteri(TEXTURE_2D, TEXTURE_MIN_FILTER, toGL(settings.filter));
    gl_.TexParameteri(TEXTURE_2D, TEXTURE_MAG_FILTER, toGL(settings.filter));
    gl_.TexParameteri(TEXTURE_2D, TEXTURE_WRAP_S, toGL(settings.wrapU));
    gl_.TexParameteri(TEXTURE_2D, TEXTURE_WRAP_T, toGL(settings.wrapV));
}

Result<std::unique_ptr<GLTexture>> GLRenderer2D::makeTexture(const TextureDescription& description, int* liveCounter)
{
    if (auto status = validateDescription(description); !status) {
        return std::unexpected(status.error());
    }
    const Extent2D size = description.size;
    std::span<const std::uint8_t> data;
    if (!description.pixels.empty()) {
        data = preparePixels(size, description.pixels, description.rowStride, description.alphaMode);
    } else {
        // Dynamic textures without initial data start transparent, not undefined.
        uploadScratch_.assign(static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height) * 4, 0);
        data = uploadScratch_;
    }

    TextureHandle handle = createTextureObject(gl_);
    gl_.ActiveTexture(TEXTURE0);
    gl_.BindTexture(TEXTURE_2D, handle.id());
    applySampling(description.settings);
    gl_.PixelStorei(UNPACK_ALIGNMENT, 1);
    // Row 0 of the data lands at t = 0, so "v = 0 is the top row" holds for uploaded images.
    gl_.TexImage2D(TEXTURE_2D, 0, static_cast<GLint>(RGBA8), size.width, size.height, 0, RGBA, UNSIGNED_BYTE, data.data());
    gl_.BindTexture(TEXTURE_2D, 0);

    if (const GLenum error = gl_.GetError(); error != NO_ERROR_) {
        return makeError(ErrorCode::GraphicsFailure,
                         concat("OpenGL error 0x", std::hex, error, std::dec, " while creating a ", describeSize(size), " texture"));
    }
    return std::make_unique<GLTexture>(std::move(handle), size, description.settings, description.usage, liveCounter);
}

Result<std::unique_ptr<Texture>> GLRenderer2D::createTexture(const TextureDescription& description)
{
    auto texture = makeTexture(description, &liveTextures_);
    if (!texture) {
        return std::unexpected(texture.error());
    }
    return std::unique_ptr<Texture>(std::move(*texture));
}

Status GLRenderer2D::updateTexture(Texture& texture, TextureRegion region, std::span<const std::uint8_t> pixels,
                                   std::size_t rowStride, AlphaMode alphaMode)
{
    auto& glTexture = static_cast<GLTexture&>(texture);
    if (glTexture.usage() != TextureUsage::Dynamic) {
        return makeError(ErrorCode::InvalidState, "only dynamic textures can be updated");
    }
    const Extent2D size = glTexture.size();
    if (region.width <= 0 || region.height <= 0 || region.x < 0 || region.y < 0 || region.x + region.width > size.width ||
        region.y + region.height > size.height) {
        return makeError(ErrorCode::InvalidArgument,
                         concat("update region ", region.x, ",", region.y, " ", region.width, "x", region.height,
                                " is outside the ", describeSize(size), " texture"));
    }
    const std::size_t rowBytes = static_cast<std::size_t>(region.width) * 4;
    const std::size_t stride = rowStride == 0 ? rowBytes : rowStride;
    if (stride < rowBytes || pixels.size() < stride * static_cast<std::size_t>(region.height - 1) + rowBytes) {
        return makeError(ErrorCode::InvalidArgument, concat("texture update expects ", region.height, " rows of ",
                                                            rowBytes, " bytes, got ", pixels.size(), " bytes"));
    }

    // Draws already submitted must see the old contents.
    flush();
    const std::span<const std::uint8_t> data = preparePixels({region.width, region.height}, pixels, rowStride, alphaMode);
    gl_.ActiveTexture(TEXTURE0);
    gl_.BindTexture(TEXTURE_2D, glTexture.id());
    gl_.PixelStorei(UNPACK_ALIGNMENT, 1);
    gl_.TexSubImage2D(TEXTURE_2D, 0, region.x, region.y, region.width, region.height, RGBA, UNSIGNED_BYTE, data.data());
    gl_.BindTexture(TEXTURE_2D, 0);
    if (const GLenum error = gl_.GetError(); error != NO_ERROR_) {
        return makeError(ErrorCode::GraphicsFailure, concat("OpenGL error 0x", std::hex, error, " while updating a texture"));
    }
    return {};
}

// --- Render targets ----------------------------------------------------------------------

Result<std::unique_ptr<RenderTarget>> GLRenderer2D::createRenderTarget(const RenderTargetDescription& description)
{
    const Extent2D size = description.size;
    if (size.isEmpty() || size.width > limits_.maxTextureSize || size.height > limits_.maxTextureSize) {
        return makeError(ErrorCode::InvalidArgument, concat("invalid render target size ", describeSize(size),
                                                            " (must be 1..", limits_.maxTextureSize, " per side)"));
    }

    TextureHandle textureHandle = createTextureObject(gl_);
    gl_.ActiveTexture(TEXTURE0);
    gl_.BindTexture(TEXTURE_2D, textureHandle.id());
    applySampling(description.settings);
    gl_.TexImage2D(TEXTURE_2D, 0, static_cast<GLint>(RGBA8), size.width, size.height, 0, RGBA, UNSIGNED_BYTE, nullptr);
    gl_.BindTexture(TEXTURE_2D, 0);

    FramebufferHandle framebuffer = createFramebuffer(gl_);
    gl_.BindFramebuffer(FRAMEBUFFER, framebuffer.id());
    gl_.FramebufferTexture2D(FRAMEBUFFER, COLOR_ATTACHMENT0, TEXTURE_2D, textureHandle.id(), 0);
    const GLenum status = gl_.CheckFramebufferStatus(FRAMEBUFFER);
    if (status == FRAMEBUFFER_COMPLETE) {
        // Start fully transparent rather than undefined.
        gl_.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        gl_.Clear(COLOR_BUFFER_BIT);
    }
    // Re-establish the framebuffer of the target being rendered, if any.
    gl_.BindFramebuffer(FRAMEBUFFER, targets_.empty() ? 0 : targets_.back().framebuffer);

    if (status != FRAMEBUFFER_COMPLETE) {
        return makeError(ErrorCode::GraphicsFailure,
                         concat("render target ", describeSize(size), " framebuffer incomplete (status 0x", std::hex, status, ")"));
    }
    if (const GLenum error = gl_.GetError(); error != NO_ERROR_) {
        return makeError(ErrorCode::GraphicsFailure,
                         concat("OpenGL error 0x", std::hex, error, std::dec, " while creating a ", describeSize(size), " render target"));
    }
    auto texture = std::make_unique<GLTexture>(std::move(textureHandle), size, description.settings, TextureUsage::Static, nullptr);
    return std::unique_ptr<RenderTarget>(std::make_unique<GLRenderTarget>(std::move(framebuffer), std::move(texture), liveRenderTargets_));
}

} // namespace schuimm::gl
