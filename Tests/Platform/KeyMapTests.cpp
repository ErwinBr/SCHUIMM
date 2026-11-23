#include "TestFramework.h"

// Pure translation tables of the native backend compiled for this platform.

#if defined(_WIN32)
#include "Platform/Windows/Win32KeyMap.h"
using namespace schuimm;

SCHUIMM_TEST(Win32KeyMap_TranslatesScanCodes)
{
    using win32::translateScanCode;
    CHECK(translateScanCode(0x1E, false) == Key::A);
    CHECK(translateScanCode(0x11, false) == Key::W);
    CHECK(translateScanCode(0x01, false) == Key::Escape);
    CHECK(translateScanCode(0x57, false) == Key::F11);
    CHECK(translateScanCode(0x1D, false) == Key::LeftControl);
    CHECK(translateScanCode(0x1D, true) == Key::RightControl);
    CHECK(translateScanCode(0x38, true) == Key::RightAlt);
    CHECK(translateScanCode(0x4B, true) == Key::Left);
    CHECK(translateScanCode(0x4B, false) == Key::Unknown); // keypad 4
    CHECK(translateScanCode(0x1C, true) == Key::Unknown);  // keypad Enter
}

#elif defined(__APPLE__)
#include "Platform/macOS/CocoaKeyMap.h"
using namespace schuimm;

SCHUIMM_TEST(CocoaKeyMap_TranslatesVirtualKeyCodes)
{
    using cocoa::translateKeyCode;
    CHECK(translateKeyCode(0x00) == Key::A);
    CHECK(translateKeyCode(0x0D) == Key::W);
    CHECK(translateKeyCode(0x35) == Key::Escape);
    CHECK(translateKeyCode(0x67) == Key::F11);
    CHECK(translateKeyCode(0x7B) == Key::Left);
    CHECK(translateKeyCode(0x38) == Key::LeftShift);
    CHECK(translateKeyCode(0x3C) == Key::RightShift);
    CHECK(translateKeyCode(0xFF) == Key::Unknown);
}

SCHUIMM_TEST(CocoaKeyMap_ModifierMasksAreDistinct)
{
    using cocoa::deviceModifierMask;
    CHECK(deviceModifierMask(0x38) != deviceModifierMask(0x3C)); // left/right shift
    CHECK(deviceModifierMask(0x3B) != deviceModifierMask(0x3E)); // left/right control
    CHECK_EQ(deviceModifierMask(0x00), std::uint32_t{0});
}

#else
#include "Platform/Linux/X11/X11KeyMap.h"
using namespace schuimm;

SCHUIMM_TEST(X11KeyMap_TranslatesXkbKeyNames)
{
    using x11::translateXkbKeyName;
    CHECK(translateXkbKeyName("AC01") == Key::A);
    CHECK(translateXkbKeyName("AD02") == Key::W);
    CHECK(translateXkbKeyName("ESC") == Key::Escape);
    CHECK(translateXkbKeyName("FK11") == Key::F11);
    CHECK(translateXkbKeyName("RGHT") == Key::Right);
    CHECK(translateXkbKeyName("LFSH") == Key::LeftShift);
    CHECK(translateXkbKeyName("ALGR") == Key::RightAlt);
    CHECK(translateXkbKeyName("ZZZZ") == Key::Unknown);
}

SCHUIMM_TEST(X11KeyMap_TranslatesKeySymFallback)
{
    using x11::translateKeySym;
    CHECK(translateKeySym(0x61) == Key::A);     // XK_a
    CHECK(translateKeySym(0x7a) == Key::Z);     // XK_z
    CHECK(translateKeySym(0x35) == Key::Digit5);
    CHECK(translateKeySym(0xffc8) == Key::F11);
    CHECK(translateKeySym(0xff1b) == Key::Escape);
    CHECK(translateKeySym(0x12345) == Key::Unknown);
}
#endif
