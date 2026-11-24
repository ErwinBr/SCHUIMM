#pragma once

#include "Audio/Sound.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace schuimm {

// The real-time half of the audio system. mix() runs on the audio thread; every other member
// is for the main thread. They share fixed per-voice state through atomics, so mix() never
// blocks, allocates, frees memory or performs I/O, and nothing the main thread sets can be
// lost: there is no queue that could fill up.
//
// Protocol (see Documentation/Architecture/Audio.md, "Threading model"):
// * Controls are state, not events: the desired run state (playing, paused, stopped) and the
//   volume of each voice, the master volume and "all paused". mix() reads the latest values
//   once per period; earlier values are simply superseded.
// * A playback is published with start(): its sound, loop flag, volume and run state are
//   written first, then its generation (release). mix() starts a playback whenever a voice's
//   generation differs from the one it last started there.
// * The main thread may call start() for a voice only after endedGeneration(voice) equals the
//   voice's previous generation (or nothing was ever played on it), and only with a different
//   generation. mix() writes the ended generation only after it has started and finished that
//   playback, so a published playback can never be overwritten before mix() has seen it.
//
// Voices refer to sounds by raw pointer; keeping each sound alive while a voice may still read
// it is the job of the owner (AudioDevice), which releases a sound only after the voice's
// ended generation shows that mix() has let go of it.
class Mixer {
public:
    static constexpr int voiceCount = 32;
    static constexpr int channels = 2;

    enum class Control : std::uint8_t { Stopped, Playing, Paused };

    // --- Main thread -----------------------------------------------------------------------
    // Publishes a new playback on `voice` (see the protocol above).
    void start(int voice, std::uint32_t generation, const Sound* sound, float volume, bool loop, bool paused) noexcept;
    // Desired state and volume of the voice's current playback.
    void setControl(int voice, Control control) noexcept;
    void setVolume(int voice, float volume) noexcept;
    void setMasterVolume(float volume) noexcept { masterVolume_.store(volume, std::memory_order_relaxed); }
    void setAllPaused(bool paused) noexcept { allPaused_.store(paused, std::memory_order_relaxed); }
    // The generation of the voice's last playback that mix() has finished with (ended
    // naturally or was stopped). The voice's sound is no longer read once this equals the
    // generation that was played.
    [[nodiscard]] std::uint32_t endedGeneration(int voice) const noexcept
    {
        return ended_[static_cast<std::size_t>(voice)].load(std::memory_order_acquire);
    }

    // --- Audio thread ----------------------------------------------------------------------
    // Writes `frames` interleaved stereo frames to `out` (overwriting it).
    void mix(float* out, std::uint32_t frames) noexcept;

private:
    // Written by the main thread, read by mix().
    struct Shared {
        std::atomic<std::uint32_t> generation{0}; // published last; 0 = nothing played yet
        std::atomic<const Sound*> sound{nullptr};
        std::atomic<bool> loop{false};
        std::atomic<Control> control{Control::Stopped};
        std::atomic<float> volume{1.0f};
    };

    // Owned by mix().
    struct Voice {
        const Sound* sound = nullptr;
        std::size_t position = 0; // next frame
        std::uint32_t generation = 0; // last playback started on this voice
        float gain = 1.0f;            // gain reached at the end of the previous buffer
        bool loop = false;
        bool active = false;
    };

    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    static_assert(std::atomic<const Sound*>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);
    static_assert(std::atomic<Control>::is_always_lock_free);
    static_assert(std::atomic<float>::is_always_lock_free);

    void end(Voice& voice, int index) noexcept;

    std::array<Shared, voiceCount> shared_{};
    std::array<Voice, voiceCount> voices_{};
    std::array<std::atomic<std::uint32_t>, voiceCount> ended_{}; // audio -> main
    std::atomic<float> masterVolume_{1.0f};
    std::atomic<bool> allPaused_{false};
    float masterGain_ = 1.0f; // owned by mix()
};

} // namespace schuimm
