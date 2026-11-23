#pragma once

#include "Core/Input/InputTypes.h"

#include <cstdint>

namespace schuimm::win32 {

// Maps a PC/AT set-1 scan code (WM_KEYDOWN lParam bits 16-23) plus the extended-key flag
// (bit 24) to an engine Key. Scan codes identify physical positions, independent of the
// active keyboard layout. Keypad keys are not part of Key yet and map to Key::Unknown.
[[nodiscard]] Key translateScanCode(std::uint32_t scanCode, bool extended) noexcept;

} // namespace schuimm::win32
