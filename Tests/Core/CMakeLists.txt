schuimm_add_test(CoreTests
    SOURCES
        ApplicationTests.cpp
        ClockTests.cpp
        CommonTests.cpp
        ImageTests.cpp
        InputStateTests.cpp
        LoggerTests.cpp
        PresentationTests.cpp
    LIBRARIES Schuimm::Application
    LABELS unit)
target_compile_definitions(CoreTests PRIVATE SCHUIMM_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
