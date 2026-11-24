// Playback management and mixing, driven deterministically through a manual backend: the test
// plays the role of the audio thread by calling render(). No audio hardware is involved.

#include "Audio/AudioDevice.h"
#include "Core/Logging/Logger.h"
#include "TestFramework.h"

#include <cmath>
#include <vector>

using namespace schuimm;

namespace {

class ManualBackend final : public AudioBackend {
public:
    explicit ManualBackend(int rate, bool* stopped = nullptr)
        : rate_(rate)
        , stopped_(stopped)
    {
    }
    Status start(RenderCallback callback, void* user) override
    {
        callback_ = callback;
        user_ = user;
        return {};
    }
    void stop() override
    {
        if (stopped_ != nullptr) {
            *stopped_ = true;
        }
        callback_ = nullptr;
    }
    int sampleRate() const override { return rate_; }
    std::string backendName() const override { return "Manual"; }
    std::string deviceName() const override { return "test"; }

    // Runs one audio callback and returns its interleaved stereo output.
    std::vector<float> render(std::uint32_t frames)
    {
        std::vector<float> out(frames * 2, 99.0f);
        if (callback_ != nullptr) {
            callback_(user_, out.data(), frames);
        }
        return out;
    }

    // Runs one callback and returns its first (left) sample.
    float firstSample(std::uint32_t frames) { return render(frames)[0]; }

private:
    int rate_;
    bool* stopped_;
    RenderCallback callback_ = nullptr;
    void* user_ = nullptr;
};

struct Fixture {
    Logger logger{LogLevel::Error};
    ManualBackend* backend = nullptr;
    std::unique_ptr<AudioDevice> device;

    explicit Fixture(int rate = 1000)
    {
        auto created = std::make_unique<ManualBackend>(rate);
        backend = created.get();
        device = AudioDevice::create(std::move(created), logger).value();
    }

    // A constant-valued sound of `frames` frames.
    std::shared_ptr<const Sound> constant(float value, std::size_t frames)
    {
        std::vector<float> samples(frames, value);
        return device->createSound(SoundData::create(1, device->sampleRate(), std::move(samples)).value()).value();
    }
};

} // namespace

SCHUIMM_TEST(AudioDevice_CreateSoundConvertsToStereoAtDeviceRate)
{
    Fixture f(2000);
    auto mono = f.device->createSound(SoundData::create(1, 2000, {0.1f, 0.2f}).value());
    REQUIRE(mono.has_value());
    CHECK_EQ((*mono)->frameCount(), std::size_t{2});
    CHECK_EQ((*mono)->data()[0], 0.1f);
    CHECK_EQ((*mono)->data()[1], 0.1f);

    // 1000 Hz -> 2000 Hz: twice the frames, linearly interpolated.
    auto upsampled = f.device->createSound(SoundData::create(2, 1000, {0.0f, 1.0f, 1.0f, 0.0f}).value());
    REQUIRE(upsampled.has_value());
    CHECK_EQ((*upsampled)->frameCount(), std::size_t{4});
    CHECK_EQ((*upsampled)->sampleRate(), 2000);
    CHECK_NEAR((*upsampled)->data()[2], 0.5, 1e-6); // halfway between frame 0 and 1 (left)
    CHECK_NEAR((*upsampled)->data()[3], 0.5, 1e-6);
    CHECK(!f.device->createSound(SoundData{}).has_value());
}

SCHUIMM_TEST(AudioDevice_PlaysAndEndsSounds)
{
    Fixture f;
    auto sound = f.constant(0.25f, 6);
    const SoundInstance instance = f.device->play(sound);
    REQUIRE(instance.isValid());
    CHECK(f.device->state(instance) == PlaybackState::Playing);
    CHECK_EQ(f.device->activeVoiceCount(), 1);

    auto out = f.backend->render(4);
    CHECK_EQ(out[0], 0.25f);
    CHECK_EQ(out[7], 0.25f);
    out = f.backend->render(4); // two frames left, then silence
    CHECK_EQ(out[2], 0.25f);
    CHECK_EQ(out[4], 0.0f);
    CHECK(f.device->state(instance) == PlaybackState::Stopped);

    // The device holds the sound until update() collects the finished voice.
    CHECK(sound.use_count() == 2);
    f.device->update();
    CHECK(sound.use_count() == 1);
    CHECK_EQ(f.device->activeVoiceCount(), 0);
}

