#pragma once

// Binary layout constants of the .schb format. All multi-byte values are little-endian and are
// written and read byte by byte (no struct I/O, no dependence on padding or host endianness).
// The full specification is Documentation/Scripting/Bytecode.md.

#include <cstddef>
#include <cstdint>

namespace schuimm::script::format {

inline constexpr std::uint8_t magic[4] = {'S', 'C', 'H', 'B'};
inline constexpr std::size_t headerSize = 32;
inline constexpr std::size_t sectionEntrySize = 12;
inline constexpr std::size_t instructionSize = 8;
inline constexpr std::size_t debugEntrySize = 8;
inline constexpr std::uint32_t maxFileSize = 64u * 1024u * 1024u;
inline constexpr std::uint32_t maxSectionCount = 16;
inline constexpr std::size_t maxParameters = 255;

enum class SectionId : std::uint32_t {
    Strings = 1,
    Constants = 2,
    Imports = 3,
    Functions = 4,
    Code = 5,
    Debug = 6,
};
inline constexpr std::uint32_t sectionCount = 6;

} // namespace schuimm::script::format
