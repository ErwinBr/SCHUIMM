// WAV decoding (Resource/Audio). Expected values are those written by
// Tests/Assets/generate_test_sounds.py.

#include "Resource/Audio/WavDecoder.h"
#include "Resource/ResourceCache.h"
#include "TestAssets.h"
#include "TestFramework.h"

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
