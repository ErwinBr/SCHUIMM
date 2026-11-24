#include "Core/Audio/SoundData.h"

#include "Core/Common/StringBuilder.h"

namespace schuimm {

SoundData::SoundData(int channels, int sampleRate, std::vector<float> samples)
    : channels_(channels)
    , sampleRate_(sampleRate)
    , samples_(std::move(samples))
{
}

Result<SoundData> SoundData::create(int channels, int sampleRate, std::vector<float> samples)
{
    if (channels < 1 || channels > maxChannels) {
        return makeError(ErrorCode::InvalidArgument, concat("unsupported channel count ", channels, " (1 or 2)"));
    }
    if (sampleRate < minSampleRate || sampleRate > maxSampleRate) {
        return makeError(ErrorCode::InvalidArgument,
                         concat("unsupported sample rate ", sampleRate, " Hz (", minSampleRate, "..", maxSampleRate, ")"));
    }
    if (samples.size() % static_cast<std::size_t>(channels) != 0) {
        return makeError(ErrorCode::InvalidArgument,
                         concat(samples.size(), " samples do not form whole frames of ", channels, " channels"));
    }
    return SoundData(channels, sampleRate, std::move(samples));
}

} // namespace schuimm
