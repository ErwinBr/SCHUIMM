#pragma once

// Internal to the macOS backend (Objective-C++ only).

#import <Cocoa/Cocoa.h>

#include "Core/Common/Error.h"
#include "Core/Logging/Logger.h"
#include "Platform/Interface/OpenGLSurface.h"

#include <memory>

namespace schuimm::cocoa {

class CocoaWindow;

// NSOpenGLContext attached to the window's content view.
//
// macOS only offers "legacy" (2.1) and "core" profiles: NSOpenGLProfileVersion3_2Core yields the
// newest core version the GPU supports (4.1 on every Mac that runs 10.13+). There is no way to ask
// for exactly 3.3, so the renderer restricts itself to the 3.3 feature set and checks the version.
class CocoaOpenGLSurface final : public OpenGLSurface {
public:
    static Result<std::unique_ptr<OpenGLSurface>> create(
        CocoaWindow& window, Logger& logger, const OpenGLSurfaceDescription& description);

    CocoaOpenGLSurface(CocoaWindow& window, NSOpenGLContext* context, void* openGLLibrary);
    ~CocoaOpenGLSurface() override;

    CocoaOpenGLSurface(const CocoaOpenGLSurface&) = delete;
    CocoaOpenGLSurface& operator=(const CocoaOpenGLSurface&) = delete;

    void makeCurrent() override;
    void swapBuffers() override;
    bool setSwapInterval(int interval) override;
    [[nodiscard]] bool supportsSwapControl() const override { return true; }
    [[nodiscard]] OpenGLProc getProcAddress(const char* name) const override;
    [[nodiscard]] Extent2D framebufferSize() const override;

private:
    CocoaWindow& window_;
    NSOpenGLContext* context_ = nil;
    void* openGLLibrary_ = nullptr; // dlopen handle of OpenGL.framework
};

} // namespace schuimm::cocoa
