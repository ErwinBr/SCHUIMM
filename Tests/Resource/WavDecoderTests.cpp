// WAV decoding (Resource/Audio). Expected values are those written by
// Tests/Assets/generate_test_sounds.py.

#include "Resource/Audio/WavDecoder.h"
#include "Resource/ResourceCache.h"
#include "TestAssets.h"
#include "TestFramework.h"

#include <cstdint>
#include <vector>

using namespace schuimm;
using namespace schuimm::test;

namespace {
Result<SoundData> loadTestSound(std::string_view name)
{
    return loadSound(testAssetLocator(), assetPath(concat("Audio/", name)));
}

void checkInvalid(std::string_view name, std::string_view reason)
{
    auto sound = loadTestSound(name);
    if (sound.has_value()) {
        SCHUIMM_TEST_FAIL_(concat(name, " decoded unexpectedly"), false);
        return;
    }
    CHECK(sound.error().code == ErrorCode::InvalidData);
    if (sound.error().message.find(reason) == std::string::npos || sound.error().message.find(name) == std::string::npos) {
        SCHUIMM_TEST_FAIL_(concat("unexpected message: ", sound.error().message), false);
    }
}
// In-memory WAV files for the format edge cases.
struct WavBuilder {
    std::vector<std::byte> bytes;

    void u8(unsigned v) { bytes.push_back(static_cast<std::byte>(v & 0xFF)); }
    void u16(unsigned v) { u8(v); u8(v >> 8); }
    void u32(std::uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); }
    void tag(const char* t) { for (int i = 0; i < 4; ++i) u8(static_cast<unsigned char>(t[i])); }
    void chunk(const char* id, const std::vector<std::byte>& body)
    {
        tag(id);
        u32(static_cast<std::uint32_t>(body.size()));
        bytes.insert(bytes.end(), body.begin(), body.end());
        if ((body.size() & 1u) != 0) {
            u8(0);
        }
    }
};

struct Extensible {
    unsigned channels = 1;
    unsigned containerBits = 16;
    unsigned validBits = 16;
    std::uint32_t channelMask = 0x4; // front centre
    unsigned extensionSize = 22;
    unsigned tag = 0x0001;           // PCM
    bool corruptGuidTail = false;
};

std::vector<std::byte> fmtBody(const Extensible& e)
{
    WavBuilder b;
    const unsigned blockAlign = e.channels * e.containerBits / 8;
    b.u16(0xFFFE);
    b.u16(e.channels);
    b.u32(48000);
    b.u32(48000 * blockAlign);
    b.u16(blockAlign);
    b.u16(e.containerBits);
    b.u16(e.extensionSize);
    b.u16(e.validBits);
    b.u32(e.channelMask);
    b.u16(e.tag);
    b.u16(0x0000);
    const unsigned tail[] = {0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
    for (unsigned v : tail) {
        b.u8(v);
    }
    if (e.corruptGuidTail) {
        b.bytes.back() = std::byte{0x72};
    }
    return b.bytes;
}

std::vector<std::byte> wavFile(const std::vector<std::byte>& fmt, const std::vector<std::byte>& data, bool duplicateFmt = false)
{
    WavBuilder body;
    body.tag("WAVE");
    body.chunk("fmt ", fmt);
    if (duplicateFmt) {
        body.chunk("fmt ", fmt);
    }
    body.chunk("data", data);
    WavBuilder file;
    file.tag("RIFF");
    file.u32(static_cast<std::uint32_t>(body.bytes.size()));
    file.bytes.insert(file.bytes.end(), body.bytes.begin(), body.bytes.end());
    return file.bytes;
}

std::vector<std::byte> samples16(std::initializer_list<int> values)
{
    WavBuilder b;
    for (int v : values) {
        b.u16(static_cast<unsigned>(v) & 0xFFFF);
    }
    return b.bytes;
}

void checkRejected(const std::vector<std::byte>& file, std::string_view reason)
{
    auto sound = decodeWav(file, "case.wav");
    if (sound.has_value()) {
        SCHUIMM_TEST_FAIL_(concat("decoded unexpectedly, expected: ", reason), false);
        return;
    }
    CHECK(sound.error().code == ErrorCode::InvalidData);
    if (sound.error().message.find(reason) == std::string::npos) {
        SCHUIMM_TEST_FAIL_(concat("unexpected message: ", sound.error().message), false);
    }
}
} // namespace

