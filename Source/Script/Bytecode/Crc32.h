#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace schuimm::script {

// CRC-32 (IEEE 802.3, reflected polynomial 0xEDB88320), as used by zlib and PNG.
[[nodiscard]] std::uint32_t crc32(std::span<const std::byte> data) noexcept;

} // namespace schuimm::script
