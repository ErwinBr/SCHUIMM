schuimm_add_test(PlatformTests
    SOURCES KeyMapTests.cpp
    LIBRARIES Schuimm::Platform
    LABELS unit)

# Native integration smoke test: creates a real window and OpenGL context, renders frames and
# shuts down. Needs a display (and on Linux an X server), so it is labelled separately:
#   ctest -L integration
if(SCHUIMM_BUILD_DEMO)
    add_test(NAME EngineDemoSmoke COMMAND EngineDemo --frames 30)
    set_tests_properties(EngineDemoSmoke PROPERTIES LABELS integration TIMEOUT 60)
endif()
