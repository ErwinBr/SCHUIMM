#pragma once

// Internal to the Win32 backend.

#include "Core/Common/Error.h"
#include "Core/Logging/Logger.h"
#include "Platform/Interface/OpenGLSurface.h"
#include "Platform/Windows/Win32Headers.h"

#include <memory>

namespace schuimm::win32 {

class Win32Platform;
class Win32Window;

// WGL context for an OpenGL 3.3 core profile.
//
// opengl32.dll only exports OpenGL 1.1 and legacy context creation. A modern context needs the
// WGL_ARB_pixel_format and WGL_ARB_create_context(_profile) extensions, whose entry points can only
// be queried while *some* context is current. Hence:
//   1. create a hidden dummy window with a legacy pixel format and context, load the extensions,
//   2. destroy the dummy (a window's pixel format can never be changed once set),
//   3. choose the real pixel format with wglChoosePixelFormatARB and create the core context.
class Win32OpenGLSurface final : public OpenGLSurface {
public:
    static Result<std::unique_ptr<OpenGLSurface>> create(Win32Platform& platform, Win32Window& window, Logger& logger,
                                                         const OpenGLSurfaceDescription& description);

    Win32OpenGLSurface(Win32Window& window, HGLRC context, HMODULE openGLLibrary);
    ~Win32OpenGLSurface() override;

    Win32OpenGLSurface(const Win32OpenGLSurface&) = delete;
    Win32OpenGLSurface& operator=(const Win32OpenGLSurface&) = delete;

    void makeCurrent() override;
    void swapBuffers() override;
    bool setSwapInterval(int interval) override;
    [[nodiscard]] bool supportsSwapControl() const override { return swapInterval_ != nullptr; }
    [[nodiscard]] OpenGLProc getProcAddress(const char* name) const override;
    [[nodiscard]] Extent2D framebufferSize() const override;

private:
    using SwapIntervalFn = BOOL(WINAPI*)(int);

    Win32Window& window_;
    HGLRC context_ = nullptr;
    HMODULE openGLLibrary_ = nullptr;
    SwapIntervalFn swapInterval_ = nullptr;
};

} // namespace schuimm::win32