SCHUIMM_TEST(WavDecoder_Mono16Bit)
{
    auto sound = loadTestSound("mono16_8k.wav");
    REQUIRE(sound.has_value());
    CHECK_EQ(sound->channels(), 1);
    CHECK_EQ(sound->sampleRate(), 8000);
    CHECK_EQ(sound->frameCount(), std::size_t{404}); // the odd-sized LIST chunk before the data is skipped
    CHECK_NEAR(sound->durationSeconds(), 404.0 / 8000.0, 1e-9);
    const auto s = sound->samples();
    CHECK_EQ(s[0], 0.0f);
    CHECK_NEAR(s[1], 32767.0 / 32768.0, 1e-6);
    CHECK_EQ(s[2], -1.0f);
    CHECK_EQ(s[3], -0.5f);
    CHECK_EQ(s[4], 0.25f);
}

SCHUIMM_TEST(WavDecoder_Stereo8BitIsUnsigned)
{
    auto sound = loadTestSound("stereo8_22k.wav");
    REQUIRE(sound.has_value());
    CHECK_EQ(sound->channels(), 2);
    CHECK_EQ(sound->sampleRate(), 22050);
    CHECK_EQ(sound->frameCount(), std::size_t{2});
    const auto s = sound->samples();
    CHECK_EQ(s[0], 0.0f);
    CHECK_NEAR(s[1], 127.0 / 128.0, 1e-6);
    CHECK_EQ(s[2], -1.0f);
    CHECK_EQ(s[3], 0.5f);
}

SCHUIMM_TEST(WavDecoder_24BitFloatAndExtensible)
{
    auto pcm24 = loadTestSound("mono24_44k.wav");
    REQUIRE(pcm24.has_value());
    CHECK_EQ(pcm24->samples()[0], 0.5f);
    CHECK_EQ(pcm24->samples()[1], -0.5f);

    auto floats = loadTestSound("stereo_float_48k.wav");
    REQUIRE(floats.has_value());
    CHECK_EQ(floats->sampleRate(), 48000);
    CHECK_EQ(floats->samples()[0], 0.25f);
    CHECK_EQ(floats->samples()[1], -0.75f);

    auto extensible = loadTestSound("extensible16.wav");
    REQUIRE(extensible.has_value());
    CHECK_EQ(extensible->channels(), 2);
    CHECK_EQ(extensible->samples()[0], 0.5f);
    CHECK_EQ(extensible->samples()[1], -0.5f);
}

SCHUIMM_TEST(WavDecoder_RejectsUnsupportedAndBrokenFiles)
{
    checkInvalid("adpcm.wav", "unsupported encoding");
    checkInvalid("three_channels.wav", "channel count 3");
    checkInvalid("truncated.wav", "truncated");
    checkInvalid("not_a_sound.wav", "not a RIFF file");
    CHECK(!decodeWav({}, "empty").has_value());

    auto missing = loadTestSound("missing.wav");
    REQUIRE(!missing.has_value());
    CHECK(missing.error().code == ErrorCode::NotFound);
}

SCHUIMM_TEST(WavDecoder_LoadsThroughResourceCache)
{
    const AssetLocator locator = testAssetLocator();
    ResourceCache<SoundData> sounds(locator, loadSound);
    const auto id = ResourceId<SoundData>::parse("Audio/mono16_8k.wav").value();
    auto first = sounds.load(id);
    REQUIRE(first.has_value());
    CHECK(sounds.load(id)->get() == first->get());
}

SCHUIMM_TEST(SoundData_ValidatesFormat)
{
    CHECK(SoundData::create(1, 44100, {0.0f, 0.5f}).has_value());
    CHECK(!SoundData::create(0, 44100, {}).has_value());
    CHECK(!SoundData::create(3, 44100, {0, 0, 0}).has_value());
    CHECK(!SoundData::create(2, 44100, {0.0f}).has_value()); // not a whole frame
    CHECK(!SoundData::create(1, 10, {0.0f}).has_value());
}

