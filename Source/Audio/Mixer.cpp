#include "Audio/Mixer.h"

#include <algorithm>
#include <cstring>

namespace schuimm {

void Mixer::end(Voice& voice, int index) noexcept
{
    voice.active = false;
    voice.sound = nullptr;
    ended_[static_cast<std::size_t>(index)].store(voice.generation, std::memory_order_release);
}

void Mixer::apply(const Command& command) noexcept
{
    // Commands that are not about one voice.
    switch (command.type) {
    case CommandType::SetMasterVolume: masterVolume_ = command.volume; return;
    case CommandType::PauseAll: allPaused_ = true; return;
    case CommandType::ResumeAll: allPaused_ = false; return;
    default: break;
    }
    if (command.voice >= voiceCount) {
        return;
    }
    const int index = command.voice;
    Voice& voice = voices_[static_cast<std::size_t>(index)];
    // Commands for an earlier playback of the voice are stale.
    const bool current = voice.active && voice.generation == command.generation;

    switch (command.type) {
    case CommandType::Play:
        if (voice.active) {
            end(voice, index); // the main thread only reuses ended voices; be defensive
        }
        voice.sound = command.sound;
        voice.position = 0;
        voice.generation = command.generation;
        voice.volume = command.volume;
        voice.gain = command.volume;
        voice.loop = command.loop;
        voice.paused = command.paused;
        voice.active = command.sound != nullptr && command.sound->frameCount() > 0;
        if (!voice.active) {
            end(voice, index);
        }
        break;
    case CommandType::Stop:
        if (current) {
            end(voice, index);
        }
        break;
    case CommandType::Pause:
        if (current) {
            voice.paused = true;
        }
        break;
    case CommandType::Resume:
        if (current) {
            voice.paused = false;
        }
        break;
    case CommandType::SetVolume:
        if (current) {
            voice.volume = command.volume;
        }
        break;
    case CommandType::SetMasterVolume:
    case CommandType::PauseAll:
    case CommandType::ResumeAll:
        break; // handled above
    }
}

void Mixer::mix(float* out, std::uint32_t frames) noexcept
{
    Command command;
    while (commands_.pop(command)) {
        apply(command);
    }

    std::memset(out, 0, sizeof(float) * static_cast<std::size_t>(frames) * channels);
    if (allPaused_ || frames == 0) {
        return;
    }

    const float inverseFrames = 1.0f / static_cast<float>(frames);
    for (int index = 0; index < voiceCount; ++index) {
        Voice& voice = voices_[static_cast<std::size_t>(index)];
        if (!voice.active || voice.paused) {
            continue;
        }
        const float* samples = voice.sound->data();
        const std::size_t length = voice.sound->frameCount();
        // Volume changes are ramped over one buffer to avoid clicks.
        const float startGain = voice.gain;
        const float step = (voice.volume - startGain) * inverseFrames;
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
        voice.gain = voice.volume;
        if (!voice.loop && voice.position >= length) {
            end(voice, index);
        }
    }

    const float startMaster = masterGain_;
    const float masterStep = (masterVolume_ - startMaster) * inverseFrames;
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const float gain = startMaster + masterStep * static_cast<float>(frame + 1);
        out[frame * 2] = std::clamp(out[frame * 2] * gain, -1.0f, 1.0f);
        out[frame * 2 + 1] = std::clamp(out[frame * 2 + 1] * gain, -1.0f, 1.0f);
    }
    masterGain_ = masterVolume_;
}

} // namespace schuimm
