#pragma once

#include "Audio/Mixer.h"
#include "Audio/Sound.h"
#include "Core/Audio/SoundData.h"
#include "Core/Common/Error.h"
#include "Core/Logging/Logger.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace schuimm {

// The platform side of audio output: opens an output stream and calls the render callback
// from its own (real-time) thread. Implemented by MiniaudioBackend; tests use a manual one.
class AudioBackend {
public:
    // Fills `frames` interleaved stereo float frames. Called on the backend's audio thread.
    using RenderCallback = void (*)(void* user, float* out, std::uint32_t frames);

    virtual ~AudioBackend() = default;
    virtual Status start(RenderCallback callback, void* user) = 0;
    // After stop() returns the callback is not running and will not be called again.
    virtual void stop() = 0;
    [[nodiscard]] virtual int sampleRate() const = 0;
    [[nodiscard]] virtual std::string backendName() const = 0;
    [[nodiscard]] virtual std::string deviceName() const = 0;
};

enum class PlaybackState {
    Stopped,
    Playing,
    Paused,
};

[[nodiscard]] const char* toString(PlaybackState state) noexcept;

struct PlaybackSettings {
    float volume = 1.0f; // linear gain, >= 0
    bool loop = false;
    bool startPaused = false;
};

// Handle of one playback. A value: copying it does not copy the playback. Once the playback
// has ended the handle is stale: state() reports Stopped and control calls do nothing.
struct SoundInstance {
    static constexpr std::uint32_t invalidVoice = 0xFFFFFFFFu;
    std::uint32_t voice = invalidVoice;
    std::uint32_t generation = 0;

    [[nodiscard]] bool isValid() const noexcept { return voice != invalidVoice; }
    friend bool operator==(const SoundInstance&, const SoundInstance&) = default;
};

// Audio output: owns the backend stream and the mixer, and manages playbacks.
//
// Threading: every member function must be called from one thread (the main thread). Mixing
// runs on the backend's audio thread, which only executes Mixer::mix(); see Mixer for how the
// two communicate without locks. Nothing here blocks on the audio thread, so playback control
// never stalls the main loop, and no control call can be lost: each one sets state that the
// next audio period applies. state() and volume() report that state.
//
// Ownership: sounds are shared (std::shared_ptr<const Sound>). A playing voice keeps its sound
// alive; it is released by update() on the main thread once the audio thread has finished with
// it, never on the audio thread. Sounds do not reference the device and may outlive it.
// The device must be destroyed before the platform layer; destruction stops the stream first.
//
// Up to Mixer::voiceCount sounds play at the same time; play() returns an invalid instance when
// all voices are busy.
class AudioDevice {
public:
    // Starts `backend`. `logger` must outlive the device.
    [[nodiscard]] static Result<std::unique_ptr<AudioDevice>> create(std::unique_ptr<AudioBackend> backend, Logger& logger);
    ~AudioDevice();

    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    [[nodiscard]] int sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] const std::string& backendName() const noexcept { return backendName_; }
    [[nodiscard]] const std::string& deviceName() const noexcept { return deviceName_; }

    // Converts decoded data to the device format (stereo, device sample rate; linear
    // resampling). Main thread work: call it at load time, not per frame.
    [[nodiscard]] Result<std::shared_ptr<const Sound>> createSound(const SoundData& data) const;

    [[nodiscard]] SoundInstance play(std::shared_ptr<const Sound> sound, PlaybackSettings settings = {});
    void stop(SoundInstance instance);
    void pause(SoundInstance instance);
    void resume(SoundInstance instance);
    void setVolume(SoundInstance instance, float volume);
    [[nodiscard]] PlaybackState state(SoundInstance instance) const;
    [[nodiscard]] float volume(SoundInstance instance) const;

    void setMasterVolume(float volume);
    [[nodiscard]] float masterVolume() const noexcept { return masterVolume_; }
    void stopAll();
    // Silences all output without changing the playbacks' own state (e.g. while suspended).
    void pauseAll();
    void resumeAll();
    [[nodiscard]] bool isPausedAll() const noexcept { return pausedAll_; }

    // Collects playbacks that ended and releases their sounds. Call once per frame.
    void update();

    // Playbacks that have not ended (playing or paused).
    [[nodiscard]] int activeVoiceCount() const noexcept;

private:
    struct VoiceSlot {
        std::shared_ptr<const Sound> sound; // keeps the sound alive while the mixer may read it
        std::uint32_t generation = 0;
        PlaybackState state = PlaybackState::Stopped;
        float volume = 1.0f;
        bool inUse = false; // until the mixer confirmed it is done with the sound
    };

    AudioDevice(std::unique_ptr<AudioBackend> backend, Logger& logger);
    static void render(void* user, float* out, std::uint32_t frames);
    VoiceSlot* slotFor(SoundInstance instance);
    [[nodiscard]] const VoiceSlot* slotFor(SoundInstance instance) const;

    std::unique_ptr<AudioBackend> backend_;
    Logger& logger_;
    std::unique_ptr<Mixer> mixer_; // on the heap: its address is shared with the audio thread
    std::array<VoiceSlot, Mixer::voiceCount> slots_{};
    std::uint32_t nextGeneration_ = 1;
    float masterVolume_ = 1.0f;
    bool pausedAll_ = false;
    bool started_ = false;
    int sampleRate_ = 0;
    std::string backendName_;
    std::string deviceName_;
};

} // namespace schuimm
