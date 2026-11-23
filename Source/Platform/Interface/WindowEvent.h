#pragma once

#include "Core/Common/Geometry.h"
#include "Core/Input/InputTypes.h"

#include <variant>

namespace schuimm {

// Events produced by a native window backend. They are queued by the Window during
// Platform::pollEvents() and consumed afterwards with Window::drainEvents(), so that engine
// code never runs inside native callbacks (except for the live-resize refresh callback).

struct CloseRequestedEvent {};

struct WindowResizedEvent {
    Extent2D windowSize;      // Logical size (points on macOS, pixels elsewhere).
    Extent2D framebufferSize; // Drawable size in pixels.
};

struct FocusChangedEvent {
    bool focused = false;
};

// Minimised, or otherwise not visible at all (e.g. fully occluded on macOS). Nothing drawn
// would be seen, so the Application suspends rendering (see ApplicationSettings).
struct MinimizedChangedEvent {
    bool minimized = false;
};

struct FullscreenChangedEvent {
    bool fullscreen = false;
};

struct KeyEvent {
    Key key = Key::Unknown;
    bool down = false;
    bool repeat = false; // OS auto-repeat of a key that is already down.
};

struct MouseButtonEvent {
    MouseButton button = MouseButton::Left;
    bool down = false;
    Vec2 position; // Window coordinates, origin top-left.
};

struct MouseMovedEvent {
    Vec2 position; // Window coordinates, origin top-left.
};

struct MouseWheelEvent {
    Vec2 delta; // In lines; +y scrolls away from the user.
};

using WindowEvent = std::variant<
    CloseRequestedEvent,
    WindowResizedEvent,
    FocusChangedEvent,
    MinimizedChangedEvent,
    FullscreenChangedEvent,
    KeyEvent,
    MouseButtonEvent,
    MouseMovedEvent,
    MouseWheelEvent>;

} // namespace schuimm
