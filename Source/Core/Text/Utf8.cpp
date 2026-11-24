#include "Core/Text/Utf8.h"

#include <cstdint>

namespace schuimm::utf8 {

namespace {

constexpr bool isContinuation(std::uint8_t byte) noexcept
{
    return (byte & 0xC0u) == 0x80u;
}

} // namespace

bool decodeNext(std::string_view& text, char32_t& codepoint) noexcept
{
    if (text.empty()) {
        return false;
    }
    const auto byteAt = [&](std::size_t i) { return static_cast<std::uint8_t>(text[i]); };
    const std::uint8_t lead = byteAt(0);

    if (lead < 0x80u) {
        codepoint = lead;
        text.remove_prefix(1);
        return true;
    }

    // Well-formed UTF-8 byte sequences (Unicode Table 3-7): the lead byte determines the length
    // and the permitted range of the second byte, which excludes overlong forms, surrogates and
    // values above U+10FFFF.
    std::size_t length = 0;
    std::uint8_t secondMin = 0x80u;
    std::uint8_t secondMax = 0xBFu;
    char32_t value = 0;
    if (lead >= 0xC2u && lead <= 0xDFu) {
        length = 2;
        value = lead & 0x1Fu;
    } else if (lead >= 0xE0u && lead <= 0xEFu) {
        length = 3;
        value = lead & 0x0Fu;
        if (lead == 0xE0u) {
            secondMin = 0xA0u; // overlong
        } else if (lead == 0xEDu) {
            secondMax = 0x9Fu; // surrogates
        }
    } else if (lead >= 0xF0u && lead <= 0xF4u) {
        length = 4;
        value = lead & 0x07u;
        if (lead == 0xF0u) {
            secondMin = 0x90u; // overlong
        } else if (lead == 0xF4u) {
            secondMax = 0x8Fu; // above U+10FFFF
        }
    } else {
        // 0x80..0xC1 (continuation byte or overlong 2-byte lead) and 0xF5..0xFF are never valid.
        codepoint = replacementCharacter;
        text.remove_prefix(1);
        return true;
    }

    std::size_t consumed = 1;
    for (; consumed < length; ++consumed) {
        if (consumed >= text.size()) {
            break;
        }
        const std::uint8_t byte = byteAt(consumed);
        const bool valid = consumed == 1 ? (byte >= secondMin && byte <= secondMax) : isContinuation(byte);
        if (!valid) {
            break;
        }
        value = (value << 6) | (byte & 0x3Fu);
    }

    text.remove_prefix(consumed);
    codepoint = consumed == length ? value : replacementCharacter;
    return true;
}

std::size_t codepointCount(std::string_view text) noexcept
{
    std::size_t count = 0;
    char32_t codepoint = 0;
    while (decodeNext(text, codepoint)) {
        ++count;
    }
    return count;
}

void append(std::string& out, char32_t codepoint)
{
    if (codepoint > maxCodepoint || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        codepoint = replacementCharacter;
    }
    const auto put = [&](std::uint32_t byte) { out.push_back(static_cast<char>(static_cast<std::uint8_t>(byte))); };
    const auto cp = static_cast<std::uint32_t>(codepoint);
    if (cp < 0x80u) {
        put(cp);
    } else if (cp < 0x800u) {
        put(0xC0u | (cp >> 6));
        put(0x80u | (cp & 0x3Fu));
    } else if (cp < 0x10000u) {
        put(0xE0u | (cp >> 12));
        put(0x80u | ((cp >> 6) & 0x3Fu));
        put(0x80u | (cp & 0x3Fu));
    } else {
        put(0xF0u | (cp >> 18));
        put(0x80u | ((cp >> 12) & 0x3Fu));
        put(0x80u | ((cp >> 6) & 0x3Fu));
        put(0x80u | (cp & 0x3Fu));
    }
}

} // namespace schuimm::utf8
