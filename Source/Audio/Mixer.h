#pragma once

#include "Audio/Sound.h"
#include "Audio/SpscQueue.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace schuimm {

// The real-time half of the audio system. mix() runs on the audio thread; every other member
// is for the main thread. They communicate only through a lock-free command queue (main ->
// audio) and per-voice atomic "ended" generations (audio -> main), so mix() never blocks,
// allocates, frees memory or performs I/O.
//
// Voices refer to sounds by raw pointer; keeping each sound alive while a voice may still read
// it is the job of the owner (AudioDevice), which releases a sound only after the voice's
// ended generation shows that mix() has let go of it.
class Mixer {
public:
    static constexpr int voiceCount = 32;
    static constexpr int channels = 2;
    static constexpr std::size_t commandCapacity = 1024;

    enum class CommandType : std::uint8_t { Play, Stop, Pause, Resume, SetVolume, SetMasterVolume, PauseAll, ResumeAll };

    struct Command {
        CommandType type = CommandType::Stop;
        std::uint8_t voice = 0;
        bool loop = false;
        bool paused = false;
        std::uint32_t generation = 0;
        float volume = 1.0f;
        const Sound* sound = nullptr;
    };

    // --- Main thread -----------------------------------------------------------------------
    // Returns false if the queue is full (the command is not queued).
    bool post(const Command& command) noexcept { return commands_.push(command); }
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
    struct Voice {
        const Sound* sound = nullptr;
        std::size_t position = 0; // next frame
        std::uint32_t generation = 0;
        float volume = 1.0f;      // target gain
        float gain = 1.0f;        // gain reached at the end of the previous buffer
        bool loop = false;
        bool paused = false;
        bool active = false;
    };

    void apply(const Command& command) noexcept;
    void end(Voice& voice, int index) noexcept;

    SpscQueue<Command, commandCapacity> commands_;
    std::array<Voice, voiceCount> voices_{};
    std::array<std::atomic<std::uint32_t>, voiceCount> ended_{};
    float masterVolume_ = 1.0f;
    float masterGain_ = 1.0f;
    bool allPaused_ = false;
};

} // namespace schuimm
