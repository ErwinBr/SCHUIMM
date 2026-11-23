#pragma once

// Minimal in-house OpenGL 3.3 core loader.
//
// Only the entry points and constants actually used by the renderer are declared. Types and
// values follow the Khronos OpenGL registry (gl.xml). Function pointers are stored in a
// GLFunctions object owned by the graphics device - there is no global state - and are resolved
// through OpenGLSurface::getProcAddress, so this file contains no window-system code.

#include "Core/Common/Error.h"
#include "Platform/Interface/OpenGLSurface.h"

#include <cstddef>
#include <cstdint>

#if defined(_WIN32) && !defined(_WIN64)
#define SCHUIMM_GLAPI __stdcall
#else
#define SCHUIMM_GLAPI
#endif

namespace schuimm::gl {

using GLenum = unsigned int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLint = int;
using GLuint = unsigned int;
using GLsizei = int;
using GLfloat = float;
using GLchar = char;
using GLubyte = unsigned char;
using GLsizeiptr = std::ptrdiff_t;
using GLintptr = std::ptrdiff_t;

inline constexpr GLenum NO_ERROR_ = 0;
inline constexpr GLboolean FALSE_ = 0;
inline constexpr GLboolean TRUE_ = 1;

inline constexpr GLbitfield COLOR_BUFFER_BIT = 0x00004000;
inline constexpr GLenum TRIANGLES = 0x0004;
inline constexpr GLenum ONE = 1;
inline constexpr GLenum SRC_ALPHA = 0x0302;
inline constexpr GLenum ONE_MINUS_SRC_ALPHA = 0x0303;
inline constexpr GLenum BLEND = 0x0BE2;
inline constexpr GLenum DEPTH_TEST = 0x0B71;
inline constexpr GLenum CULL_FACE = 0x0B44;
inline constexpr GLenum SCISSOR_TEST = 0x0C11;
inline constexpr GLenum BACK = 0x0405;
inline constexpr GLenum UNPACK_ALIGNMENT = 0x0CF5;
inline constexpr GLenum PACK_ALIGNMENT = 0x0D05;
inline constexpr GLenum MAX_TEXTURE_SIZE = 0x0D33;
inline constexpr GLenum TEXTURE_2D = 0x0DE1;
inline constexpr GLenum UNSIGNED_BYTE = 0x1401;
inline constexpr GLenum UNSIGNED_SHORT = 0x1403;
inline constexpr GLenum FLOAT = 0x1406;
inline constexpr GLenum RGBA = 0x1908;
inline constexpr GLenum VENDOR = 0x1F00;
inline constexpr GLenum RENDERER = 0x1F01;
inline constexpr GLenum VERSION = 0x1F02;
inline constexpr GLenum EXTENSIONS = 0x1F03;
inline constexpr GLenum NEAREST = 0x2600;
inline constexpr GLenum LINEAR = 0x2601;
inline constexpr GLenum TEXTURE_MAG_FILTER = 0x2800;
inline constexpr GLenum TEXTURE_MIN_FILTER = 0x2801;
inline constexpr GLenum TEXTURE_WRAP_S = 0x2802;
inline constexpr GLenum TEXTURE_WRAP_T = 0x2803;
inline constexpr GLenum RGBA8 = 0x8058;
inline constexpr GLenum CLAMP_TO_EDGE = 0x812F;
inline constexpr GLenum MAJOR_VERSION = 0x821B;
inline constexpr GLenum MINOR_VERSION = 0x821C;
inline constexpr GLenum NUM_EXTENSIONS = 0x821D;
inline constexpr GLenum CONTEXT_FLAGS = 0x821E;
inline constexpr GLenum TEXTURE0 = 0x84C0;
inline constexpr GLenum ARRAY_BUFFER = 0x8892;
inline constexpr GLenum ELEMENT_ARRAY_BUFFER = 0x8893;
inline constexpr GLenum STREAM_DRAW = 0x88E0;
inline constexpr GLenum STATIC_DRAW = 0x88E4;
inline constexpr GLenum DYNAMIC_DRAW = 0x88E8;
inline constexpr GLenum FRAGMENT_SHADER = 0x8B30;
inline constexpr GLenum VERTEX_SHADER = 0x8B31;
inline constexpr GLenum COMPILE_STATUS = 0x8B81;
inline constexpr GLenum LINK_STATUS = 0x8B82;
inline constexpr GLenum INFO_LOG_LENGTH = 0x8B84;
inline constexpr GLenum SHADING_LANGUAGE_VERSION = 0x8B8C;
inline constexpr GLenum READ_FRAMEBUFFER = 0x8CA8;
inline constexpr GLenum DRAW_FRAMEBUFFER = 0x8CA9;
inline constexpr GLenum FRAMEBUFFER_COMPLETE = 0x8CD5;
inline constexpr GLenum COLOR_ATTACHMENT0 = 0x8CE0;
inline constexpr GLenum FRAMEBUFFER = 0x8D40;
inline constexpr GLenum CONTEXT_PROFILE_MASK = 0x9126;
inline constexpr GLint CONTEXT_CORE_PROFILE_BIT = 0x00000001;

// X(returnType, name, parameterList)
#define SCHUIMM_GL_FUNCTIONS(X)                                                                     \
    X(void, ActiveTexture, (GLenum texture))                                                        \
    X(void, AttachShader, (GLuint program, GLuint shader))                                          \
    X(void, BindBuffer, (GLenum target, GLuint buffer))                                             \
    X(void, BindFramebuffer, (GLenum target, GLuint framebuffer))                                   \
    X(void, BindTexture, (GLenum target, GLuint texture))                                           \
    X(void, BindVertexArray, (GLuint array))                                                        \
    X(void, BlendFunc, (GLenum sfactor, GLenum dfactor))                                            \
    X(void, BlendFuncSeparate, (GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha))    \
    X(void, BufferData, (GLenum target, GLsizeiptr size, const void* data, GLenum usage))           \
    X(void, BufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void* data))     \
    X(GLenum, CheckFramebufferStatus, (GLenum target))                                              \
    X(void, Clear, (GLbitfield mask))                                                               \
    X(void, ClearColor, (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha))                  \
    X(void, CompileShader, (GLuint shader))                                                         \
    X(GLuint, CreateProgram, ())                                                                    \
    X(GLuint, CreateShader, (GLenum type))                                                          \
    X(void, DeleteBuffers, (GLsizei n, const GLuint* buffers))                                      \
    X(void, DeleteFramebuffers, (GLsizei n, const GLuint* framebuffers))                            \
    X(void, DeleteProgram, (GLuint program))                                                        \
    X(void, DeleteShader, (GLuint shader))                                                          \
    X(void, DeleteTextures, (GLsizei n, const GLuint* textures))                                    \
    X(void, DeleteVertexArrays, (GLsizei n, const GLuint* arrays))                                  \
    X(void, DetachShader, (GLuint program, GLuint shader))                                          \
    X(void, Disable, (GLenum cap))                                                                  \
    X(void, DrawArrays, (GLenum mode, GLint first, GLsizei count))                                  \
    X(void, DrawElements, (GLenum mode, GLsizei count, GLenum type, const void* indices))           \
    X(void, Enable, (GLenum cap))                                                                   \
    X(void, EnableVertexAttribArray, (GLuint index))                                                \
    X(void, Finish, ())                                                                             \
    X(void, FramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)) \
    X(void, GenBuffers, (GLsizei n, GLuint* buffers))                                               \
    X(void, GenFramebuffers, (GLsizei n, GLuint* framebuffers))                                     \
    X(void, GenTextures, (GLsizei n, GLuint* textures))                                             \
    X(void, GenVertexArrays, (GLsizei n, GLuint* arrays))                                           \
    X(GLenum, GetError, ())                                                                         \
    X(void, GetIntegerv, (GLenum pname, GLint* data))                                               \
    X(void, GetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog)) \
    X(void, GetProgramiv, (GLuint program, GLenum pname, GLint* params))                            \
    X(void, GetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog))   \
    X(void, GetShaderiv, (GLuint shader, GLenum pname, GLint* params))                              \
    X(const GLubyte*, GetString, (GLenum name))                                                     \
    X(const GLubyte*, GetStringi, (GLenum name, GLuint index))                                      \
    X(GLint, GetUniformLocation, (GLuint program, const GLchar* name))                              \
    X(void, LinkProgram, (GLuint program))                                                          \
    X(void, PixelStorei, (GLenum pname, GLint param))                                               \
    X(void, ReadBuffer, (GLenum src))                                                               \
    X(void, ReadPixels, (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void* pixels)) \
    X(void, ShaderSource, (GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length)) \
    X(void, TexImage2D, (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void* pixels)) \
    X(void, TexParameteri, (GLenum target, GLenum pname, GLint param))                              \
    X(void, Uniform1i, (GLint location, GLint v0))                                                  \
    X(void, Uniform2f, (GLint location, GLfloat v0, GLfloat v1))                                    \
    X(void, UseProgram, (GLuint program))                                                           \
    X(void, VertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer)) \
    X(void, Viewport, (GLint x, GLint y, GLsizei width, GLsizei height))

struct GLFunctions {
#define SCHUIMM_GL_DECLARE(returnType, name, parameters) returnType(SCHUIMM_GLAPI* name) parameters = nullptr;
    SCHUIMM_GL_FUNCTIONS(SCHUIMM_GL_DECLARE)
#undef SCHUIMM_GL_DECLARE
};

// Resolves every entry point. The surface's context must be current.
// Fails, naming the first missing function, if any entry point is unavailable.
[[nodiscard]] Status loadFunctions(GLFunctions& functions, const OpenGLSurface& surface);

} // namespace schuimm::gl
