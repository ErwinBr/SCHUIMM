// The only engine source file that includes miniaudio.h.

#include "Audio/Miniaudio/MiniaudioAudioDevice.h"

#include "Core/Common/StringBuilder.h"

#include <miniaudio.h>

namespace schuimm {

namespace {

std::string resultText(ma_result result)
{
    return concat(ma_result_description(result), " (", static_cast<int>(result), ")");
}

class MiniaudioBackend final : public AudioBackend {
public:
    ~MiniaudioBackend() override
    {
        if (deviceInitialised_) {
            ma_device_uninit(&device_); // stops the device and joins its thread
        }
        if (contextInitialised_) {
            ma_context_uninit(&context_);
        }
    }

    Status initialise(const AudioDeviceSettings& settings)
    {
        // Backends in priority order. The null backend is only used when explicitly requested,
        // so a machine without audio output is reported instead of playing into nothing.
        const ma_backend platformBackends[] = {ma_backend_wasapi, ma_backend_dsound, ma_backend_coreaudio,
                                               ma_backend_pulseaudio, ma_backend_alsa};
        const ma_backend nullBackend[] = {ma_backend_null};
        const ma_backend* backends = settings.nullBackend ? nullBackend : platformBackends;
        const ma_uint32 backendCount = settings.nullBackend ? 1u : static_cast<ma_uint32>(std::size(platformBackends));

        ma_context_config contextConfig = ma_context_config_init();
        if (const ma_result result = ma_context_init(backends, backendCount, &contextConfig, &context_); result != MA_SUCCESS) {
            return makeError(ErrorCode::Unsupported, concat("no audio backend available: ", resultText(result)));
        }
        contextInitialised_ = true;

        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = 2;
        config.sampleRate = 0; // the device's native rate: no resampling in the real-time path
        config.periodSizeInMilliseconds = static_cast<ma_uint32>(settings.periodMilliseconds > 0 ? settings.periodMilliseconds : 10);
        config.dataCallback = &MiniaudioBackend::dataCallback;
        config.pUserData = this;
        if (const ma_result result = ma_device_init(&context_, &config, &device_); result != MA_SUCCESS) {
            return makeError(ErrorCode::Unsupported, concat("cannot open the audio output device (", ma_get_backend_name(context_.backend),
                                                            "): ", resultText(result)));
        }
        deviceInitialised_ = true;
        return {};
    }

    Status start(RenderCallback callback, void* user) override
    {
        callback_ = callback;
        user_ = user;
        if (const ma_result result = ma_device_start(&device_); result != MA_SUCCESS) {
            return makeError(ErrorCode::PlatformFailure, concat("cannot start audio playback: ", resultText(result)));
        }
        return {};
    }

    void stop() override
    {
        if (deviceInitialised_) {
            ma_device_stop(&device_); // returns once the callback is no longer running
        }
    }

    int sampleRate() const override { return static_cast<int>(device_.sampleRate); }
    std::string backendName() const override { return ma_get_backend_name(context_.backend); }
    std::string deviceName() const override { return device_.playback.name; }

private:
    static void dataCallback(ma_device* device, void* output, const void* input, ma_uint32 frames)
    {
        (void)input;
        auto* self = static_cast<MiniaudioBackend*>(device->pUserData);
        self->callback_(self->user_, static_cast<float*>(output), frames);
    }

    ma_context context_{};
    ma_device device_{};
    bool contextInitialised_ = false;
    bool deviceInitialised_ = false;
    RenderCallback callback_ = nullptr;
    void* user_ = nullptr;
};

} // namespace

Result<std::unique_ptr<AudioDevice>> createMiniaudioAudioDevice(const AudioDeviceSettings& settings, Logger& logger)
{
    auto backend = std::make_unique<MiniaudioBackend>();
    if (auto status = backend->initialise(settings); !status) {
        return std::unexpected(status.error());
    }
    return AudioDevice::create(std::move(backend), logger);
}

} // namespace schuimm
