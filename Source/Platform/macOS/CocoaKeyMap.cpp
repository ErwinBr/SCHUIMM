#include "Platform/macOS/CocoaKeyMap.h"

namespace schuimm::cocoa {

Key translateKeyCode(std::uint16_t keyCode) noexcept
{
    // Values from <HIToolbox/Events.h>; copied to avoid pulling in Carbon.
    switch (keyCode) {
    case 0x00: return Key::A;
    case 0x01: return Key::S;
    case 0x02: return Key::D;
    case 0x03: return Key::F;
    case 0x04: return Key::H;
    case 0x05: return Key::G;
    case 0x06: return Key::Z;
    case 0x07: return Key::X;
    case 0x08: return Key::C;
    case 0x09: return Key::V;
    case 0x0B: return Key::B;
    case 0x0C: return Key::Q;
    case 0x0D: return Key::W;
    case 0x0E: return Key::E;
    case 0x0F: return Key::R;
    case 0x10: return Key::Y;
    case 0x11: return Key::T;
    case 0x12: return Key::Digit1;
    case 0x13: return Key::Digit2;
    case 0x14: return Key::Digit3;
    case 0x15: return Key::Digit4;
    case 0x16: return Key::Digit6;
    case 0x17: return Key::Digit5;
    case 0x18: return Key::Equals;
    case 0x19: return Key::Digit9;
    case 0x1A: return Key::Digit7;
    case 0x1B: return Key::Minus;
    case 0x1C: return Key::Digit8;
    case 0x1D: return Key::Digit0;
    case 0x1E: return Key::RightBracket;
    case 0x1F: return Key::O;
    case 0x20: return Key::U;
    case 0x21: return Key::LeftBracket;
    case 0x22: return Key::I;
    case 0x23: return Key::P;
    case 0x24: return Key::Enter;
    case 0x25: return Key::L;
    case 0x26: return Key::J;
    case 0x27: return Key::Apostrophe;
    case 0x28: return Key::K;
    case 0x29: return Key::Semicolon;
    case 0x2A: return Key::Backslash;
    case 0x2B: return Key::Comma;
    case 0x2C: return Key::Slash;
    case 0x2D: return Key::N;
    case 0x2E: return Key::M;
    case 0x2F: return Key::Period;
    case 0x30: return Key::Tab;
    case 0x31: return Key::Space;
    case 0x32: return Key::Grave;
    case 0x33: return Key::Backspace;
    case 0x35: return Key::Escape;
    case 0x36: return Key::RightSuper;
    case 0x37: return Key::LeftSuper;
    case 0x38: return Key::LeftShift;
    case 0x39: return Key::CapsLock;
    case 0x3A: return Key::LeftAlt;
    case 0x3B: return Key::LeftControl;
    case 0x3C: return Key::RightShift;
    case 0x3D: return Key::RightAlt;
    case 0x3E: return Key::RightControl;
    case 0x60: return Key::F5;
    case 0x61: return Key::F6;
    case 0x62: return Key::F7;
    case 0x63: return Key::F3;
    case 0x64: return Key::F8;
    case 0x65: return Key::F9;
    case 0x67: return Key::F11;
    case 0x6D: return Key::F10;
    case 0x6F: return Key::F12;
    case 0x72: return Key::Insert; // "Help" on Apple keyboards
    case 0x73: return Key::Home;
    case 0x74: return Key::PageUp;
    case 0x75: return Key::Delete; // Forward delete
    case 0x76: return Key::F4;
    case 0x77: return Key::End;
    case 0x78: return Key::F2;
    case 0x79: return Key::PageDown;
    case 0x7A: return Key::F1;
    case 0x7B: return Key::Left;
    case 0x7C: return Key::Right;
    case 0x7D: return Key::Down;
    case 0x7E: return Key::Up;
    default: return Key::Unknown;
    }
}

std::uint32_t deviceModifierMask(std::uint16_t keyCode) noexcept
{
    switch (keyCode) {
    case 0x3B: return 0x00000001; // NX_DEVICELCTLKEYMASK
    case 0x38: return 0x00000002; // NX_DEVICELSHFTKEYMASK
    case 0x3C: return 0x00000004; // NX_DEVICERSHFTKEYMASK
    case 0x37: return 0x00000008; // NX_DEVICELCMDKEYMASK
    case 0x36: return 0x00000010; // NX_DEVICERCMDKEYMASK
    case 0x3A: return 0x00000020; // NX_DEVICELALTKEYMASK
    case 0x3D: return 0x00000040; // NX_DEVICERALTKEYMASK
    case 0x3E: return 0x00002000; // NX_DEVICERCTLKEYMASK
    case 0x39: return 0x00010000; // NSEventModifierFlagCapsLock
    default: return 0;
    }
}

} // namespace schuimm::cocoa
