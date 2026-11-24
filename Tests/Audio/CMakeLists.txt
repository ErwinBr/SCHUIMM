# Audio: playback management and mixing (manual backend) and the miniaudio device on its null
# backend are headless unit tests. AudioDeviceSmoke needs a real output device (label audio).
schuimm_add_test(AudioTests
    SOURCES
        AudioDeviceTests.cpp
        NullBackendTests.cpp
    LIBRARIES Schuimm::AudioMiniaudio
    LABELS unit)

schuimm_add_test(AudioDeviceSmoke
    SOURCES AudioDeviceSmoke.cpp
    LIBRARIES Schuimm::AudioMiniaudio
    LABELS audio)
set_tests_properties(AudioDeviceSmoke PROPERTIES TIMEOUT 60)
