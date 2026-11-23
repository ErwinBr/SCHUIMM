#include "Core/Common/StringBuilder.h"

#include "Platform/Linux/X11/X11OpenGLSurface.h"
#include "Platform/Linux/X11/X11Platform.h"
#include "Platform/Linux/X11/X11Window.h"

#include <cstring>
#include <string_view>

namespace schuimm::x11 {

namespace {

using CreateContextAttribsArb = GLXContext (*)(Display*, GLXFBConfig, GLXContext, Bool, const int*);

bool hasExtension(const char* extensions, std::string_view name)
{
    if (extensions == nullptr) {
        return false;
    }
    std::string_view list(extensions);
    while (!list.empty()) {
        const std::size_t end = list.find(' ');
        if (list.substr(0, end) == name) {
            return true;
        }
        if (end == std::string_view::npos) {
            break;
        }
        list.remove_prefix(end + 1);
    }
    return false;
}

// Xlib reports protocol errors through a process-wide callback. This variable is only written
// while a ScopedErrorTrap is active on the (single) thread that talks to the X server.
thread_local int trappedErrorCode = 0;

int trapError(Display* display, XErrorEvent* event)
{
    (void)display;
    trappedErrorCode = event->error_code;
    return 0;
}

class ScopedErrorTrap {
public:
    explicit ScopedErrorTrap(Display* display)
        : display_(display)
    {
        XSync(display_, False);
        trappedErrorCode = 0;
        previous_ = XSetErrorHandler(trapError);
    }
    ~ScopedErrorTrap() { XSetErrorHandler(previous_); }
    ScopedErrorTrap(const ScopedErrorTrap&) = delete;
    ScopedErrorTrap& operator=(const ScopedErrorTrap&) = delete;

    int errorCode()
    {
        XSync(display_, False);
        return trappedErrorCode;
    }

private:
    Display* display_;
    XErrorHandler previous_ = nullptr;
};

OpenGLProc lookup(const char* name)
{
    return reinterpret_cast<OpenGLProc>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>(name)));
}

} // namespace

Result<std::unique_ptr<OpenGLSurface>> X11OpenGLSurface::create(X11Platform& platform, X11Window& window, Logger& logger,
                                                                const OpenGLSurfaceDescription& description)
{
    Display* display = platform.display();
    const char* extensions = glXQueryExtensionsString(display, platform.visual()->screen);
    if (!hasExtension(extensions, "GLX_ARB_create_context") || !hasExtension(extensions, "GLX_ARB_create_context_profile")) {
        return makeError(ErrorCode::Unsupported,
                         "GLX_ARB_create_context_profile is not available; an OpenGL core profile cannot be created");
    }
    const auto createContextAttribs = reinterpret_cast<CreateContextAttribsArb>(lookup("glXCreateContextAttribsARB"));
    if (createContextAttribs == nullptr) {
        return makeError(ErrorCode::Unsupported, "glXCreateContextAttribsARB not found");
    }

    const int attributes[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, description.majorVersion,
        GLX_CONTEXT_MINOR_VERSION_ARB, description.minorVersion,
        GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
        GLX_CONTEXT_FLAGS_ARB, description.debugContext ? GLX_CONTEXT_DEBUG_BIT_ARB : 0,
        None,
    };

    GLXContext context = nullptr;
    int errorCode = 0;
    {
        // Unsupported versions are reported as X protocol errors (BadMatch / GLXBadFBConfig),
        // which would terminate the process with the default handler.
        ScopedErrorTrap trap(display);
        context = createContextAttribs(display, platform.framebufferConfig(), nullptr, True, attributes);
        errorCode = trap.errorCode();
    }
    if (context == nullptr || errorCode != 0) {
        if (context != nullptr) {
            glXDestroyContext(display, context);
        }
        return makeError(ErrorCode::GraphicsFailure,
                         concat("glXCreateContextAttribsARB failed for OpenGL ", description.majorVersion, ".",
                                description.minorVersion, " core (X error ", errorCode, ")"));
    }

    if (!glXMakeCurrent(display, window.handle(), context)) {
        glXDestroyContext(display, context);
        return makeError(ErrorCode::GraphicsFailure, "glXMakeCurrent failed");
    }

    auto surface = std::make_unique<X11OpenGLSurface>(platform, window, context);
    if (hasExtension(extensions, "GLX_EXT_swap_control")) {
        surface->swapIntervalExt_ = reinterpret_cast<SwapIntervalExt>(lookup("glXSwapIntervalEXT"));
    }
    if (surface->swapIntervalExt_ == nullptr && hasExtension(extensions, "GLX_MESA_swap_control")) {
        surface->swapIntervalMesa_ = reinterpret_cast<SwapIntervalMesa>(lookup("glXSwapIntervalMESA"));
    }
    logger.info("Platform", concat("Created GLX context (", glXIsDirect(display, context) ? "direct" : "INDIRECT",
                                   " rendering, swap control: ",
                                   surface->swapIntervalExt_ ? "GLX_EXT_swap_control"
                                   : surface->swapIntervalMesa_ ? "GLX_MESA_swap_control" : "none", ")"));
    return std::unique_ptr<OpenGLSurface>(std::move(surface));
}

X11OpenGLSurface::X11OpenGLSurface(X11Platform& platform, X11Window& window, GLXContext context)
    : platform_(platform)
    , window_(window)
    , context_(context)
{
    window_.setSurfaceAttached(true);
}

X11OpenGLSurface::~X11OpenGLSurface()
{
    Display* display = platform_.display();
    if (glXGetCurrentContext() == context_) {
        glXMakeCurrent(display, None, nullptr);
    }
    glXDestroyContext(display, context_);
    window_.setSurfaceAttached(false);
}

void X11OpenGLSurface::makeCurrent()
{
    glXMakeCurrent(platform_.display(), window_.handle(), context_);
}

void X11OpenGLSurface::swapBuffers()
{
    glXSwapBuffers(platform_.display(), window_.handle());
}

bool X11OpenGLSurface::setSwapInterval(int interval)
{
    if (swapIntervalExt_ != nullptr) {
        swapIntervalExt_(platform_.display(), window_.handle(), interval);
        return true;
    }
    if (swapIntervalMesa_ != nullptr) {
        return swapIntervalMesa_(static_cast<unsigned int>(interval)) == 0;
    }
    return false;
}

bool X11OpenGLSurface::supportsSwapControl() const
{
    return swapIntervalExt_ != nullptr || swapIntervalMesa_ != nullptr;
}

OpenGLProc X11OpenGLSurface::getProcAddress(const char* name) const
{
    return lookup(name);
}

Extent2D X11OpenGLSurface::framebufferSize() const
{
    return window_.framebufferSize();
}

} // namespace schuimm::x11
