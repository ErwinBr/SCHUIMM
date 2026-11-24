#pragma once

// CPU-side audio data: the decoded form of a sound asset, independent of any audio device.
// It lives in Core for the same reason as Image: the resource layer produces it (WAV decoding)
// and the audio layer consumes it, and neither depends on the other.

#include "Core/Common/Error.h"

#include <cstddef>
#include <span>
#include <vector>

namespace schuimm {

// Interleaved 32-bit float PCM samples in [-1, 1]: frame i holds channels() samples starting
// at index i * channels(). Mono or stereo (left, right). A value type: copyable and movable.
class SoundData {
public:
    static constexpr int maxChannels = 2;
    static constexpr int minSampleRate = 1000;
    static constexpr int maxSampleRate = 384000;

    SoundData() = default;

    // Fails with InvalidArgument for an unsupported channel count or sample rate, or if the
    // sample count is not a multiple of the channel count.
    [[nodiscard]] static Result<SoundData> create(int channels, int sampleRate, std::vector<float> samples);

    [[nodiscard]] int channels() const noexcept { return channels_; }
    [[nodiscard]] int sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] std::size_t frameCount() const noexcept { return channels_ > 0 ? samples_.size() / static_cast<std::size_t>(channels_) : 0; }
    [[nodiscard]] double durationSeconds() const noexcept
    {
        return sampleRate_ > 0 ? static_cast<double>(frameCount()) / sampleRate_ : 0.0;
    }
    [[nodiscard]] bool isEmpty() const noexcept { return samples_.empty(); }
    [[nodiscard]] std::span<const float> samples() const noexcept { return samples_; }

private:
    SoundData(int channels, int sampleRate, std::vector<float> samples);

    int channels_ = 0;
    int sampleRate_ = 0;
    std::vector<float> samples_;
};

} // namespace schuimm