SCHUIMM_TEST(AudioDevice_MixesSimultaneousSoundsWithVolumes)
{
    Fixture f;
    auto a = f.constant(0.25f, 100);
    auto b = f.constant(0.5f, 100);
    const SoundInstance first = f.device->play(a, {0.5f});
    const SoundInstance second = f.device->play(b);
    CHECK(!(first == second));
    auto out = f.backend->render(4);
    CHECK_NEAR(out[0], 0.25 * 0.5 + 0.5, 1e-6);

    f.device->setMasterVolume(0.5f);
    out = f.backend->render(4); // ramped over this buffer
    CHECK_NEAR(out[6], (0.125 + 0.5) * 0.5, 1e-6);
    out = f.backend->render(4);
    CHECK_NEAR(out[0], (0.125 + 0.5) * 0.5, 1e-6);

    f.device->setVolume(second, 0.0f);
    CHECK_EQ(f.device->volume(second), 0.0f);
    (void)f.backend->render(4);
    out = f.backend->render(4);
    CHECK_NEAR(out[0], 0.125 * 0.5, 1e-6);
    CHECK_EQ(f.device->masterVolume(), 0.5f);
}

SCHUIMM_TEST(AudioDevice_OutputIsClamped)
{
    Fixture f;
    auto loud = f.constant(0.9f, 10);
    (void)f.device->play(loud);
    (void)f.device->play(loud);
    const auto out = f.backend->render(2);
    CHECK_EQ(out[0], 1.0f);
}

SCHUIMM_TEST(AudioDevice_PauseResumeAndStop)
{
    Fixture f;
    auto sound = f.constant(0.25f, 100);
    const SoundInstance instance = f.device->play(sound, {1.0f, false, true}); // starts paused
    CHECK(f.device->state(instance) == PlaybackState::Paused);
    CHECK_EQ(f.backend->firstSample(2), 0.0f);
    f.device->resume(instance);
    CHECK(f.device->state(instance) == PlaybackState::Playing);
    CHECK_EQ(f.backend->firstSample(2), 0.25f);
    f.device->pause(instance);
    CHECK_EQ(f.backend->firstSample(2), 0.0f);
    f.device->resume(instance);
    CHECK_EQ(f.backend->firstSample(2), 0.25f);

    f.device->stop(instance);
    CHECK(f.device->state(instance) == PlaybackState::Stopped);
    CHECK_EQ(sound.use_count(), 2); // not released before the audio thread confirmed
    CHECK_EQ(f.backend->firstSample(2), 0.0f);
    f.device->update();
    CHECK_EQ(sound.use_count(), 1);

    // Controls on a stale handle do nothing.
    f.device->resume(instance);
    f.device->setVolume(instance, 1.0f);
    CHECK(f.device->state(instance) == PlaybackState::Stopped);
}

SCHUIMM_TEST(AudioDevice_LoopsUntilStopped)
{
    Fixture f;
    std::vector<float> ramp{0.1f, 0.2f, 0.3f};
    auto sound = f.device->createSound(SoundData::create(1, 1000, ramp).value()).value();
    const SoundInstance instance = f.device->play(sound, {1.0f, true});
    const auto out = f.backend->render(7);
    const float expected[] = {0.1f, 0.2f, 0.3f, 0.1f, 0.2f, 0.3f, 0.1f};
    for (std::size_t i = 0; i < 7; ++i) {
        CHECK_EQ(out[i * 2], expected[i]);
    }
    CHECK(f.device->state(instance) == PlaybackState::Playing);
    f.device->stop(instance);
    (void)f.backend->render(1);
    f.device->update();
    CHECK_EQ(f.device->activeVoiceCount(), 0);
}

SCHUIMM_TEST(AudioDevice_PauseAllSilencesWithoutChangingState)
{
    Fixture f;
    auto sound = f.constant(0.25f, 100);
    const SoundInstance instance = f.device->play(sound);
    f.device->pauseAll();
    CHECK(f.device->isPausedAll());
    CHECK_EQ(f.backend->firstSample(4), 0.0f);
    CHECK(f.device->state(instance) == PlaybackState::Playing);
    f.device->resumeAll();
    CHECK_EQ(f.backend->firstSample(4), 0.25f);
    f.device->stopAll();
    (void)f.backend->render(1);
    f.device->update();
    CHECK_EQ(f.device->activeVoiceCount(), 0);
}

