#pragma once

#include "Core/Common/Geometry.h"

namespace schuimm {

using OpenGLProc = void (*)();

struct OpenGLSurfaceDescription {
    int majorVersion = 3;
    int minorVersion = 3;
    bool debugContext = false;
    int depthBits = 24;
    int stencilBits = 8;
};

// A native OpenGL context bound to a window's drawable (WGL, NSOpenGLContext, GLX,
// and later EGL for Wayland). This is the only seam between the platform layer and
// the OpenGL renderer: the renderer never touches native window-system APIs.
//
// Lifetime: a surface must be destroyed before the Window that created it.
// Threading: all calls must happen on the thread that created the surface.
class OpenGLSurface {
public:
    virtual ~OpenGLSurface() = default;

    virtual void makeCurrent() = 0;
    virtual void swapBuffers() = 0;

    // 1 = wait for vertical blank, 0 = immediate. Returns false when the driver does not
    // support swap control (the request then has no effect).
    virtual bool setSwapInterval(int interval) = 0;
    [[nodiscard]] virtual bool supportsSwapControl() const = 0;

    // Resolves an OpenGL entry point for the current context, or nullptr.
    [[nodiscard]] virtual OpenGLProc getProcAddress(const char* name) const = 0;

    [[nodiscard]] virtual Extent2D framebufferSize() const = 0;
};

} // namespace schuimm
