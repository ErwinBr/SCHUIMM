schuimm_add_test(PlatformTests
    SOURCES KeyMapTests.cpp
    LIBRARIES Schuimm::Platform
    LABELS unit)

# Paths and file I/O; headless.
schuimm_add_test(FileSystemTests
    SOURCES
        FileSystemTests.cpp
        PathTests.cpp
    LIBRARIES Schuimm::FileSystem
    LABELS unit)
target_compile_definitions(FileSystemTests PRIVATE SCHUIMM_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")

# Native integration smoke test: creates a real window and OpenGL context, renders frames and
# shuts down. Needs a display (and on Linux an X server), so it is labelled separately:
#   ctest -L integration
if(SCHUIMM_BUILD_DEMO)
    add_test(NAME EngineDemoSmoke COMMAND EngineDemo --frames 30)
    set_tests_properties(EngineDemoSmoke PROPERTIES LABELS integration TIMEOUT 60)
    # Starts the courtyard script's demonstration and uses the fan (240 frames), then runs the timed
    # key interactions, including leaving (which cancels the demonstration) and re-entering rooms.
    add_test(NAME ActorDemoSmoke COMMAND ActorDemo --auto --frames 760)
    set_tests_properties(ActorDemoSmoke PROPERTIES LABELS integration TIMEOUT 60)
endif()
