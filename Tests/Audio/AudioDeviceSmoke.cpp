// Real audio output: opens the default output device through the platform's audio API (no
// null backend), plays a short quiet tone and shuts down. Needs an audio device, so it is
// labelled `audio` and run where one exists (see ContinuousIntegration.md).

#include "Audio/Miniaudio/MiniaudioAudioDevice.h"
#include "Core/Logging/Logger.h"
#include "Core/Logging/StandardSinks.h"
#include "TestFramework.h"

#include <chrono>
#include <cmath>
#include <numbers>
#include <thread>

using namespace schuimm;

SCHUIMM_TEST(AudioDeviceSmoke_PlaysToneOnDefaultDevice)
{
    Logger logger(LogLevel::Info);
    logger.addSink(std::make_unique<ConsoleSink>());
    auto device = createMiniaudioAudioDevice(AudioDeviceSettings{}, logger);
    if (!device) {
        std::fprintf(stderr, "  %s\n", describe(device.error()).c_str());
    }
    REQUIRE(device.has_value());
    AudioDevice& audio = **device;
    CHECK(audio.backendName() != "Null");

    // 0.3 s, 440 Hz, quiet, with short fades so it does not click.
    const int rate = 44100;
    std::vector<float> samples(static_cast<std::size_t>(rate * 3 / 10));
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double t = static_cast<double>(i) / rate;
        const double fade = std::min({1.0, t / 0.02, (0.3 - t) / 0.02});
        samples[i] = static_cast<float>(0.1 * fade * std::sin(2.0 * std::numbers::pi * 440.0 * t));
    }
    auto sound = audio.createSound(SoundData::create(1, rate, std::move(samples)).value());
    REQUIRE(sound.has_value());
    const SoundInstance instance = audio.play(*sound);
    REQUIRE(instance.isValid());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (audio.state(instance) != PlaybackState::Stopped && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(audio.state(instance) == PlaybackState::Stopped); // the device consumed the sound
    audio.update();
    CHECK_EQ(audio.activeVoiceCount(), 0);
}
