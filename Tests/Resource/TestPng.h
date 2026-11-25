#pragma once

// Builds minimal PNG files in memory for tests: a signature, a valid IHDR chunk (8-bit RGBA) and
// an IDAT chunk whose contents are not valid compressed data. The header is complete, so
// readImageInfo() reports the size, but the pixels cannot be decoded.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace schuimm::test {

inline std::uint32_t pngCrc32(std::span<const std::byte> data)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const std::byte b : data) {
        crc ^= static_cast<std::uint32_t>(b);
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

inline void appendBigEndian(std::vector<std::byte>& out, std::uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<std::byte>((value >> shift) & 0xFFu));
}

inline void appendPngChunk(std::vector<std::byte>& bytes, const char (&type)[5], std::span<const std::byte> data)
{
    appendBigEndian(bytes, static_cast<std::uint32_t>(data.size()));
    const std::size_t typeStart = bytes.size();
    for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<std::byte>(type[i]));
    bytes.insert(bytes.end(), data.begin(), data.end());
    appendBigEndian(bytes, pngCrc32(std::span(bytes).subspan(typeStart)));
}

inline std::vector<std::byte> pngHeader(std::uint32_t width, std::uint32_t height)
{
    std::vector<std::byte> bytes;
    for (const int b : {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A}) bytes.push_back(static_cast<std::byte>(b));
    std::vector<std::byte> ihdr;
    appendBigEndian(ihdr, width);
    appendBigEndian(ihdr, height);
    for (const int b : {8, 6, 0, 0, 0}) ihdr.push_back(static_cast<std::byte>(b));
    appendPngChunk(bytes, "IHDR", ihdr);
    const std::array<std::byte, 4> garbage = {std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
    appendPngChunk(bytes, "IDAT", garbage);
    return bytes;
}

} // namespace schuimm::test
