#include "Graphics/OpenGL/GLFunctions.h"

#include <string>

namespace schuimm::gl {

Status loadFunctions(GLFunctions& functions, const OpenGLSurface& surface)
{
#define SCHUIMM_GL_LOAD(returnType, name, parameters)                                               \
    functions.name = reinterpret_cast<returnType(SCHUIMM_GLAPI*) parameters>(surface.getProcAddress("gl" #name)); \
    if (functions.name == nullptr) {                                                                \
        return makeError(ErrorCode::GraphicsFailure, std::string("missing OpenGL entry point gl") + #name); \
    }
    SCHUIMM_GL_FUNCTIONS(SCHUIMM_GL_LOAD)
#undef SCHUIMM_GL_LOAD
    return {};
}

} // namespace schuimm::gl
