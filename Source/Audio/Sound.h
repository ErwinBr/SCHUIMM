#pragma once

#include <cstddef>
#include <vector>

namespace schuimm {

// A sound ready for playback on one AudioDevice: stereo 32-bit float samples at the device's
// sample rate (converted once, on the main thread, by AudioDevice::createSound). Immutable and
// shared (std::shared_ptr<const Sound>); a playing voice keeps it alive. A Sound holds no
// reference to the device, so it may outlive it.
class Sound {
public:
    Sound(int sampleRate, std::vector<float> stereoSamples)
        : sampleRate_(sampleRate)
        , samples_(std::move(stereoSamples))
    {
    }

    [[nodiscard]] int sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] std::size_t frameCount() const noexcept { return samples_.size() / 2; }
    [[nodiscard]] double durationSeconds() const noexcept
    {
        return sampleRate_ > 0 ? static_cast<double>(frameCount()) / sampleRate_ : 0.0;
    }
    // Interleaved left/right.
    [[nodiscard]] const float* data() const noexcept { return samples_.data(); }

private:
    int sampleRate_;
    std::vector<float> samples_;
};

} // namespace schuimm
