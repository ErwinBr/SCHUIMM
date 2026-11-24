#pragma once

#include "Core/Audio/SoundData.h"
#include "Core/Common/Error.h"
#include "Resource/AssetLocator.h"
#include "Resource/AssetPath.h"

#include <cstddef>
#include <span>
#include <string_view>

namespace schuimm {

// Decodes a RIFF/WAVE file held in memory.
//
// Supported: uncompressed PCM with 8-bit (unsigned), 16-, 24- or 32-bit (signed) integer
// samples, and 32-bit IEEE float; mono or stereo; WAVE_FORMAT_EXTENSIBLE when its sub-format
// is PCM or float. Unknown chunks are skipped. Samples are converted to 32-bit float.
//
// Anything else (compressed codecs, more than two channels, missing or truncated chunks) fails
// with InvalidData and a message naming `sourceName` and the reason.
[[nodiscard]] Result<SoundData> decodeWav(std::span<const std::byte> data, std::string_view sourceName);

// ResourceCache<SoundData> loader: reads the asset and decodes it.
[[nodiscard]] Result<SoundData> loadSound(const AssetLocator& locator, const AssetPath& asset);

} // namespace schuimm
