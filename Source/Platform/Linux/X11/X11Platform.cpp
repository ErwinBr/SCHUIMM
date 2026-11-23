#include "Core/Common/StringBuilder.h"
#include "Platform/NativePlatform.h"

#include "Platform/Linux/X11/X11KeyMap.h"
#include "Platform/Linux/X11/X11Platform.h"
#include "Platform/Linux/X11/X11Window.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <poll.h>

namespace schuimm::x11 {

X11Platform::X11Platform(Logger& logger, Display* display, GLXFBConfig framebufferConfig, XVisualInfo* visual)
    : logger_(logger)
    , display_(display)
    , framebufferConfig_(framebufferConfig)
    , visual_(visual)
{
    atoms_.wmProtocols = XInternAtom(display_, "WM_PROTOCOLS", False);
    atoms_.wmDeleteWindow = XInternAtom(display_, "WM_DELETE_WINDOW", False);
    atoms_.wmState = XInternAtom(display_, "WM_STATE", False);
    atoms_.netWmState = XInternAtom(display_, "_NET_WM_STATE", False);
    atoms_.netWmStateFullscreen = XInternAtom(display_, "_NET_WM_STATE_FULLSCREEN", False);
    atoms_.netWmStateHidden = XInternAtom(display_, "_NET_WM_STATE_HIDDEN", False);
    atoms_.netWmName = XInternAtom(display_, "_NET_WM_NAME", False);
    atoms_.netSupported = XInternAtom(display_, "_NET_SUPPORTED", False);
    atoms_.utf8String = XInternAtom(display_, "UTF8_STRING", False);

    // With detectable auto-repeat the server stops sending fake KeyRelease events for held keys,
    // so a held key produces KeyPress, KeyPress, ..., KeyRelease.
    int detectableSupported = False;
    XkbSetDetectableAutoRepeat(display_, True, &detectableSupported);
    if (!detectableSupported) {
        logger_.warning("Platform", "XKB detectable auto-repeat not supported; key repeat may produce release events");
    }

    buildKeyMap();
    detectWindowManagerFeatures();
    logger_.info("Platform", concat("X11 platform initialised (display ", DisplayString(display_), ", vendor ",
                                    ServerVendor(display_), ", visual 0x", std::hex, visual_->visualid, ")"));
}

X11Platform::~X11Platform()
{
    XFree(visual_);
    XCloseDisplay(display_);
}

void X11Platform::buildKeyMap()
{
    keyMap_.fill(Key::Unknown);
    int minKeyCode = 0;
    int maxKeyCode = 0;
    XDisplayKeycodes(display_, &minKeyCode, &maxKeyCode);
    minKeyCode = std::max(minKeyCode, 0);
    maxKeyCode = std::min(maxKeyCode, 255);

    XkbDescPtr description = XkbGetMap(display_, 0, XkbUseCoreKbd);
    bool haveNames = false;
    if (description != nullptr && XkbGetNames(display_, XkbKeyNamesMask, description) == Success &&
        description->names != nullptr && description->names->keys != nullptr) {
        haveNames = true;
    }

    int mapped = 0;
    for (int keyCode = minKeyCode; keyCode <= maxKeyCode; ++keyCode) {
        Key key = Key::Unknown;
        if (haveNames) {
            const char* name = description->names->keys[keyCode].name;
            key = translateXkbKeyName(std::string_view(name, strnlen(name, XkbKeyNameLength)));
        }
        if (key == Key::Unknown) {
            key = translateKeySym(XkbKeycodeToKeysym(display_, static_cast<KeyCode>(keyCode), 0, 0));
        }
        keyMap_[static_cast<std::size_t>(keyCode)] = key;
        mapped += key != Key::Unknown ? 1 : 0;
    }
    if (description != nullptr) {
        XkbFreeNames(description, XkbKeyNamesMask, True);
        XkbFreeKeyboard(description, 0, True);
    }
    logger_.debug("Platform", concat("XKB key map built: ", mapped, " physical keys mapped",
                                     haveNames ? " (from key names)" : " (from keysyms)"));
}

void X11Platform::detectWindowManagerFeatures()
{
    Atom type = 0;
    int format = 0;
    unsigned long count = 0;
    unsigned long remaining = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(display_, DefaultRootWindow(display_), atoms_.netSupported, 0, 4096, False, XA_ATOM, &type,
                           &format, &count, &remaining, &data) == Success && data != nullptr) {
        const auto* atoms = reinterpret_cast<const Atom*>(data);
        netWmFullscreenSupported_ = std::find(atoms, atoms + count, atoms_.netWmStateFullscreen) != atoms + count;
        XFree(data);
    }
    if (!netWmFullscreenSupported_) {
        logger_.warning("Platform", "No EWMH window manager with _NET_WM_STATE_FULLSCREEN support detected; "
                                    "fullscreen switching is unavailable");
    }
}

