# Backend-neutral graphics: the rendering interfaces plus the code every backend shares
# (camera, CPU-side sprite batching). No graphics API is used here.
add_library(schuimm_graphics STATIC
    Common/Camera2D.cpp
    Common/ShelfPacker.cpp
    Common/SpriteBatcher.cpp
    Common/TextLayout.cpp
    Interface/Texture.cpp)
add_library(Schuimm::Graphics ALIAS schuimm_graphics)
target_link_libraries(schuimm_graphics PUBLIC Schuimm::Core)
schuimm_configure_target(schuimm_graphics)

# OpenGL 3.3 core backend. It depends on the platform *interface* only (OpenGLSurface);
# no window-system headers are used here.
add_library(schuimm_graphics_opengl STATIC
    OpenGL/GLFunctions.cpp
    OpenGL/GLGraphicsDevice.cpp
    OpenGL/GLObjects.cpp
    OpenGL/GLRenderer2D.cpp)
add_library(Schuimm::GraphicsOpenGL ALIAS schuimm_graphics_opengl)
target_link_libraries(schuimm_graphics_opengl PUBLIC Schuimm::Graphics Schuimm::PlatformInterface)
schuimm_configure_target(schuimm_graphics_opengl)
