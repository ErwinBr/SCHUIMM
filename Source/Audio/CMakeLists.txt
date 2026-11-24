# Audio layer: playback control, mixing and the real-time thread contract. Independent of
# graphics and of any audio API; headless-testable with a manual backend.
add_library(schuimm_audio STATIC
    AudioDevice.cpp
    Mixer.cpp)
add_library(Schuimm::Audio ALIAS schuimm_audio)
target_link_libraries(schuimm_audio PUBLIC Schuimm::Core)
schuimm_configure_target(schuimm_audio)

# Output through miniaudio's low-level device API (the only user of miniaudio.h).
add_library(schuimm_audio_miniaudio STATIC
    Miniaudio/MiniaudioAudioDevice.cpp)
add_library(Schuimm::AudioMiniaudio ALIAS schuimm_audio_miniaudio)
target_link_libraries(schuimm_audio_miniaudio
    PUBLIC Schuimm::Audio
    PRIVATE Schuimm::ThirdParty::Miniaudio)
schuimm_configure_target(schuimm_audio_miniaudio)
