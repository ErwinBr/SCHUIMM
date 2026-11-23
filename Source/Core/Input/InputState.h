#pragma once

#include "Core/Common/Geometry.h"
#include "Core/Input/InputTypes.h"

#include <array>

namespace schuimm {

// Platform-independent input state tracking.
//
// Event side (called by whoever translates native events, normally the Application):
//   keyDown/keyUp, mouseButtonDown/Up, mouseMoved, mouseWheel, releaseAll.
// Frame side:
//   update() once per frame, after all events of that frame were delivered.
//   Queries then report the state for the frame that just began.
//
// Repeated keyDown events for a key that is already down (OS auto-repeat) are ignored.
// A press and release within the same frame is reported as Pressed in that frame and
// Released in the next one, so short taps are never lost.
class InputState {
public:
    // --- Event side ------------------------------------------------------------------
    void keyDown(Key key);
    void keyUp(Key key);
    void mouseButtonDown(MouseButton button);
    void mouseButtonUp(MouseButton button);
    // Position in window coordinates (points), origin top-left.
    void mouseMoved(Vec2 position);
    // Wheel delta in "lines" (positive y = away from the user, positive x = right).
    void mouseWheel(Vec2 delta);
    // Releases every key and button, e.g. when the window loses focus and
    // further key-up events would never arrive.
    void releaseAll();

    // --- Frame side ------------------------------------------------------------------
    void update();

    [[nodiscard]] ButtonState state(Key key) const noexcept;
    [[nodiscard]] ButtonState state(MouseButton button) const noexcept;

    [[nodiscard]] bool isDown(Key key) const noexcept { return isDownState(state(key)); }
    [[nodiscard]] bool wasPressed(Key key) const noexcept { return state(key) == ButtonState::Pressed; }
    [[nodiscard]] bool wasReleased(Key key) const noexcept { return state(key) == ButtonState::Released; }
    [[nodiscard]] bool isHeld(Key key) const noexcept { return state(key) == ButtonState::Held; }

    [[nodiscard]] bool isDown(MouseButton button) const noexcept { return isDownState(state(button)); }
    [[nodiscard]] bool wasPressed(MouseButton button) const noexcept { return state(button) == ButtonState::Pressed; }
    [[nodiscard]] bool wasReleased(MouseButton button) const noexcept { return state(button) == ButtonState::Released; }
    [[nodiscard]] bool isHeld(MouseButton button) const noexcept { return state(button) == ButtonState::Held; }

    [[nodiscard]] Vec2 mousePosition() const noexcept { return mousePosition_; }
    [[nodiscard]] Vec2 mouseDelta() const noexcept { return frameMouseDelta_; }
    [[nodiscard]] Vec2 wheelDelta() const noexcept { return frameWheelDelta_; }
    [[nodiscard]] bool hasMousePosition() const noexcept { return hasMousePosition_; }

private:
    struct Tracker {
        bool down = false;           // Raw state from events.
        bool wentDown = false;       // Edge seen since the last update().
        bool wentUp = false;         // Edge seen since the last update().
        bool pendingRelease = false; // Tap completed: report Released next frame.
        ButtonState state = ButtonState::Up;

        void press() noexcept;
        void release() noexcept;
        void advanceFrame() noexcept;
    };

    static constexpr bool isDownState(ButtonState s) noexcept
    {
        return s == ButtonState::Pressed || s == ButtonState::Held;
    }

    std::array<Tracker, keyCount> keys_{};
    std::array<Tracker, mouseButtonCount> buttons_{};
    Vec2 mousePosition_{};
    Vec2 pendingMouseDelta_{};
    Vec2 frameMouseDelta_{};
    Vec2 pendingWheelDelta_{};
    Vec2 frameWheelDelta_{};
    bool hasMousePosition_ = false;
};

} // namespace schuimm