Key X11Platform::translateKeyCode(unsigned int keyCode) const noexcept
{
    return keyCode < keyMap_.size() ? keyMap_[keyCode] : Key::Unknown;
}

Result<std::unique_ptr<schuimm::Window>> X11Platform::createWindow(const WindowDescription& description)
{
    if (description.size.isEmpty()) {
        return makeError(ErrorCode::InvalidArgument, "window size must be positive");
    }
    const ::Window root = RootWindow(display_, visual_->screen);
    const Colormap colormap = XCreateColormap(display_, root, visual_->visual, AllocNone);

    XSetWindowAttributes attributes{};
    attributes.colormap = colormap;
    attributes.border_pixel = 0;
    attributes.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                            StructureNotifyMask | FocusChangeMask | PropertyChangeMask | ExposureMask;
    const ::Window handle = XCreateWindow(display_, root, 0, 0, static_cast<unsigned>(description.size.width),
                                          static_cast<unsigned>(description.size.height), 0, visual_->depth,
                                          InputOutput, visual_->visual, CWColormap | CWBorderPixel | CWEventMask,
                                          &attributes);
    if (handle == 0) {
        XFreeColormap(display_, colormap);
        return makeError(ErrorCode::PlatformFailure, "XCreateWindow failed");
    }
    return std::unique_ptr<schuimm::Window>(std::make_unique<X11Window>(*this, logger_, handle, colormap, description));
}

void X11Platform::pollEvents()
{
    while (XPending(display_) > 0) {
        XEvent event{};
        XNextEvent(display_, &event);
        if (event.type == MappingNotify) {
            XRefreshKeyboardMapping(&event.xmapping);
            buildKeyMap();
            continue;
        }
        for (X11Window* window : windows_) {
            if (window->handle() == event.xany.window) {
                window->handleEvent(event);
                break;
            }
        }
    }
}

void X11Platform::waitEvents(double timeoutSeconds)
{
    if (XPending(display_) == 0) {
        pollfd descriptor{ConnectionNumber(display_), POLLIN, 0};
        const int timeoutMs = static_cast<int>(std::ceil(std::max(0.0, timeoutSeconds) * 1000.0));
        poll(&descriptor, 1, timeoutMs);
    }
    pollEvents();
}

void X11Platform::registerWindow(X11Window* window)
{
    windows_.push_back(window);
}

void X11Platform::unregisterWindow(X11Window* window)
{
    windows_.erase(std::remove(windows_.begin(), windows_.end(), window), windows_.end());
}

} // namespace schuimm::x11

namespace schuimm {

Result<std::unique_ptr<Platform>> createNativePlatform(Logger& logger)
{
    // XInitThreads is not needed: the engine is single-threaded with respect to X11.
    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        return makeError(ErrorCode::PlatformFailure,
                         "cannot open X display (is DISPLAY set and the X server reachable?)");
    }

    int glxMajor = 0;
    int glxMinor = 0;
    if (!glXQueryVersion(display, &glxMajor, &glxMinor) || glxMajor < 1 || (glxMajor == 1 && glxMinor < 3)) {
        XCloseDisplay(display);
        return makeError(ErrorCode::Unsupported, concat("GLX 1.3 or newer required (server provides ", glxMajor, ".", glxMinor, ")"));
    }

    // The framebuffer configuration determines the X visual, so it is chosen before any window
    // exists. 8-bit RGBA, 24-bit depth, 8-bit stencil, double buffered.
    const int attributes[] = {
        GLX_X_RENDERABLE, True,
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
        GLX_RED_SIZE, 8,
        GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8,
        GLX_ALPHA_SIZE, 8,
        GLX_DEPTH_SIZE, 24,
        GLX_STENCIL_SIZE, 8,
        GLX_DOUBLEBUFFER, True,
        None,
    };
    int count = 0;
    GLXFBConfig* configs = glXChooseFBConfig(display, DefaultScreen(display), attributes, &count);
    GLXFBConfig chosen = nullptr;
    XVisualInfo* visual = nullptr;
    for (int i = 0; i < count && visual == nullptr; ++i) {
        visual = glXGetVisualFromFBConfig(display, configs[i]);
        if (visual != nullptr) {
            chosen = configs[i];
        }
    }
    if (configs != nullptr) {
        XFree(configs);
    }
    if (visual == nullptr) {
        XCloseDisplay(display);
        return makeError(ErrorCode::GraphicsFailure, "no GLX framebuffer configuration with RGBA8/D24S8 double buffering");
    }
    return std::unique_ptr<Platform>(std::make_unique<x11::X11Platform>(logger, display, chosen, visual));
}

} // namespace schuimm
