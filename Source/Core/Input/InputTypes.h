#pragma once

#include <cstddef>
#include <cstdint>

namespace schuimm {

// Physical keys, named after the US-QWERTY position. Backends map native scan codes
// (not layout-dependent characters) to these values, so Key::W is always the key
// above S regardless of the active keyboard layout. Text input is a separate, future concern.
enum class Key : std::uint16_t {
    Unknown = 0,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Escape, Enter, Tab, Backspace, Space,
    Left, Right, Up, Down,
    Insert, Delete, Home, End, PageUp, PageDown,
    LeftShift, RightShift, LeftControl, RightControl, LeftAlt, RightAlt, LeftSuper, RightSuper,
    Minus, Equals, LeftBracket, RightBracket, Backslash, Semicolon, Apostrophe, Grave, Comma, Period, Slash,
    CapsLock,
    Count
};

enum class MouseButton : std::uint8_t {
    Left,
    Right,
    Middle,
    Extra1,
    Extra2,
    Count
};

// Per-frame state of a key or button.
enum class ButtonState : std::uint8_t {
    Up,       // Not down, and was not down last frame.
    Pressed,  // Went down this frame.
    Held,     // Down this frame and the previous frame.
    Released, // Went up this frame.
};

inline constexpr std::size_t keyCount = static_cast<std::size_t>(Key::Count);
inline constexpr std::size_t mouseButtonCount = static_cast<std::size_t>(MouseButton::Count);

[[nodiscard]] const char* toString(Key key) noexcept;
[[nodiscard]] const char* toString(MouseButton button) noexcept;
[[nodiscard]] const char* toString(ButtonState state) noexcept;

} // namespace schuimm
