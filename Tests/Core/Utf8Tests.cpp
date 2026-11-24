#include "Core/Text/Utf8.h"
#include "TestFramework.h"

#include <vector>

using namespace schuimm;

namespace {
std::vector<char32_t> decodeAll(std::string_view text)
{
    std::vector<char32_t> result;
    char32_t codepoint = 0;
    while (utf8::decodeNext(text, codepoint)) {
        result.push_back(codepoint);
    }
    return result;
}

std::vector<char32_t> codepoints(std::initializer_list<char32_t> list)
{
    return list;
}
} // namespace

SCHUIMM_TEST(Utf8_DecodesAsciiAndWesternEuropeanLetters)
{
    CHECK(decodeAll("Az 09") == codepoints({'A', 'z', ' ', '0', '9'}));
    // é è ë ï ü ö ä ç ñ (two bytes each)
    CHECK(decodeAll("\xC3\xA9\xC3\xA8\xC3\xAB\xC3\xAF\xC3\xBC\xC3\xB6\xC3\xA4\xC3\xA7\xC3\xB1") ==
          codepoints({0xE9, 0xE8, 0xEB, 0xEF, 0xFC, 0xF6, 0xE4, 0xE7, 0xF1}));
    CHECK_EQ(utf8::codepointCount("Caf\xC3\xA9"), std::size_t{4});
}

SCHUIMM_TEST(Utf8_DecodesThreeAndFourByteSequences)
{
    CHECK(decodeAll("\xE2\x82\xAC") == codepoints({0x20AC}));         // €
    CHECK(decodeAll("\xF0\x9F\x99\x82") == codepoints({0x1F642}));    // 🙂
    CHECK(decodeAll("\xF4\x8F\xBF\xBF") == codepoints({0x10FFFF}));   // largest code point
}

SCHUIMM_TEST(Utf8_ReplacesIllFormedSequences)
{
    const char32_t r = utf8::replacementCharacter;
    CHECK(decodeAll("\x80") == codepoints({r}));                      // stray continuation
    CHECK(decodeAll("\xC0\xAF") == codepoints({r, r}));               // overlong '/'
    CHECK(decodeAll("\xE0\x80\xAF") == codepoints({r, r, r}));        // overlong 3-byte
    CHECK(decodeAll("\xED\xA0\x80") == codepoints({r, r, r}));        // UTF-16 surrogate
    CHECK(decodeAll("\xF4\x90\x80\x80") == codepoints({r, r, r, r})); // above U+10FFFF
    CHECK(decodeAll("\xFF") == codepoints({r}));
    // Truncated sequences: one replacement for the maximal subpart, then decoding resumes.
    CHECK(decodeAll("\xC3") == codepoints({r}));
    CHECK(decodeAll("\xE2\x82" "A") == codepoints({r, 'A'}));
    CHECK(decodeAll("\xF0\x9F\x99") == codepoints({r}));
}

SCHUIMM_TEST(Utf8_EncodeRoundTrips)
{
    for (const char32_t cp : {char32_t{'a'}, char32_t{0xE9}, char32_t{0x20AC}, char32_t{0x1F642}}) {
        CHECK(decodeAll(utf8::encode(cp)) == codepoints({cp}));
    }
    CHECK_EQ(utf8::encode(0xD800), utf8::encode(utf8::replacementCharacter));
    CHECK_EQ(utf8::encode(0x110000), utf8::encode(utf8::replacementCharacter));
}
