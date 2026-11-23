#include "Graphics/OpenGL/GLRenderer2D.h"

#include "Core/Common/Assert.h"
#include "Core/Common/StringBuilder.h"

#include <cstdint>

namespace schuimm::gl {

namespace {

// GLSL 3.30 core: valid on OpenGL 3.3+ and on macOS's 4.1 core profile.
// Positions are in target pixels with origin top-left; the vertex shader converts to clip space.
constexpr const char* colorVertexShader = R"(#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aColor;
uniform vec2 uTargetSize;
out vec4 vColor;
void main()
{
    vec2 ndc = vec2(aPosition.x / uTargetSize.x * 2.0 - 1.0, 1.0 - aPosition.y / uTargetSize.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vColor = aColor;
}
)";

constexpr const char* colorFragmentShader = R"(#version 330 core
in vec4 vColor;
out vec4 fragColor;
void main()
{
    fragColor = vColor;
}
)";

constexpr const char* textureVertexShader = R"(#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
uniform vec2 uTargetSize;
out vec2 vTexCoord;
out vec4 vColor;
void main()
{
    vec2 ndc = vec2(aPosition.x / uTargetSize.x * 2.0 - 1.0, 1.0 - aPosition.y / uTargetSize.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vTexCoord = aTexCoord;
    vColor = aColor;
}
)";

constexpr const char* textureFragmentShader = R"(#version 330 core
in vec2 vTexCoord;
in vec4 vColor;
uniform sampler2D uTexture;
out vec4 fragColor;
void main()
{
    fragColor = texture(uTexture, vTexCoord) * vColor;
}
)";

struct TexturedVertex {
    float x, y;
    float u, v;
    float r, g, b, a;
};

struct PackedColoredVertex {
    float x, y;
    float r, g, b, a;
};

const void* byteOffset(std::size_t offset)
{
    return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(offset));
}

} // namespace

GLTexture::GLTexture(TextureHandle handle, Extent2D size, int& liveCounter)
    : handle_(std::move(handle))
    , size_(size)
    , liveCounter_(liveCounter)
{
    ++liveCounter_;
}

GLTexture::~GLTexture()
{
    --liveCounter_;
}

Result<std::unique_ptr<GLRenderer2D>> GLRenderer2D::create(const GLFunctions& gl, Extent2D virtualResolution)
{
    std::unique_ptr<GLRenderer2D> renderer(new GLRenderer2D(gl, virtualResolution));
    if (auto status = renderer->initialise(); !status) {
        return std::unexpected(status.error());
    }
    return renderer;
}

GLRenderer2D::GLRenderer2D(const GLFunctions& gl, Extent2D virtualResolution)
    : gl_(gl)
    , virtualResolution_(virtualResolution)
    , targetSize_(virtualResolution)
{
}

GLRenderer2D::~GLRenderer2D()
{
    SCHUIMM_VERIFY(liveTextures_ == 0, "all textures must be destroyed before the renderer");
}

