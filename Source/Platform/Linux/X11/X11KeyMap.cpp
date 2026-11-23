#include "Platform/Linux/X11/X11KeyMap.h"

#include <array>
#include <utility>

namespace schuimm::x11 {

Key translateXkbKeyName(std::string_view name) noexcept
{
    static constexpr std::array<std::pair<std::string_view, Key>, 86> table{{
        {"TLDE", Key::Grave},       {"AE01", Key::Digit1},       {"AE02", Key::Digit2},
        {"AE03", Key::Digit3},      {"AE04", Key::Digit4},       {"AE05", Key::Digit5},
        {"AE06", Key::Digit6},      {"AE07", Key::Digit7},       {"AE08", Key::Digit8},
        {"AE09", Key::Digit9},      {"AE10", Key::Digit0},       {"AE11", Key::Minus},
        {"AE12", Key::Equals},      {"BKSP", Key::Backspace},    {"TAB", Key::Tab},
        {"AD01", Key::Q},           {"AD02", Key::W},            {"AD03", Key::E},
        {"AD04", Key::R},           {"AD05", Key::T},            {"AD06", Key::Y},
        {"AD07", Key::U},           {"AD08", Key::I},            {"AD09", Key::O},
        {"AD10", Key::P},           {"AD11", Key::LeftBracket},  {"AD12", Key::RightBracket},
        {"BKSL", Key::Backslash},   {"CAPS", Key::CapsLock},     {"AC01", Key::A},
        {"AC02", Key::S},           {"AC03", Key::D},            {"AC04", Key::F},
        {"AC05", Key::G},           {"AC06", Key::H},            {"AC07", Key::J},
        {"AC08", Key::K},           {"AC09", Key::L},            {"AC10", Key::Semicolon},
        {"AC11", Key::Apostrophe},  {"RTRN", Key::Enter},        {"LFSH", Key::LeftShift},
        {"AB01", Key::Z},           {"AB02", Key::X},            {"AB03", Key::C},
        {"AB04", Key::V},           {"AB05", Key::B},            {"AB06", Key::N},
        {"AB07", Key::M},           {"AB08", Key::Comma},        {"AB09", Key::Period},
        {"AB10", Key::Slash},       {"RTSH", Key::RightShift},   {"LCTL", Key::LeftControl},
        {"LALT", Key::LeftAlt},     {"SPCE", Key::Space},        {"RALT", Key::RightAlt},
        {"ALGR", Key::RightAlt},    {"RCTL", Key::RightControl}, {"LWIN", Key::LeftSuper},
        {"LMTA", Key::LeftSuper},   {"RWIN", Key::RightSuper},   {"RMTA", Key::RightSuper},
        {"ESC", Key::Escape},       {"FK01", Key::F1},           {"FK02", Key::F2},
        {"FK03", Key::F3},          {"FK04", Key::F4},           {"FK05", Key::F5},
        {"FK06", Key::F6},          {"FK07", Key::F7},           {"FK08", Key::F8},
        {"FK09", Key::F9},          {"FK10", Key::F10},          {"FK11", Key::F11},
        {"FK12", Key::F12},         {"INS", Key::Insert},        {"DELE", Key::Delete},
        {"HOME", Key::Home},        {"END", Key::End},           {"PGUP", Key::PageUp},
        {"PGDN", Key::PageDown},    {"UP", Key::Up},             {"LEFT", Key::Left},
        {"DOWN", Key::Down},        {"RGHT", Key::Right},
    }};
    for (const auto& [keyName, key] : table) {
        if (keyName == name) {
            return key;
        }
    }
    return Key::Unknown;
}

Key translateKeySym(unsigned long keySym) noexcept
{
    // Values from <X11/keysymdef.h>.
    if (keySym >= 0x61 && keySym <= 0x7a) { // XK_a..XK_z
        return static_cast<Key>(static_cast<unsigned>(Key::A) + (keySym - 0x61));
    }
    if (keySym >= 0x30 && keySym <= 0x39) { // XK_0..XK_9
        return static_cast<Key>(static_cast<unsigned>(Key::Digit0) + (keySym - 0x30));
    }
    if (keySym >= 0xffbe && keySym <= 0xffc9) { // XK_F1..XK_F12
        return static_cast<Key>(static_cast<unsigned>(Key::F1) + (keySym - 0xffbe));
    }
    switch (keySym) {
    case 0xff1b: return Key::Escape;
    case 0xff0d: return Key::Enter;
    case 0xff09: return Key::Tab;
    case 0xff08: return Key::Backspace;
    case 0x0020: return Key::Space;
    case 0xff51: return Key::Left;
    case 0xff52: return Key::Up;
    case 0xff53: return Key::Right;
    case 0xff54: return Key::Down;
    case 0xff63: return Key::Insert;
    case 0xffff: return Key::Delete;
    case 0xff50: return Key::Home;
    case 0xff57: return Key::End;
    case 0xff55: return Key::PageUp;
    case 0xff56: return Key::PageDown;
    case 0xffe1: return Key::LeftShift;
    case 0xffe2: return Key::RightShift;
    case 0xffe3: return Key::LeftControl;
    case 0xffe4: return Key::RightControl;
    case 0xffe9: return Key::LeftAlt;
    case 0xffea: return Key::RightAlt;
    case 0xfe03: return Key::RightAlt; // ISO_Level3_Shift (AltGr)
    case 0xffeb: return Key::LeftSuper;
    case 0xffec: return Key::RightSuper;
    case 0xffe5: return Key::CapsLock;
    case 0x002d: return Key::Minus;
    case 0x003d: return Key::Equals;
    case 0x005b: return Key::LeftBracket;
    case 0x005d: return Key::RightBracket;
    case 0x005c: return Key::Backslash;
    case 0x003b: return Key::Semicolon;
    case 0x0027: return Key::Apostrophe;
    case 0x0060: return Key::Grave;
    case 0x002c: return Key::Comma;
    case 0x002e: return Key::Period;
    case 0x002f: return Key::Slash;
    default: return Key::Unknown;
    }
}

} // namespace schuimm::x11
