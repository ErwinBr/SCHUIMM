#pragma once

#include "Core/Input/InputTypes.h"

#include <cstdint>

namespace schuimm::cocoa {

// Maps a macOS virtual key code (NSEvent.keyCode, the kVK_* constants from HIToolbox) to a
// physical engine Key. Key codes are layout-independent positions, matching Key semantics.
[[nodiscard]] Key translateKeyCode(std::uint16_t keyCode) noexcept;

// Device-dependent modifier bits (NX_DEVICE*KEYMASK in IOKit) for a modifier key code,
// used to tell left and right modifiers apart in flagsChanged events. 0 if not a modifier.
[[nodiscard]] std::uint32_t deviceModifierMask(std::uint16_t keyCode) noexcept;

} // namespace schuimm::cocoa
