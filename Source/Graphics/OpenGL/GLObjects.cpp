#include "Graphics/OpenGL/GLObjects.h"

#include <string>
#include <vector>

namespace schuimm::gl {

BufferHandle createBuffer(const GLFunctions& gl)
{
    GLuint id = 0;
    gl.GenBuffers(1, &id);
    return {gl, id};
}

VertexArrayHandle createVertexArray(const GLFunctions& gl)
{
    GLuint id = 0;
    gl.GenVertexArrays(1, &id);
    return {gl, id};
}

TextureHandle createTextureObject(const GLFunctions& gl)
{
    GLuint id = 0;
    gl.GenTextures(1, &id);
    return {gl, id};
}

FramebufferHandle createFramebuffer(const GLFunctions& gl)
{
    GLuint id = 0;
    gl.GenFramebuffers(1, &id);
    return {gl, id};
}

namespace {

std::string shaderLog(const GLFunctions& gl, GLuint shader)
{
    GLint length = 0;
    gl.GetShaderiv(shader, INFO_LOG_LENGTH, &length);
    if (length <= 1) {
        return {};
    }
    std::vector<GLchar> buffer(static_cast<std::size_t>(length));
    gl.GetShaderInfoLog(shader, length, nullptr, buffer.data());
    return std::string(buffer.data());
}

std::string programLog(const GLFunctions& gl, GLuint program)
{
    GLint length = 0;
    gl.GetProgramiv(program, INFO_LOG_LENGTH, &length);
    if (length <= 1) {
        return {};
    }
    std::vector<GLchar> buffer(static_cast<std::size_t>(length));
    gl.GetProgramInfoLog(program, length, nullptr, buffer.data());
    return std::string(buffer.data());
}

Result<ShaderHandle> compileShader(const GLFunctions& gl, std::string_view programName, GLenum stage, const char* source)
{
    ShaderHandle shader(gl, gl.CreateShader(stage));
    if (!shader) {
        return makeError(ErrorCode::GraphicsFailure, "glCreateShader failed");
    }
    gl.ShaderSource(shader.id(), 1, &source, nullptr);
    gl.CompileShader(shader.id());
    GLint status = 0;
    gl.GetShaderiv(shader.id(), COMPILE_STATUS, &status);
    if (status == 0) {
        return makeError(ErrorCode::ShaderCompilation,
            std::string(programName) + (stage == VERTEX_SHADER ? " vertex" : " fragment") +
                " shader failed to compile:\n" + shaderLog(gl, shader.id()));
    }
    return shader;
}

} // namespace

Result<ProgramHandle> createProgram(const GLFunctions& gl, std::string_view name,
                                    const char* vertexSource, const char* fragmentSource)
{
    auto vertex = compileShader(gl, name, VERTEX_SHADER, vertexSource);
    if (!vertex) {
        return std::unexpected(vertex.error());
    }
    auto fragment = compileShader(gl, name, FRAGMENT_SHADER, fragmentSource);
    if (!fragment) {
        return std::unexpected(fragment.error());
    }

    ProgramHandle program(gl, gl.CreateProgram());
    if (!program) {
        return makeError(ErrorCode::GraphicsFailure, "glCreateProgram failed");
    }
    gl.AttachShader(program.id(), vertex->id());
    gl.AttachShader(program.id(), fragment->id());
    gl.LinkProgram(program.id());
    gl.DetachShader(program.id(), vertex->id());
    gl.DetachShader(program.id(), fragment->id());

    GLint status = 0;
    gl.GetProgramiv(program.id(), LINK_STATUS, &status);
    if (status == 0) {
        return makeError(ErrorCode::ShaderCompilation,
            std::string(name) + " program failed to link:\n" + programLog(gl, program.id()));
    }
    return program;
}

} // namespace schuimm::gl