Status GLRenderer2D::initialise()
{
    auto colorProgram = createProgram(gl_, "color", colorVertexShader, colorFragmentShader);
    if (!colorProgram) {
        return std::unexpected(colorProgram.error());
    }
    colorProgram_ = std::move(*colorProgram);
    colorTargetSizeLocation_ = gl_.GetUniformLocation(colorProgram_.id(), "uTargetSize");

    auto textureProgram = createProgram(gl_, "texture", textureVertexShader, textureFragmentShader);
    if (!textureProgram) {
        return std::unexpected(textureProgram.error());
    }
    textureProgram_ = std::move(*textureProgram);
    textureTargetSizeLocation_ = gl_.GetUniformLocation(textureProgram_.id(), "uTargetSize");
    gl_.UseProgram(textureProgram_.id());
    gl_.Uniform1i(gl_.GetUniformLocation(textureProgram_.id(), "uTexture"), 0);
    gl_.UseProgram(0);

    // Colored triangles: streamed VBO of three vertices.
    colorVertexArray_ = createVertexArray(gl_);
    colorVertexBuffer_ = createBuffer(gl_);
    gl_.BindVertexArray(colorVertexArray_.id());
    gl_.BindBuffer(ARRAY_BUFFER, colorVertexBuffer_.id());
    gl_.BufferData(ARRAY_BUFFER, sizeof(PackedColoredVertex) * 3, nullptr, STREAM_DRAW);
    gl_.EnableVertexAttribArray(0);
    gl_.VertexAttribPointer(0, 2, FLOAT, FALSE_, sizeof(PackedColoredVertex), byteOffset(offsetof(PackedColoredVertex, x)));
    gl_.EnableVertexAttribArray(1);
    gl_.VertexAttribPointer(1, 4, FLOAT, FALSE_, sizeof(PackedColoredVertex), byteOffset(offsetof(PackedColoredVertex, r)));

    // Textured quads: streamed VBO of four vertices plus a static index buffer (EBO).
    quadVertexArray_ = createVertexArray(gl_);
    quadVertexBuffer_ = createBuffer(gl_);
    quadIndexBuffer_ = createBuffer(gl_);
    gl_.BindVertexArray(quadVertexArray_.id());
    gl_.BindBuffer(ARRAY_BUFFER, quadVertexBuffer_.id());
    gl_.BufferData(ARRAY_BUFFER, sizeof(TexturedVertex) * 4, nullptr, STREAM_DRAW);
    gl_.EnableVertexAttribArray(0);
    gl_.VertexAttribPointer(0, 2, FLOAT, FALSE_, sizeof(TexturedVertex), byteOffset(offsetof(TexturedVertex, x)));
    gl_.EnableVertexAttribArray(1);
    gl_.VertexAttribPointer(1, 2, FLOAT, FALSE_, sizeof(TexturedVertex), byteOffset(offsetof(TexturedVertex, u)));
    gl_.EnableVertexAttribArray(2);
    gl_.VertexAttribPointer(2, 4, FLOAT, FALSE_, sizeof(TexturedVertex), byteOffset(offsetof(TexturedVertex, r)));
    const std::uint16_t indices[] = {0, 1, 2, 2, 3, 0};
    gl_.BindBuffer(ELEMENT_ARRAY_BUFFER, quadIndexBuffer_.id()); // recorded in the VAO
    gl_.BufferData(ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, STATIC_DRAW);

    gl_.BindVertexArray(0);
    gl_.BindBuffer(ARRAY_BUFFER, 0);

    if (const GLenum error = gl_.GetError(); error != NO_ERROR_) {
        return makeError(ErrorCode::GraphicsFailure, concat("OpenGL error 0x", std::hex, error, " during renderer setup"));
    }
    return {};
}

void GLRenderer2D::clear(Color color)
{
    gl_.ClearColor(color.r, color.g, color.b, color.a);
    gl_.Clear(COLOR_BUFFER_BIT);
}

void GLRenderer2D::drawTriangle(const std::array<ColoredVertex, 3>& vertices)
{
    PackedColoredVertex packed[3];
    for (std::size_t i = 0; i < 3; ++i) {
        const ColoredVertex& v = vertices[i];
        packed[i] = {v.position.x, v.position.y, v.color.r, v.color.g, v.color.b, v.color.a};
    }
    gl_.UseProgram(colorProgram_.id());
    gl_.Uniform2f(colorTargetSizeLocation_, static_cast<float>(targetSize_.width), static_cast<float>(targetSize_.height));
    gl_.BindVertexArray(colorVertexArray_.id());
    gl_.BindBuffer(ARRAY_BUFFER, colorVertexBuffer_.id());
    gl_.BufferData(ARRAY_BUFFER, sizeof(packed), nullptr, STREAM_DRAW); // orphan the previous storage
    gl_.BufferSubData(ARRAY_BUFFER, 0, sizeof(packed), packed);
    gl_.DrawArrays(TRIANGLES, 0, 3);
    gl_.BindVertexArray(0);
}

