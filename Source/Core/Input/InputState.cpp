#include "Core/Input/InputState.h"

namespace schuimm {

namespace {
template <typename Enum>
constexpr std::size_t indexOf(Enum value) noexcept
{
    return static_cast<std::size_t>(value);
}
} // namespace

void InputState::Tracker::press() noexcept
{
    if (!down) {
        down = true;
        wentDown = true;
    }
}

void InputState::Tracker::release() noexcept
{
    if (down) {
        down = false;
        wentUp = true;
    }
}

void InputState::Tracker::advanceFrame() noexcept
{
    const bool wasDown = isDownState(state);

    if (wentDown && wentUp) {
        if (down) {
            // Released and pressed again within one frame: a fresh press.
            state = ButtonState::Pressed;
        } else if (wasDown) {
            // Was down, released, tapped again: the net effect is a release.
            state = ButtonState::Released;
        } else {
            // Tap shorter than a frame.
            state = ButtonState::Pressed;
            pendingRelease = true;
        }
    } else if (wentDown) {
        state = wasDown ? ButtonState::Held : ButtonState::Pressed;
        pendingRelease = false;
    } else if (wentUp) {
        state = ButtonState::Released;
        pendingRelease = false;
    } else if (pendingRelease) {
        state = ButtonState::Released;
        pendingRelease = false;
    } else {
        state = down ? ButtonState::Held : ButtonState::Up;
    }

    wentDown = false;
    wentUp = false;
}

void InputState::keyDown(Key key)
{
    if (key == Key::Unknown || key == Key::Count) {
        return;
    }
    keys_[indexOf(key)].press();
}

void InputState::keyUp(Key key)
{
    if (key == Key::Unknown || key == Key::Count) {
        return;
    }
    keys_[indexOf(key)].release();
}

void InputState::mouseButtonDown(MouseButton button)
{
    if (button == MouseButton::Count) {
        return;
    }
    buttons_[indexOf(button)].press();
}

void InputState::mouseButtonUp(MouseButton button)
{
    if (button == MouseButton::Count) {
        return;
    }
    buttons_[indexOf(button)].release();
}

void InputState::mouseMoved(Vec2 position)
{
    if (hasMousePosition_) {
        pendingMouseDelta_ += position - mousePosition_;
    }
    mousePosition_ = position;
    hasMousePosition_ = true;
}

void InputState::mouseWheel(Vec2 delta)
{
    pendingWheelDelta_ += delta;
}

void InputState::releaseAll()
{
    for (Tracker& tracker : keys_) {
        tracker.release();
    }
    for (Tracker& tracker : buttons_) {
        tracker.release();
    }
}

void InputState::update()
{
    for (Tracker& tracker : keys_) {
        tracker.advanceFrame();
    }
    for (Tracker& tracker : buttons_) {
        tracker.advanceFrame();
    }
    frameMouseDelta_ = pendingMouseDelta_;
    pendingMouseDelta_ = {};
    frameWheelDelta_ = pendingWheelDelta_;
    pendingWheelDelta_ = {};
}

ButtonState InputState::state(Key key) const noexcept
{
    if (key == Key::Unknown || key == Key::Count) {
        return ButtonState::Up;
    }
    return keys_[indexOf(key)].state;
}

ButtonState InputState::state(MouseButton button) const noexcept
{
    if (button == MouseButton::Count) {
        return ButtonState::Up;
    }
    return buttons_[indexOf(button)].state;
}

} // namespace schuimm
