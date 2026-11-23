#pragma once

#include "Core/Input/InputTypes.h"

#include <string_view>

namespace schuimm::x11 {

// Maps an XKB key name (the 4-character physical position name from the keyboard geometry,
// e.g. "AC01" for the key that types 'A' on US-QWERTY) to an engine Key.
// Key names are layout-independent, which matches Key's physical-position semantics.
[[nodiscard]] Key translateXkbKeyName(std::string_view name) noexcept;

// Fallback for servers that report no key names: maps an unshifted X keysym (XK_* value).
[[nodiscard]] Key translateKeySym(unsigned long keySym) noexcept;

} // namespace schuimm::x11
