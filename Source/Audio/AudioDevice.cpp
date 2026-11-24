#include "Audio/AudioDevice.h"

#include "Core/Common/Assert.h"
#include "Core/Common/StringBuilder.h"

#include <algorithm>
#include <cmath>

namespace schuimm {

namespace {
constexpr const char* category = "Audio";

float sanitiseVolume(float volume)
{
    return std::isfinite(volume) ? std::max(0.0f, volume) : 0.0f;
}
} // namespace

const char* toString(PlaybackState state) noexcept
{
    switch (state) {
    case PlaybackState::Stopped: return "Stopped";
    case PlaybackState::Playing: return "Playing";
    case PlaybackState::Paused: return "Paused";
    }
    return "?";
}

AudioDevice::AudioDevice(std::unique_ptr<AudioBackend> backend, Logger& logger)
    : backend_(std::move(backend))
    , logger_(logger)
    , mixer_(std::make_unique<Mixer>())
    , sampleRate_(backend_->sampleRate())
    , backendName_(backend_->backendName())
    , deviceName_(backend_->deviceName())
{
}

Result<std::unique_ptr<AudioDevice>> AudioDevice::create(std::unique_ptr<AudioBackend> backend, Logger& logger)
{
    if (!backend) {
        return makeError(ErrorCode::InvalidArgument, "an audio backend is required");
    }
    if (backend->sampleRate() < SoundData::minSampleRate || backend->sampleRate() > SoundData::maxSampleRate) {
        return makeError(ErrorCode::Unsupported, concat("unsupported device sample rate ", backend->sampleRate(), " Hz"));
    }
    std::unique_ptr<AudioDevice> device(new AudioDevice(std::move(backend), logger));
    if (auto status = device->backend_->start(&AudioDevice::render, device->mixer_.get()); !status) {
        return std::unexpected(status.error());
    }
    device->started_ = true;
    logger.info(category, concat("Audio device started: ", device->backendName_, ", '", device->deviceName_, "', ",
                                 device->sampleRate_, " Hz stereo, ", Mixer::voiceCount, " voices"));
    return device;
}

AudioDevice::~AudioDevice()
{
    // Stop the audio thread before the mixer and the sounds it reads go away.
    if (started_) {
        backend_->stop();
    }
    logger_.info(category, "Audio device stopped");
}

void AudioDevice::render(void* user, float* out, std::uint32_t frames)
{
    static_cast<Mixer*>(user)->mix(out, frames);
}

Result<std::shared_ptr<const Sound>> AudioDevice::createSound(const SoundData& data) const
{
    if (data.isEmpty()) {
        return makeError(ErrorCode::InvalidArgument, "cannot create a sound from empty data");
    }
    const std::size_t sourceFrames = data.frameCount();
    const std::span<const float> source = data.samples();
    const bool mono = data.channels() == 1;
    const auto sampleAt = [&](std::size_t frame, int channel) {
        return mono ? source[frame] : source[frame * 2 + static_cast<std::size_t>(channel)];
    };

    const double ratio = static_cast<double>(data.sampleRate()) / sampleRate_;
    const auto frames = static_cast<std::size_t>(std::llround(static_cast<double>(sourceFrames) / ratio));
    std::vector<float> stereo(std::max<std::size_t>(frames, 1) * 2);
    for (std::size_t frame = 0; frame < stereo.size() / 2; ++frame) {
        // Linear interpolation between the two nearest source frames.
        const double position = static_cast<double>(frame) * ratio;
        const auto i0 = std::min(static_cast<std::size_t>(position), sourceFrames - 1);
        const std::size_t i1 = std::min(i0 + 1, sourceFrames - 1);
        const auto t = static_cast<float>(position - static_cast<double>(i0));
        for (int channel = 0; channel < 2; ++channel) {
            const float a = sampleAt(i0, channel);
            const float b = sampleAt(i1, channel);
            stereo[frame * 2 + static_cast<std::size_t>(channel)] = a + (b - a) * t;
        }
    }
    return std::make_shared<const Sound>(sampleRate_, std::move(stereo));
}

AudioDevice::VoiceSlot* AudioDevice::slotFor(SoundInstance instance)
{
    if (!instance.isValid() || instance.voice >= slots_.size()) {
        return nullptr;
    }
    VoiceSlot& slot = slots_[instance.voice];
    return slot.generation == instance.generation && slot.state != PlaybackState::Stopped ? &slot : nullptr;
}

const AudioDevice::VoiceSlot* AudioDevice::slotFor(SoundInstance instance) const
{
    return const_cast<AudioDevice*>(this)->slotFor(instance);
}

SoundInstance AudioDevice::play(std::shared_ptr<const Sound> sound, PlaybackSettings settings)
{
    if (!sound) {
        return {};
    }
    if (sound->sampleRate() != sampleRate_) {
        logger_.warning(category, concat("Sound for ", sound->sampleRate(), " Hz cannot play on a ", sampleRate_,
                                         " Hz device; create it with this device's createSound()"));
        return {};
    }
    update(); // reclaim voices that ended since the last frame
    const auto free = std::find_if(slots_.begin(), slots_.end(), [](const VoiceSlot& slot) { return !slot.inUse; });
    if (free == slots_.end()) {
        return {};
    }
    const auto voice = static_cast<std::uint32_t>(free - slots_.begin());
    VoiceSlot& slot = *free;
    // The mixer starts a playback when the voice's generation changes, so the new one must
    // differ from the voice's previous one (only relevant when the counter wraps) and from 0,
    // which means "nothing played yet".
    std::uint32_t generation = nextGeneration_;
    while (generation == 0 || generation == slot.generation) {
        ++generation;
    }
    nextGeneration_ = generation + 1;
    SCHUIMM_ASSERT(mixer_->endedGeneration(static_cast<int>(voice)) == slot.generation,
                   "a voice is reused only after the mixer has finished with it");

    slot.sound = std::move(sound);
    slot.generation = generation;
    slot.state = settings.startPaused ? PlaybackState::Paused : PlaybackState::Playing;
    slot.volume = sanitiseVolume(settings.volume);
    slot.inUse = true;
    mixer_->start(static_cast<int>(voice), generation, slot.sound.get(), slot.volume, settings.loop, settings.startPaused);
    return {voice, generation};
}

void AudioDevice::stop(SoundInstance instance)
{
    if (VoiceSlot* slot = slotFor(instance)) {
        slot->state = PlaybackState::Stopped; // the sound is released once the mixer confirms
        mixer_->setControl(static_cast<int>(instance.voice), Mixer::Control::Stopped);
    }
}

void AudioDevice::pause(SoundInstance instance)
{
    if (VoiceSlot* slot = slotFor(instance); slot != nullptr && slot->state == PlaybackState::Playing) {
        slot->state = PlaybackState::Paused;
        mixer_->setControl(static_cast<int>(instance.voice), Mixer::Control::Paused);
    }
}

void AudioDevice::resume(SoundInstance instance)
{
    if (VoiceSlot* slot = slotFor(instance); slot != nullptr && slot->state == PlaybackState::Paused) {
        slot->state = PlaybackState::Playing;
        mixer_->setControl(static_cast<int>(instance.voice), Mixer::Control::Playing);
    }
}

void AudioDevice::setVolume(SoundInstance instance, float volume)
{
    if (VoiceSlot* slot = slotFor(instance)) {
        slot->volume = sanitiseVolume(volume);
        mixer_->setVolume(static_cast<int>(instance.voice), slot->volume);
    }
}

PlaybackState AudioDevice::state(SoundInstance instance) const
{
    // Reflect playbacks that ended on the audio thread even if update() was not called yet.
    const VoiceSlot* slot = slotFor(instance);
    if (slot == nullptr || mixer_->endedGeneration(static_cast<int>(instance.voice)) == instance.generation) {
        return PlaybackState::Stopped;
    }
    return slot->state;
}

float AudioDevice::volume(SoundInstance instance) const
{
    const VoiceSlot* slot = slotFor(instance);
    return slot != nullptr ? slot->volume : 0.0f;
}

void AudioDevice::setMasterVolume(float volume)
{
    masterVolume_ = sanitiseVolume(volume);
    mixer_->setMasterVolume(masterVolume_);
}

void AudioDevice::stopAll()
{
    for (std::uint32_t voice = 0; voice < slots_.size(); ++voice) {
        stop({voice, slots_[voice].generation});
    }
}

void AudioDevice::pauseAll()
{
    pausedAll_ = true;
    mixer_->setAllPaused(true);
}

void AudioDevice::resumeAll()
{
    pausedAll_ = false;
    mixer_->setAllPaused(false);
}

void AudioDevice::update()
{
    for (std::size_t voice = 0; voice < slots_.size(); ++voice) {
        VoiceSlot& slot = slots_[voice];
        if (slot.inUse && mixer_->endedGeneration(static_cast<int>(voice)) == slot.generation) {
            slot.inUse = false;
            slot.state = PlaybackState::Stopped;
            slot.sound.reset(); // main thread: freeing memory is fine here
        }
    }
}

int AudioDevice::activeVoiceCount() const noexcept
{
    int count = 0;
    for (std::size_t voice = 0; voice < slots_.size(); ++voice) {
        const VoiceSlot& slot = slots_[voice];
        if (slot.inUse && slot.state != PlaybackState::Stopped &&
            mixer_->endedGeneration(static_cast<int>(voice)) != slot.generation) {
            ++count;
        }
    }
    return count;
}

} // namespace schuimm
