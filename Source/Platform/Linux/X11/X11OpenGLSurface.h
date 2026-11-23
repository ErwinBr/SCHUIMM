#pragma once

// Internal to the X11 backend.

#include "Core/Common/Error.h"
#include "Core/Logging/Logger.h"
#include "Platform/Interface/OpenGLSurface.h"

#include <memory>

#include "Platform/Linux/X11/X11Headers.h"

namespace schuimm::x11 {

class X11Platform;
class X11Window;

// GLX context created with GLX_ARB_create_context(_profile) for an OpenGL 3.3 core profile.
class X11OpenGLSurface final : public OpenGLSurface {
public:
    static Result<std::unique_ptr<OpenGLSurface>> create(X11Platform& platform, X11Window& window, Logger& logger,
                                                         const OpenGLSurfaceDescription& description);

    X11OpenGLSurface(X11Platform& platform, X11Window& window, GLXContext context);
    ~X11OpenGLSurface() override;

    X11OpenGLSurface(const X11OpenGLSurface&) = delete;
    X11OpenGLSurface& operator=(const X11OpenGLSurface&) = delete;

    void makeCurrent() override;
    void swapBuffers() override;
    bool setSwapInterval(int interval) override;
    [[nodiscard]] bool supportsSwapControl() const override;
    [[nodiscard]] OpenGLProc getProcAddress(const char* name) const override;
    [[nodiscard]] Extent2D framebufferSize() const override;

private:
    using SwapIntervalExt = void (*)(Display*, GLXDrawable, int);
    using SwapIntervalMesa = int (*)(unsigned int);

    X11Platform& platform_;
    X11Window& window_;
    GLXContext context_ = nullptr;
    SwapIntervalExt swapIntervalExt_ = nullptr;
    SwapIntervalMesa swapIntervalMesa_ = nullptr;
};

} // namespace schuimm::x11
