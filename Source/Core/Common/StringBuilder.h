#pragma once

#include <sstream>
#include <string>
#include <utility>

namespace schuimm {

// Concatenates streamable values into a string: concat("size ", w, "x", h).
// std::format is avoided on purpose: libc++ does not provide it for the macOS 10.13 target.
template <typename... Args>
[[nodiscard]] std::string concat(Args&&... args)
{
    std::ostringstream stream;
    (stream << ... << std::forward<Args>(args));
    return std::move(stream).str();
}

// Fixed-point formatting of a floating-point value, e.g. formatFixed(16.6667, 2) -> "16.67".
[[nodiscard]] std::string formatFixed(double value, int decimals);

} // namespace schuimm
