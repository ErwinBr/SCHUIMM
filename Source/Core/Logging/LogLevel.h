#pragma once

#include <cstdint>

namespace schuimm {

enum class LogLevel : std::uint8_t {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Off, // Only meaningful as a minimum level: disables all output.
};

[[nodiscard]] const char* toString(LogLevel level) noexcept;

} // namespace schuimm
