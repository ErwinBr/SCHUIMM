#include "Core/Input/InputTypes.h"

namespace schuimm {

const char* toString(Key key) noexcept
{
    switch (key) {
    case Key::Unknown: return "Unknown";
    case Key::A: return "A";
    case Key::B: return "B";
    case Key::C: return "C";
    case Key::D: return "D";
    case Key::E: return "E";
    case Key::F: return "F";
    case Key::G: return "G";
    case Key::H: return "H";
    case Key::I: return "I";
    case Key::J: return "J";
    case Key::K: return "K";
    case Key::L: return "L";
    case Key::M: return "M";
    case Key::N: return "N";
    case Key::O: return "O";
    case Key::P: return "P";
    case Key::Q: return "Q";
    case Key::R: return "R";
    case Key::S: return "S";
    case Key::T: return "T";
    case Key::U: return "U";
    case Key::V: return "V";
    case Key::W: return "W";
    case Key::X: return "X";
    case Key::Y: return "Y";
    case Key::Z: return "Z";
    case Key::Digit0: return "Digit0";
    case Key::Digit1: return "Digit1";
    case Key::Digit2: return "Digit2";
    case Key::Digit3: return "Digit3";
    case Key::Digit4: return "Digit4";
    case Key::Digit5: return "Digit5";
    case Key::Digit6: return "Digit6";
    case Key::Digit7: return "Digit7";
    case Key::Digit8: return "Digit8";
    case Key::Digit9: return "Digit9";
    case Key::F1: return "F1";
    case Key::F2: return "F2";
    case Key::F3: return "F3";
    case Key::F4: return "F4";
    case Key::F5: return "F5";
    case Key::F6: return "F6";
    case Key::F7: return "F7";
    case Key::F8: return "F8";
    case Key::F9: return "F9";
    case Key::F10: return "F10";
    case Key::F11: return "F11";
    case Key::F12: return "F12";
    case Key::Escape: return "Escape";
    case Key::Enter: return "Enter";
    case Key::Tab: return "Tab";
    case Key::Backspace: return "Backspace";
    case Key::Space: return "Space";
    case Key::Left: return "Left";
    case Key::Right: return "Right";
    case Key::Up: return "Up";
    case Key::Down: return "Down";
    case Key::Insert: return "Insert";
    case Key::Delete: return "Delete";
    case Key::Home: return "Home";
    case Key::End: return "End";
    case Key::PageUp: return "PageUp";
    case Key::PageDown: return "PageDown";
    case Key::LeftShift: return "LeftShift";
    case Key::RightShift: return "RightShift";
    case Key::LeftControl: return "LeftControl";
    case Key::RightControl: return "RightControl";
    case Key::LeftAlt: return "LeftAlt";
    case Key::RightAlt: return "RightAlt";
    case Key::LeftSuper: return "LeftSuper";
    case Key::RightSuper: return "RightSuper";
    case Key::Minus: return "Minus";
    case Key::Equals: return "Equals";
    case Key::LeftBracket: return "LeftBracket";
    case Key::RightBracket: return "RightBracket";
    case Key::Backslash: return "Backslash";
    case Key::Semicolon: return "Semicolon";
    case Key::Apostrophe: return "Apostrophe";
    case Key::Grave: return "Grave";
    case Key::Comma: return "Comma";
    case Key::Period: return "Period";
    case Key::Slash: return "Slash";
    case Key::CapsLock: return "CapsLock";
    case Key::Count: break;
    }
    return "Unknown";
}

const char* toString(MouseButton button) noexcept
{
    switch (button) {
    case MouseButton::Left: return "Left";
    case MouseButton::Right: return "Right";
    case MouseButton::Middle: return "Middle";
    case MouseButton::Extra1: return "Extra1";
    case MouseButton::Extra2: return "Extra2";
    case MouseButton::Count: break;
    }
    return "Unknown";
}

const char* toString(ButtonState state) noexcept
{
    switch (state) {
    case ButtonState::Up: return "Up";
    case ButtonState::Pressed: return "Pressed";
    case ButtonState::Held: return "Held";
    case ButtonState::Released: return "Released";
    }
    return "Unknown";
}

} // namespace schuimm
