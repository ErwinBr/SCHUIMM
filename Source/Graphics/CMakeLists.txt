# Backend-neutral rendering interface (headers only).
add_library(schuimm_graphics_interface INTERFACE)
add_library(Schuimm::GraphicsInterface ALIAS schuimm_graphics_interface)
target_link_libraries(schuimm_graphics_interface INTERFACE Schuimm::Core)

# OpenGL 3.3 core backend. It depends on the platform *interface* only (OpenGLSurface);
# no window-system headers are used here.
add_library(schuimm_graphics_opengl STATIC
    OpenGL/GLFunctions.cpp
    OpenGL/GLGraphicsDevice.cpp
    OpenGL/GLObjects.cpp
    OpenGL/GLRenderer2D.cpp)
add_library(Schuimm::GraphicsOpenGL ALIAS schuimm_graphics_opengl)
target_link_libraries(schuimm_graphics_opengl PUBLIC Schuimm::GraphicsInterface Schuimm::PlatformInterface)
schuimm_configure_target(schuimm_graphics_opengl)