SCHUIMM_TEST(WavDecoder_ExtensibleSubFormatIsComparedInFull)
{
    CHECK(decodeWav(wavFile(fmtBody({}), samples16({16384})), "pcm.wav").has_value());

    Extensible wrongTail;
    wrongTail.corruptGuidTail = true; // same first two bytes as PCM
    checkRejected(wavFile(fmtBody(wrongTail), samples16({16384})), "unsupported sub-format {0100000000001000800000aa00389b72}");

    Extensible unknown;
    unknown.tag = 0x0002; // ADPCM's GUID
    checkRejected(wavFile(fmtBody(unknown), samples16({16384})), "unsupported sub-format");

    Extensible floats;
    floats.tag = 0x0003;
    floats.containerBits = 32;
    floats.validBits = 32;
    WavBuilder sample;
    sample.u32(0x3E800000); // 0.25f
    auto decoded = decodeWav(wavFile(fmtBody(floats), sample.bytes), "float.wav");
    REQUIRE(decoded.has_value());
    CHECK_EQ(decoded->samples()[0], 0.25f);
}

SCHUIMM_TEST(WavDecoder_ValidBitsBelowTheContainerDecodeCorrectly)
{
    // 20 valid bits in a 24-bit container: left-aligned, low 4 bits zero.
    Extensible e;
    e.containerBits = 24;
    e.validBits = 20;
    WavBuilder data;
    const std::uint32_t half = 0x40000u << 4;      // +0.5 in 20 bits, shifted into the container
    const std::uint32_t minus = 0xC0000u << 4;     // -0.5
    for (const std::uint32_t v : {half, minus}) {
        data.u8(v);
        data.u8(v >> 8);
        data.u8(v >> 16);
    }
    auto sound = decodeWav(wavFile(fmtBody(e), data.bytes), "20in24.wav");
    REQUIRE(sound.has_value());
    CHECK_EQ(sound->samples()[0], 0.5f);
    CHECK_EQ(sound->samples()[1], -0.5f);

    for (const unsigned bad : {0u, 17u}) {
        Extensible invalid;
        invalid.validBits = bad;
        checkRejected(wavFile(fmtBody(invalid), samples16({0})), "inconsistent valid bits");
    }
    Extensible floatBits;
    floatBits.tag = 0x0003;
    floatBits.containerBits = 32;
    floatBits.validBits = 24;
    checkRejected(wavFile(fmtBody(floatBits), samples16({0, 0})), "inconsistent valid bits");
}

SCHUIMM_TEST(WavDecoder_RejectsInconsistentExtensibleFields)
{
    Extensible shortExtension;
    shortExtension.extensionSize = 20;
    checkRejected(wavFile(fmtBody(shortExtension), samples16({0})), "extension too small");

    Extensible mask;
    mask.channelMask = 0x3; // two speakers for one channel
    checkRejected(wavFile(fmtBody(mask), samples16({0})), "channel mask 0x3");

    Extensible noMask;
    noMask.channels = 2;
    noMask.channelMask = 0; // unassigned speakers are allowed
    CHECK(decodeWav(wavFile(fmtBody(noMask), samples16({1, 2})), "nomask.wav").has_value());
}

SCHUIMM_TEST(WavDecoder_RejectsPartialFramesEmptyDataAndDuplicateFormats)
{
    Extensible stereo;
    stereo.channels = 2;
    stereo.channelMask = 0x3;
    checkRejected(wavFile(fmtBody(stereo), samples16({1, 2, 3})), "not a whole number of frames (6 bytes, 4 per frame)");
    checkRejected(wavFile(fmtBody(stereo), {}), "no audio frames");
    checkRejected(wavFile(fmtBody(stereo), samples16({1, 2}), true), "duplicate format chunk");
    CHECK(decodeWav(wavFile(fmtBody(stereo), samples16({1, 2})), "ok.wav").has_value());
}
