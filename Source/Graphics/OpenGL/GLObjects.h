#pragma once

#include "Graphics/OpenGL/GLFunctions.h"

#include <string_view>
#include <utility>

namespace schuimm::gl {

// Move-only RAII owner of one OpenGL object name. The GLFunctions table (owned by the device)
// must outlive the handle, and the context must be current when the handle is destroyed.
template <typename Traits>
class Handle {
public:
    Handle() = default;
    Handle(const GLFunctions& functions, GLuint id) noexcept
        : functions_(&functions)
        , id_(id)
    {
    }
    ~Handle() { reset(); }

    Handle(Handle&& other) noexcept
        : functions_(other.functions_)
        , id_(std::exchange(other.id_, 0))
    {
    }
    Handle& operator=(Handle&& other) noexcept
    {
        if (this != &other) {
            reset();
            functions_ = other.functions_;
            id_ = std::exchange(other.id_, 0);
        }
        return *this;
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    [[nodiscard]] GLuint id() const noexcept { return id_; }
    explicit operator bool() const noexcept { return id_ != 0; }

    void reset() noexcept
    {
        if (id_ != 0 && functions_ != nullptr) {
            Traits::destroy(*functions_, id_);
        }
        id_ = 0;
    }

private:
    const GLFunctions* functions_ = nullptr;
    GLuint id_ = 0;
};

struct BufferTraits {
    static void destroy(const GLFunctions& gl, GLuint id) noexcept { gl.DeleteBuffers(1, &id); }
};
struct VertexArrayTraits {
    static void destroy(const GLFunctions& gl, GLuint id) noexcept { gl.DeleteVertexArrays(1, &id); }
};
struct TextureTraits {
    static void destroy(const GLFunctions& gl, GLuint id) noexcept { gl.DeleteTextures(1, &id); }
};
struct FramebufferTraits {
    static void destroy(const GLFunctions& gl, GLuint id) noexcept { gl.DeleteFramebuffers(1, &id); }
};
struct ShaderTraits {
    static void destroy(const GLFunctions& gl, GLuint id) noexcept { gl.DeleteShader(id); }
};
struct ProgramTraits {
    static void destroy(const GLFunctions& gl, GLuint id) noexcept { gl.DeleteProgram(id); }
};

using BufferHandle = Handle<BufferTraits>;
using VertexArrayHandle = Handle<VertexArrayTraits>;
using TextureHandle = Handle<TextureTraits>;
using FramebufferHandle = Handle<FramebufferTraits>;
using ShaderHandle = Handle<ShaderTraits>;
using ProgramHandle = Handle<ProgramTraits>;

[[nodiscard]] BufferHandle createBuffer(const GLFunctions& gl);
[[nodiscard]] VertexArrayHandle createVertexArray(const GLFunctions& gl);
[[nodiscard]] TextureHandle createTextureObject(const GLFunctions& gl);
[[nodiscard]] FramebufferHandle createFramebuffer(const GLFunctions& gl);

// Compiles and links a vertex + fragment shader program. Compile/link logs are returned in the error.
[[nodiscard]] Result<ProgramHandle> createProgram(const GLFunctions& gl, std::string_view name,
                                                  const char* vertexSource, const char* fragmentSource);

} // namespace schuimm::gl
