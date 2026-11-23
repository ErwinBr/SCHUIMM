#include "Core/Common/Assert.h"
#include "Core/Common/StringBuilder.h"

#include "Platform/Linux/X11/X11OpenGLSurface.h"
#include "Platform/Linux/X11/X11Platform.h"
#include "Platform/Linux/X11/X11Window.h"

#include <algorithm>

namespace schuimm::x11 {

namespace {
constexpr long netWmStateRemove = 0;
constexpr long netWmStateAdd = 1;
constexpr long iconicState = 3; // ICCCM WM_STATE value
} // namespace

X11Window::X11Window(X11Platform& platform, Logger& logger, ::Window handle, Colormap colormap,
                     const WindowDescription& description)
    : platform_(platform)
    , logger_(logger)
    , handle_(handle)
    , colormap_(colormap)
    , size_(description.size)
    , resizable_(description.resizable)
{
    Display* display = platform_.display();
    const Atoms& atoms = platform_.atoms();

    // Ask the window manager to send a ClientMessage instead of killing the connection on close.
    Atom protocols[] = {atoms.wmDeleteWindow};
    XSetWMProtocols(display, handle_, protocols, 1);

    XClassHint* classHint = XAllocClassHint();
    if (classHint != nullptr) {
        char resourceName[] = "schuimm";
        char resourceClass[] = "Schuimm";
        classHint->res_name = resourceName;
        classHint->res_class = resourceClass;
        XSetClassHint(display, handle_, classHint);
        XFree(classHint);
    }

    if (!resizable_) {
        XSizeHints* hints = XAllocSizeHints();
        if (hints != nullptr) {
            hints->flags = PMinSize | PMaxSize;
            hints->min_width = hints->max_width = size_.width;
            hints->min_height = hints->max_height = size_.height;
            XSetWMNormalHints(display, handle_, hints);
            XFree(hints);
        }
    }

    setTitle(description.title);

    if (description.fullscreen && platform_.supportsNetWmFullscreen()) {
        // Before mapping, EWMH allows setting the initial state directly on the property.
        Atom state = atoms.netWmStateFullscreen;
        XChangeProperty(display, handle_, atoms.netWmState, XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<unsigned char*>(&state), 1);
    }

    platform_.registerWindow(this);
    XMapRaised(display, handle_);
    XFlush(display);
    logger_.info("Platform", concat("Created X11 window ", size_.width, "x", size_.height, " (id 0x", std::hex, handle_, ")"));
}

X11Window::~X11Window()
{
    SCHUIMM_VERIFY(!hasSurface_, "the OpenGL surface must be destroyed before its window");
    platform_.unregisterWindow(this);
    Display* display = platform_.display();
    XDestroyWindow(display, handle_);
    XFreeColormap(display, colormap_);
    XFlush(display);
}

void X11Window::setSize(Extent2D size)
{
    if (size.isEmpty() || fullscreen_) {
        return;
    }
    Display* display = platform_.display();
    if (!resizable_) {
        XSizeHints* hints = XAllocSizeHints();
        if (hints != nullptr) {
            hints->flags = PMinSize | PMaxSize;
            hints->min_width = hints->max_width = size.width;
            hints->min_height = hints->max_height = size.height;
            XSetWMNormalHints(display, handle_, hints);
            XFree(hints);
        }
    }
    XResizeWindow(display, handle_, static_cast<unsigned>(size.width), static_cast<unsigned>(size.height));
    XFlush(display);
}

void X11Window::setTitle(const std::string& title)
{
    Display* display = platform_.display();
    const Atoms& atoms = platform_.atoms();
    XStoreName(display, handle_, title.c_str()); // Legacy, Latin-1 only.
    XChangeProperty(display, handle_, atoms.netWmName, atoms.utf8String, 8, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(title.data()), static_cast<int>(title.size()));
    XFlush(display);
}

void X11Window::setFullscreen(bool fullscreen)
{
    if (fullscreen == fullscreen_) {
        return;
    }
    if (!platform_.supportsNetWmFullscreen()) {
        logger_.warning("Platform", "Fullscreen requested, but the window manager does not support _NET_WM_STATE_FULLSCREEN");
        return;
    }
    // EWMH: ask the window manager; the result arrives as a _NET_WM_STATE PropertyNotify.
    Display* display = platform_.display();
    const Atoms& atoms = platform_.atoms();
    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.window = handle_;
    event.xclient.message_type = atoms.netWmState;
    event.xclient.format = 32;
    event.xclient.data.l[0] = fullscreen ? netWmStateAdd : netWmStateRemove;
    event.xclient.data.l[1] = static_cast<long>(atoms.netWmStateFullscreen);
    event.xclient.data.l[2] = 0;
    event.xclient.data.l[3] = 1; // Source indication: normal application.
    XSendEvent(display, DefaultRootWindow(display), False, SubstructureNotifyMask | SubstructureRedirectMask, &event);
    XFlush(display);
}

void X11Window::drainEvents(std::vector<WindowEvent>& out)
{
    events_.drainInto(out);
}

void X11Window::setLiveResizeCallback(std::function<void()> callback)
{
    // X11 has no modal resize loop: resizing arrives as ordinary ConfigureNotify events
    // while the main loop keeps running, so the callback is never needed.
    (void)callback;
}

Result<std::unique_ptr<OpenGLSurface>> X11Window::createOpenGLSurface(const OpenGLSurfaceDescription& description)
{
    if (hasSurface_) {
        return makeError(ErrorCode::InvalidState, "window already has an OpenGL surface");
    }
    return X11OpenGLSurface::create(platform_, *this, logger_, description);
}

Vec2 X11Window::position(int x, int y) const noexcept
{
    return {static_cast<float>(x), static_cast<float>(y)};
}

void X11Window::setMinimized(bool minimized)
{
    if (minimized != minimized_) {
        minimized_ = minimized;
        events_.push(MinimizedChangedEvent{minimized_});
    }
}

void X11Window::readWindowState()
{
    Display* display = platform_.display();
    const Atoms& atoms = platform_.atoms();

    bool fullscreen = false;
    bool hidden = false;
    Atom type = 0;
    int format = 0;
    unsigned long count = 0;
    unsigned long remaining = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(display, handle_, atoms.netWmState, 0, 64, False, XA_ATOM, &type, &format, &count,
                           &remaining, &data) == Success && data != nullptr) {
        const auto* states = reinterpret_cast<const Atom*>(data);
        fullscreen = std::find(states, states + count, atoms.netWmStateFullscreen) != states + count;
        hidden = std::find(states, states + count, atoms.netWmStateHidden) != states + count;
        XFree(data);
    }

