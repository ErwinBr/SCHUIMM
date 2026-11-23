#include "Platform/macOS/CocoaOpenGLSurface.h"

#include "Core/Common/StringBuilder.h"
#include "Platform/macOS/CocoaWindow.h"

#include <dlfcn.h>

namespace schuimm::cocoa {

Result<std::unique_ptr<OpenGLSurface>> CocoaOpenGLSurface::create(
    CocoaWindow& window, Logger& logger, const OpenGLSurfaceDescription& description)
{
    @autoreleasepool {
        if (description.majorVersion > 4 || (description.majorVersion == 4 && description.minorVersion > 1)) {
            return makeError(ErrorCode::Unsupported,
                concat("macOS supports at most OpenGL 4.1; requested ", description.majorVersion, ".", description.minorVersion));
        }
        // Requests of 3.2+ map to the 3.2 core profile which returns the highest available core version.
        const NSOpenGLPixelFormatAttribute profile =
            description.majorVersion >= 3 ? NSOpenGLProfileVersion3_2Core : NSOpenGLProfileVersionLegacy;

        const NSOpenGLPixelFormatAttribute attributes[] = {
            NSOpenGLPFAAccelerated,
            NSOpenGLPFADoubleBuffer,
            NSOpenGLPFAOpenGLProfile, profile,
            NSOpenGLPFAColorSize, 24,
            NSOpenGLPFAAlphaSize, 8,
            NSOpenGLPFADepthSize, static_cast<NSOpenGLPixelFormatAttribute>(description.depthBits),
            NSOpenGLPFAStencilSize, static_cast<NSOpenGLPixelFormatAttribute>(description.stencilBits),
            0,
        };

        NSOpenGLPixelFormat* pixelFormat = [[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
        if (pixelFormat == nil) {
            return makeError(ErrorCode::GraphicsFailure, "no accelerated OpenGL core profile pixel format available");
        }

        NSOpenGLContext* context = [[NSOpenGLContext alloc] initWithFormat:pixelFormat shareContext:nil];
        if (context == nil) {
            return makeError(ErrorCode::GraphicsFailure, "NSOpenGLContext creation failed");
        }

        void* library = dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_LAZY | RTLD_LOCAL);
        if (library == nullptr) {
            return makeError(ErrorCode::GraphicsFailure, "cannot load OpenGL.framework");
        }

        [context setView:window.contentView()];
        [context makeCurrentContext];
        [context update];

        logger.info("Platform", "Created NSOpenGLContext (core profile)");
        return std::unique_ptr<OpenGLSurface>(std::make_unique<CocoaOpenGLSurface>(window, context, library));
    }
}

CocoaOpenGLSurface::CocoaOpenGLSurface(CocoaWindow& window, NSOpenGLContext* context, void* openGLLibrary)
    : window_(window)
    , context_(context)
    , openGLLibrary_(openGLLibrary)
{
    window_.attachOpenGLContext(context_);
}

CocoaOpenGLSurface::~CocoaOpenGLSurface()
{
    @autoreleasepool {
        window_.detachOpenGLContext();
        if ([NSOpenGLContext currentContext] == context_) {
            [NSOpenGLContext clearCurrentContext];
        }
        [context_ clearDrawable];
        context_ = nil;
    }
    if (openGLLibrary_ != nullptr) {
        dlclose(openGLLibrary_);
    }
}

void CocoaOpenGLSurface::makeCurrent()
{
    [context_ makeCurrentContext];
}

void CocoaOpenGLSurface::swapBuffers()
{
    [context_ flushBuffer];
}

bool CocoaOpenGLSurface::setSwapInterval(int interval)
{
    const GLint value = interval;
    [context_ setValues:&value forParameter:NSOpenGLContextParameterSwapInterval];
    return true;
}

OpenGLProc CocoaOpenGLSurface::getProcAddress(const char* name) const
{
    return reinterpret_cast<OpenGLProc>(dlsym(openGLLibrary_, name));
}

Extent2D CocoaOpenGLSurface::framebufferSize() const
{
    return window_.framebufferSize();
}

} // namespace schuimm::cocoa
