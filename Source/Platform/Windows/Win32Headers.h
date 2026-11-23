#pragma once

// Single entry point for Windows SDK headers inside the Win32 backend.
// WIN32_LEAN_AND_MEAN, NOMINMAX, UNICODE and _WIN32_WINNT=0x0601 (Windows 7) are set by CMake.

#include <windows.h>
#include <windowsx.h>

#include <string>
#include <string_view>

namespace schuimm::win32 {

[[nodiscard]] std::wstring toWide(std::string_view utf8);
[[nodiscard]] std::string lastErrorMessage(DWORD error = GetLastError());

} // namespace schuimm::win32