SCHUIMM_TEST(AudioDevice_VoicePoolExhaustionAndReuse)
{
    Fixture f;
    auto sound = f.constant(0.01f, 50);
    std::vector<SoundInstance> instances;
    for (int i = 0; i < Mixer::voiceCount; ++i) {
        instances.push_back(f.device->play(sound));
        CHECK(instances.back().isValid());
    }
    CHECK(!f.device->play(sound).isValid()); // every voice is busy
    CHECK_EQ(f.device->activeVoiceCount(), Mixer::voiceCount);

    (void)f.backend->render(50); // all sounds end
    const SoundInstance reused = f.device->play(sound); // play() collects ended voices first
    REQUIRE(reused.isValid());
    // A new playback on a reused voice gets a new generation: the old handle stays stale.
    CHECK(f.device->state(instances[reused.voice]) == PlaybackState::Stopped);
    CHECK(f.device->state(reused) == PlaybackState::Playing);
}

// --- Control state is never lost (no command queue that could fill up) ------------------------

SCHUIMM_TEST(AudioDevice_ManyControlCallsBetweenMixesKeepTheLastValue)
{
    Fixture f;
    auto sound = f.constant(0.5f, 100);
    const SoundInstance instance = f.device->play(sound, {1.0f, true});
    (void)f.backend->render(2);
    // Far more calls than any queue would hold, all within one audio period.
    for (int i = 0; i < 10000; ++i) {
        f.device->setVolume(instance, (i % 2) == 0 ? 0.2f : 0.4f);
        f.device->pause(instance);
        f.device->resume(instance);
        f.device->setMasterVolume((i % 2) == 0 ? 0.1f : 0.9f);
    }
    f.device->setVolume(instance, 0.25f);
    f.device->setMasterVolume(1.0f);
    CHECK(f.device->state(instance) == PlaybackState::Playing);
    CHECK_EQ(f.device->volume(instance), 0.25f);
    (void)f.backend->render(4); // volume ramps to the new value over this buffer
    CHECK_NEAR(f.backend->firstSample(4), 0.5 * 0.25, 1e-6);

    for (int i = 0; i < 10000; ++i) {
        f.device->pause(instance);
        f.device->resume(instance);
    }
    f.device->stop(instance); // the last call wins
    CHECK_EQ(f.backend->firstSample(4), 0.0f);
    f.device->update();
    CHECK_EQ(f.device->activeVoiceCount(), 0);
    CHECK_EQ(sound.use_count(), 1);
}

SCHUIMM_TEST(AudioDevice_PlayThenStopBeforeAnyMixFreesTheVoice)
{
    Fixture f;
    auto sound = f.constant(0.5f, 100);
    const SoundInstance instance = f.device->play(sound, {1.0f, true});
    f.device->stop(instance);
    CHECK(f.device->state(instance) == PlaybackState::Stopped);
    CHECK_EQ(f.backend->firstSample(4), 0.0f); // never heard
    f.device->update();
    CHECK_EQ(f.device->activeVoiceCount(), 0);
    CHECK_EQ(sound.use_count(), 1);
}

SCHUIMM_TEST(AudioDevice_RepeatedPlayAndStopOfAllVoicesNeverLeaks)
{
    Fixture f;
    auto sound = f.constant(0.01f, 1000);
    for (int round = 0; round < 1000; ++round) {
        std::vector<SoundInstance> instances;
        for (int i = 0; i < Mixer::voiceCount; ++i) {
            instances.push_back(f.device->play(sound, {1.0f, true}));
            REQUIRE(instances.back().isValid());
        }
        for (const SoundInstance& instance : instances) {
            f.device->stop(instance);
        }
        (void)f.backend->render(1);
        f.device->update();
        REQUIRE(f.device->activeVoiceCount() == 0);
    }
    CHECK_EQ(sound.use_count(), 1);
}

// --- Every play request is preserved ---------------------------------------------------------

SCHUIMM_TEST(AudioDevice_AllPlaysWithinOneFrameAreHeard)
{
    for (const int count : {2, 8, Mixer::voiceCount}) {
        Fixture f;
        std::vector<std::shared_ptr<const Sound>> sounds;
        std::vector<SoundInstance> instances;
        double expected = 0.0;
        for (int i = 0; i < count; ++i) {
            const float level = static_cast<float>(i + 1) / 1024.0f; // distinct and exact in float
            sounds.push_back(f.constant(level, 100));
            instances.push_back(f.device->play(sounds.back()));
            REQUIRE(instances.back().isValid());
            expected += static_cast<double>(level);
        }
        CHECK_NEAR(f.backend->firstSample(4), expected, 1e-6);
        for (const SoundInstance& instance : instances) {
            CHECK(f.device->state(instance) == PlaybackState::Playing);
        }
        CHECK_EQ(f.device->activeVoiceCount(), count);
    }
}

