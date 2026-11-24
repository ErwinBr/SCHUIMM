#include "Audio/Mixer.h"

#include <algorithm>
#include <cstring>

namespace schuimm {

// --- Main thread ---------------------------------------------------------------------------

void Mixer::start(int voice, std::uint32_t generation, const Sound* sound, float volume, bool loop, bool paused) noexcept
{
    Shared& shared = shared_[static_cast<std::size_t>(voice)];
    shared.sound.store(sound, std::memory_order_relaxed);
    shared.loop.store(loop, std::memory_order_relaxed);
    shared.volume.store(volume, std::memory_order_relaxed);
    shared.control.store(paused ? Control::Paused : Control::Playing, std::memory_order_relaxed);
    // Publishes everything above: mix() reads the fields only after it has seen this value.
    shared.generation.store(generation, std::memory_order_release);
}

void Mixer::setControl(int voice, Control control) noexcept
{
    shared_[static_cast<std::size_t>(voice)].control.store(control, std::memory_order_relaxed);
}

void Mixer::setVolume(int voice, float volume) noexcept
{
    shared_[static_cast<std::size_t>(voice)].volume.store(volume, std::memory_order_relaxed);
}

// --- Audio thread --------------------------------------------------------------------------

void Mixer::end(Voice& voice, int index) noexcept
{
    voice.active = false;
    voice.sound = nullptr;
    // Release: the main thread may free the sound and reuse the voice once it sees this.
    ended_[static_cast<std::size_t>(index)].store(voice.generation, std::memory_order_release);
}

void Mixer::mix(float* out, std::uint32_t frames) noexcept
{
    std::memset(out, 0, sizeof(float) * static_cast<std::size_t>(frames) * channels);
    // Starts and stops are processed even while all output is paused, so stopped voices are
    // still handed back to the main thread.
    const bool allPaused = allPaused_.load(std::memory_order_relaxed);
    const bool mixing = !allPaused && frames > 0;
    const float inverseFrames = frames > 0 ? 1.0f / static_cast<float>(frames) : 0.0f;

    for (int index = 0; index < voiceCount; ++index) {
        Voice& voice = voices_[static_cast<std::size_t>(index)];
        const Shared& shared = shared_[static_cast<std::size_t>(index)];

        // A new playback was published on this voice.
        const std::uint32_t generation = shared.generation.load(std::memory_order_acquire);
        if (generation != voice.generation) {
            if (voice.active) {
                end(voice, index); // the main thread only reuses ended voices; be defensive
            }
            voice.generation = generation;
            voice.sound = shared.sound.load(std::memory_order_relaxed);
            voice.loop = shared.loop.load(std::memory_order_relaxed);
            voice.gain = shared.volume.load(std::memory_order_relaxed);
            voice.position = 0;
            voice.active = voice.sound != nullptr && voice.sound->frameCount() > 0;
            if (!voice.active) {
                end(voice, index);
            }
        }
        if (!voice.active) {
            continue;
        }

        const Control control = shared.control.load(std::memory_order_relaxed);
        if (control == Control::Stopped) {
            end(voice, index);
            continue;
        }
        if (control == Control::Paused || !mixing) {
            continue;
        }

        const float* samples = voice.sound->data();
        const std::size_t length = voice.sound->frameCount();
        // Volume changes are ramped over one buffer to avoid clicks.
        const float target = shared.volume.load(std::memory_order_relaxed);
        const float startGain = voice.gain;
        const float step = (target - startGain) * inverseFrames;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            if (voice.position >= length) {
                if (!voice.loop) {
                    break;
                }
                voice.position = 0;
            }
            const float gain = startGain + step * static_cast<float>(frame + 1);
            out[frame * 2] += samples[voice.position * 2] * gain;
            out[frame * 2 + 1] += samples[voice.position * 2 + 1] * gain;
            ++voice.position;
        }
        voice.gain = target;
        if (!voice.loop && voice.position >= length) {
            end(voice, index);
        }
    }

    if (!mixing) {
        return;
    }
    const float masterVolume = masterVolume_.load(std::memory_order_relaxed);
    const float startMaster = masterGain_;
    const float masterStep = (masterVolume - startMaster) * inverseFrames;
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const float gain = startMaster + masterStep * static_cast<float>(frame + 1);
        out[frame * 2] = std::clamp(out[frame * 2] * gain, -1.0f, 1.0f);
        out[frame * 2 + 1] = std::clamp(out[frame * 2 + 1] * gain, -1.0f, 1.0f);
    }
    masterGain_ = masterVolume;
}

} // namespace schuimm