void GLRenderer2D::drawTexture(const Texture& texture, const RectF& destination, const RectF& source, Color tint)
{
    // Every Texture handed out by this renderer is a GLTexture.
    const auto& glTexture = static_cast<const GLTexture&>(texture);
    drawTextureId(glTexture.id(), destination, source, tint);
}

void GLRenderer2D::drawTextureId(GLuint texture, const RectF& d, const RectF& s, Color c)
{
    const float u0 = s.x;
    const float v0 = s.y;
    const float u1 = s.x + s.width;
    const float v1 = s.y + s.height;
    const TexturedVertex vertices[4] = {
        {d.x, d.y, u0, v0, c.r, c.g, c.b, c.a},
        {d.x + d.width, d.y, u1, v0, c.r, c.g, c.b, c.a},
        {d.x + d.width, d.y + d.height, u1, v1, c.r, c.g, c.b, c.a},
        {d.x, d.y + d.height, u0, v1, c.r, c.g, c.b, c.a},
    };

    gl_.UseProgram(textureProgram_.id());
    gl_.Uniform2f(textureTargetSizeLocation_, static_cast<float>(targetSize_.width), static_cast<float>(targetSize_.height));
    gl_.ActiveTexture(TEXTURE0);
    gl_.BindTexture(TEXTURE_2D, texture);
    gl_.BindVertexArray(quadVertexArray_.id());
    gl_.BindBuffer(ARRAY_BUFFER, quadVertexBuffer_.id());
    gl_.BufferData(ARRAY_BUFFER, sizeof(vertices), nullptr, STREAM_DRAW);
    gl_.BufferSubData(ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    gl_.DrawElements(TRIANGLES, 6, UNSIGNED_SHORT, nullptr);
    gl_.BindVertexArray(0);
}

Result<std::unique_ptr<Texture>> GLRenderer2D::createTexture(const TextureDescription& description)
{
    const Extent2D size = description.size;
    if (size.isEmpty()) {
        return makeError(ErrorCode::InvalidArgument, "texture size must be positive");
    }
    const std::size_t expectedBytes = static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height) * 4;
    if (description.rgbaPixels.size() != expectedBytes) {
        return makeError(ErrorCode::InvalidArgument,
            concat("texture expects ", expectedBytes, " bytes of RGBA data, got ", description.rgbaPixels.size()));
    }

    TextureHandle handle = createTextureObject(gl_);
    const GLint filter = description.filter == TextureFilter::Nearest ? static_cast<GLint>(NEAREST) : static_cast<GLint>(LINEAR);
    gl_.ActiveTexture(TEXTURE0);
    gl_.BindTexture(TEXTURE_2D, handle.id());
    gl_.TexParameteri(TEXTURE_2D, TEXTURE_MIN_FILTER, filter);
    gl_.TexParameteri(TEXTURE_2D, TEXTURE_MAG_FILTER, filter);
    gl_.TexParameteri(TEXTURE_2D, TEXTURE_WRAP_S, static_cast<GLint>(CLAMP_TO_EDGE));
    gl_.TexParameteri(TEXTURE_2D, TEXTURE_WRAP_T, static_cast<GLint>(CLAMP_TO_EDGE));
    gl_.PixelStorei(UNPACK_ALIGNMENT, 1);
    // Row 0 of the data lands at t = 0, so "v = 0 is the top row" holds for uploaded images.
    gl_.TexImage2D(TEXTURE_2D, 0, static_cast<GLint>(RGBA8), size.width, size.height, 0, RGBA, UNSIGNED_BYTE,
                   description.rgbaPixels.data());
    gl_.BindTexture(TEXTURE_2D, 0);

    if (const GLenum error = gl_.GetError(); error != NO_ERROR_) {
        return makeError(ErrorCode::GraphicsFailure, concat("OpenGL error 0x", std::hex, error, " while creating a texture"));
    }
    return std::unique_ptr<Texture>(std::make_unique<GLTexture>(std::move(handle), size, liveTextures_));
}

} // namespace schuimm::gl
