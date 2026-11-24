// The real miniaudio device code path with miniaudio's null backend: a genuine audio thread
// calls the mixer at a fixed rate, without sound hardware. Headless, so it runs on CI.

#include "Audio/Miniaudio/MiniaudioAudioDevice.h"
#include "Core/Logging/Logger.h"
#include "TestFramework.h"

#include <chrono>
#include <thread>

using namespace schuimm;

namespace {
std::unique_ptr<AudioDevice> createNullDevice(Logger& logger)
{
    AudioDeviceSettings settings;
    settings.nullBackend = true;
    auto device = createMiniaudioAudioDevice(settings, logger);
    if (!device) {
        std::fprintf(stderr, "  %s\n", describe(device.error()).c_str());
        return nullptr;
    }
    return std::move(*device);
}

// Waits (polling, up to `seconds`) until `done` holds.
template <typename Predicate>
bool waitFor(Predicate done, double seconds)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}
} // namespace

SCHUIMM_TEST(NullBackend_InitialisesAndShutsDown)
{
    Logger logger(LogLevel::Error);
    for (int cycle = 0; cycle < 3; ++cycle) {
        auto device = createNullDevice(logger);
        REQUIRE(device != nullptr);
        CHECK_EQ(device->backendName(), std::string("Null"));
        CHECK(device->sampleRate() >= SoundData::minSampleRate);
    }
}

SCHUIMM_TEST(NullBackend_PlaysSoundsToCompletionOnTheAudioThread)
{
    Logger logger(LogLevel::Error);
    auto device = createNullDevice(logger);
    REQUIRE(device != nullptr);
    // 50 ms of silence-level signal at the device rate.
    const int rate = device->sampleRate();
    std::vector<float> samples(static_cast<std::size_t>(rate / 20), 0.1f);
    auto sound = device->createSound(SoundData::create(1, rate, std::move(samples)).value()).value();

    const SoundInstance a = device->play(sound);
    const SoundInstance b = device->play(sound, {0.5f});
    REQUIRE(a.isValid() && b.isValid());
    CHECK_EQ(device->activeVoiceCount(), 2);
    CHECK(waitFor([&] { return device->state(a) == PlaybackState::Stopped && device->state(b) == PlaybackState::Stopped; }, 5.0));
    device->update();
    CHECK_EQ(device->activeVoiceCount(), 0);
    CHECK_EQ(sound.use_count(), 1);

    // A looping sound keeps playing until stopped; stop is confirmed by the audio thread.
    const SoundInstance loop = device->play(sound, {1.0f, true});
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    CHECK(device->state(loop) == PlaybackState::Playing);
    device->stop(loop);
    CHECK(waitFor([&] {
        device->update();
        return sound.use_count() == 1;
    }, 5.0));
}

SCHUIMM_TEST(NullBackend_ControlStormWhileTheAudioThreadRuns)
{
    // Main-thread playback control as fast as possible while the real audio thread mixes:
    // exercises the publish/reuse protocol across threads (run under ThreadSanitizer too).
    Logger logger(LogLevel::Error);
    auto device = createNullDevice(logger);
    REQUIRE(device != nullptr);
    const int rate = device->sampleRate();
    auto blip = device->createSound(SoundData::create(1, rate, std::vector<float>(64, 0.01f)).value()).value();
    auto loop = device->createSound(SoundData::create(1, rate, std::vector<float>(256, 0.01f)).value()).value();

    int played = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
    for (std::uint32_t i = 0; std::chrono::steady_clock::now() < deadline; ++i) {
        const bool looping = (i % 3) == 0;
        const SoundInstance instance = device->play(looping ? loop : blip, {0.5f, looping});
        if (instance.isValid()) {
            ++played;
            device->setVolume(instance, static_cast<float>(i % 10) / 10.0f);
            if ((i % 2) == 0) {
                device->pause(instance);
                device->resume(instance);
            }
            if (looping || (i % 5) == 0) {
                device->stop(instance);
            }
        }
        device->setMasterVolume((i % 2) == 0 ? 0.5f : 1.0f);
        device->update();
    }
    CHECK(played > 100);
    // Every playback ends (stopped or finished) and every sound is released.
    CHECK(waitFor([&] {
        device->update();
        return device->activeVoiceCount() == 0 && blip.use_count() == 1 && loop.use_count() == 1;
    }, 5.0));
}
