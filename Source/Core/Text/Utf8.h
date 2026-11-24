#pragma once

// UTF-8 decoding and encoding. All engine text (file names, UI strings, log messages) is UTF-8;
// text rendering decodes it to Unicode code points with decodeNext().

#include <cstddef>
#include <string>
#include <string_view>

namespace schuimm::utf8 {

// U+FFFD, substituted for every ill-formed sequence.
inline constexpr char32_t replacementCharacter = 0xFFFD;
inline constexpr char32_t maxCodepoint = 0x10FFFF;

// Decodes the code point at the start of `text` and removes its bytes from `text`.
// Returns false (and leaves `text` empty) once `text` is empty.
//
// Ill-formed input never stops decoding: each maximal ill-formed subsequence (Unicode 15,
// section 3.9, "U+FFFD substitution of maximal subparts") yields one replacementCharacter.
// This rejects overlong encodings, UTF-16 surrogates (U+D800..U+DFFF), values above U+10FFFF,
// stray continuation bytes and truncated sequences.
bool decodeNext(std::string_view& text, char32_t& codepoint) noexcept;

// Number of code points decodeNext() would produce for `text`.
[[nodiscard]] std::size_t codepointCount(std::string_view text) noexcept;

// Appends the UTF-8 encoding of `codepoint`. Surrogates and values above U+10FFFF are encoded
// as replacementCharacter.
void append(std::string& out, char32_t codepoint);

[[nodiscard]] inline std::string encode(char32_t codepoint)
{
    std::string out;
    append(out, codepoint);
    return out;
}

} // namespace schuimm::utf8