    data = nullptr;
    if (XGetWindowProperty(display, handle_, atoms.wmState, 0, 2, False, atoms.wmState, &type, &format, &count,
                           &remaining, &data) == Success && data != nullptr) {
        if (count >= 1 && reinterpret_cast<const long*>(data)[0] == iconicState) {
            hidden = true;
        }
        XFree(data);
    }

    if (fullscreen != fullscreen_) {
        fullscreen_ = fullscreen;
        events_.push(FullscreenChangedEvent{fullscreen_});
    }
    setMinimized(hidden);
}

void X11Window::handleEvent(const XEvent& event)
{
    switch (event.type) {
    case KeyPress: {
        const unsigned int keyCode = event.xkey.keycode;
        const bool repeat = keyCode < keysDown_.size() && keysDown_.test(keyCode);
        if (keyCode < keysDown_.size()) {
            keysDown_.set(keyCode);
        }
        events_.push(KeyEvent{platform_.translateKeyCode(keyCode), true, repeat});
        break;
    }
    case KeyRelease: {
        const unsigned int keyCode = event.xkey.keycode;
        if (keyCode < keysDown_.size()) {
            keysDown_.reset(keyCode);
        }
        events_.push(KeyEvent{platform_.translateKeyCode(keyCode), false, false});
        break;
    }
    case ButtonPress:
    case ButtonRelease: {
        const bool down = event.type == ButtonPress;
        const Vec2 at = position(event.xbutton.x, event.xbutton.y);
        switch (event.xbutton.button) {
        case Button1: events_.push(MouseButtonEvent{MouseButton::Left, down, at}); break;
        case Button2: events_.push(MouseButtonEvent{MouseButton::Middle, down, at}); break;
        case Button3: events_.push(MouseButtonEvent{MouseButton::Right, down, at}); break;
        // X11 reports wheel notches as buttons 4-7 (press and release per notch).
        case Button4: if (down) events_.push(MouseWheelEvent{{0.0f, 1.0f}}); break;
        case Button5: if (down) events_.push(MouseWheelEvent{{0.0f, -1.0f}}); break;
        case 6: if (down) events_.push(MouseWheelEvent{{-1.0f, 0.0f}}); break;
        case 7: if (down) events_.push(MouseWheelEvent{{1.0f, 0.0f}}); break;
        case 8: events_.push(MouseButtonEvent{MouseButton::Extra1, down, at}); break;
        case 9: events_.push(MouseButtonEvent{MouseButton::Extra2, down, at}); break;
        default: break;
        }
        break;
    }
    case MotionNotify:
        events_.push(MouseMovedEvent{position(event.xmotion.x, event.xmotion.y)});
        break;
    case ConfigureNotify: {
        const Extent2D newSize{event.xconfigure.width, event.xconfigure.height};
        if (newSize != size_) {
            size_ = newSize;
            events_.push(WindowResizedEvent{size_, size_});
        }
        break;
    }
    case ClientMessage:
        if (event.xclient.message_type == platform_.atoms().wmProtocols &&
            static_cast<Atom>(event.xclient.data.l[0]) == platform_.atoms().wmDeleteWindow) {
            events_.push(CloseRequestedEvent{});
        }
        break;
    case FocusIn:
    case FocusOut:
        // Focus changes caused by keyboard grabs (e.g. while dragging the title bar) are ignored.
        if (event.xfocus.mode == NotifyGrab || event.xfocus.mode == NotifyUngrab) {
            break;
        }
        if ((event.type == FocusIn) != focused_) {
            focused_ = event.type == FocusIn;
            if (!focused_) {
                keysDown_.reset();
            }
            events_.push(FocusChangedEvent{focused_});
        }
        break;
    case PropertyNotify:
        if (event.xproperty.atom == platform_.atoms().netWmState || event.xproperty.atom == platform_.atoms().wmState) {
            readWindowState();
        }
        break;
    case MapNotify:
        setMinimized(false);
        break;
    case UnmapNotify:
        setMinimized(true);
        break;
    default:
        break;
    }
}

} // namespace schuimm::x11
