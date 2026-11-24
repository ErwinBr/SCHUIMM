#pragma once

#include "Audio/AudioDevice.h"
#include "Core/Common/Error.h"
#include "Core/Logging/Logger.h"

#include <memory>

namespace schuimm {

struct AudioDeviceSettings {
    // false: the platform's audio APIs (WASAPI/DirectSound, CoreAudio, PulseAudio/ALSA); fails
    // if no output device is available.
    // true: miniaudio's null backend, which runs a real audio thread at a fixed rate without
    // producing sound. For headless tests and CI.
    bool nullBackend = false;
    // Requested length of one audio callback period; the backend may choose another.
    int periodMilliseconds = 10;
};

// Creates an AudioDevice that plays through miniaudio's low-level device API. The miniaudio
// headers are private to this backend.
[[nodiscard]] Result<std::unique_ptr<AudioDevice>> createMiniaudioAudioDevice(const AudioDeviceSettings& settings,
                                                                             Logger& logger);

} // namespace schuimm