SCHUIMM_TEST(AudioDevice_SameSoundPlayedManyTimesInOneFrame)
{
    Fixture f;
    auto sound = f.constant(1.0f / 64.0f, 100);
    std::vector<SoundInstance> instances;
    for (int i = 0; i < Mixer::voiceCount; ++i) {
        instances.push_back(f.device->play(sound));
        REQUIRE(instances.back().isValid());
        for (std::size_t j = 0; j + 1 < instances.size(); ++j) {
            CHECK(!(instances[j] == instances.back()));
        }
    }
    CHECK_NEAR(f.backend->firstSample(4), Mixer::voiceCount / 64.0, 1e-6);
}

SCHUIMM_TEST(AudioDevice_PlayStopPlayInOneFrame)
{
    Fixture f;
    auto first = f.constant(0.25f, 100);
    auto second = f.constant(0.5f, 100);
    const SoundInstance a = f.device->play(first);
    f.device->stop(a);
    const SoundInstance b = f.device->play(second);
    REQUIRE(b.isValid());
    CHECK(!(a == b));
    CHECK_EQ(f.backend->firstSample(4), 0.5f); // only the second is heard
    CHECK(f.device->state(a) == PlaybackState::Stopped);
    CHECK(f.device->state(b) == PlaybackState::Playing);
}

SCHUIMM_TEST(AudioDevice_EveryPlayIsHeardExactlyOnceAcrossVoiceReuse)
{
    Fixture f;
    constexpr float level = 1.0f / 1024.0f;
    auto blip = f.constant(level, 1); // one frame: heard in exactly one mix
    long long requested = 0;
    long long heard = 0;
    for (int round = 0; requested < 10000; ++round) {
        const int count = (round % Mixer::voiceCount) + 1;
        for (int i = 0; i < count; ++i) {
            REQUIRE(f.device->play(blip).isValid());
            ++requested;
        }
        const auto out = f.backend->render(2);
        heard += std::llround(out[0] / level);
        CHECK_EQ(out[2], 0.0f); // and only once
        f.device->update();
        REQUIRE(f.device->activeVoiceCount() == 0);
    }
    CHECK_EQ(heard, requested);
}

SCHUIMM_TEST(Mixer_IgnoresARepublishedGeneration)
{
    // Why AudioDevice never reuses a voice's previous generation: the mixer only starts a
    // playback when the generation changes.
    Mixer mixer;
    const Sound sound(1000, {0.5f, 0.5f, 0.5f, 0.5f}); // two frames
    std::vector<float> out(2);
    mixer.start(0, 7, &sound, 1.0f, false, false);
    mixer.mix(out.data(), 1);
    CHECK_EQ(out[0], 0.5f);
    mixer.mix(out.data(), 1);
    CHECK_EQ(mixer.endedGeneration(0), 7u);
    mixer.start(0, 7, &sound, 1.0f, false, false);
    mixer.mix(out.data(), 1);
    CHECK_EQ(out[0], 0.0f);
    mixer.start(0, 8, &sound, 1.0f, false, false);
    mixer.mix(out.data(), 1);
    CHECK_EQ(out[0], 0.5f);
}

SCHUIMM_TEST(AudioDevice_RejectsForeignSoundsAndNull)
{
    Fixture f(1000);
    Fixture other(2000); // sounds are converted for one device's rate
    auto foreign = other.constant(0.5f, 10);
    CHECK(!f.device->play(foreign).isValid());
    CHECK(!f.device->play(nullptr).isValid());
    CHECK(f.device->state(SoundInstance{}) == PlaybackState::Stopped);
}

SCHUIMM_TEST(AudioDevice_DestructionStopsBackendAndSoundsMayOutliveIt)
{
    Logger logger(LogLevel::Error);
    bool stopped = false;
    std::shared_ptr<const Sound> sound;
    {
        auto device = AudioDevice::create(std::make_unique<ManualBackend>(1000, &stopped), logger).value();
        sound = device->createSound(SoundData::create(1, 1000, {0.5f, 0.5f}).value()).value();
        (void)device->play(sound, {1.0f, true});
    }
    CHECK(stopped);
    CHECK_EQ(sound.use_count(), 1);
    CHECK_EQ(sound->frameCount(), std::size_t{2});
}
