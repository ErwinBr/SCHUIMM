# Resource layer: asset paths, locator, cache and CPU-side image decoding. Headless: no window,
# no OpenGL context (decoding must work without one).
schuimm_add_test(ResourceTests
    SOURCES
        AssetLocatorTests.cpp
        AssetPathTests.cpp
        ImageDecoderTests.cpp
        ResourceCacheTests.cpp
    LIBRARIES Schuimm::Resource
    LABELS unit)
target_compile_definitions(ResourceTests PRIVATE
    SCHUIMM_TEST_ASSET_DIR="${PROJECT_SOURCE_DIR}/Tests/Assets"
    SCHUIMM_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
