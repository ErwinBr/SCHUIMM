#include "Script/Bytecode/Crc32.h"

#include <array>

namespace schuimm::script {

namespace {
constexpr std::array<std::uint32_t, 256> makeTable() noexcept
{
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t value = i;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value & 1u) != 0 ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
        }
        table[i] = value;
    }
    return table;
}
constexpr std::array<std::uint32_t, 256> crcTable = makeTable();
} // namespace

std::uint32_t crc32(std::span<const std::byte> data) noexcept
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const std::byte b : data) {
        crc = crcTable[(crc ^ static_cast<std::uint32_t>(b)) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

} // namespace schuimm::script
