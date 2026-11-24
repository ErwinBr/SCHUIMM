# Display-dependent integration tests: they create real windows and OpenGL contexts, so they
# need a desktop session (on Linux an X server, e.g. Xvfb). Run them with `ctest -L integration`.
schuimm_add_test(LifecycleTests
    SOURCES LifecycleTests.cpp
    LIBRARIES Schuimm::Platform Schuimm::GraphicsOpenGL
    LABELS integration)
set_tests_properties(LifecycleTests PROPERTIES TIMEOUT 120)
