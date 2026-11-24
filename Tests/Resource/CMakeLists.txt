# Resource layer: asset paths, locator, cache, CPU-side image decoding and font loading. Headless: no window,
# no OpenGL context (decoding must work without one).
schuimm_add_test(ResourceTests
    SOURCES
        AssetLocatorTests.cpp
        AssetPathTests.cpp
        FontFaceTests.cpp
        ImageDecoderTests.cpp
        ResourceCacheTests.cpp
        WavDecoderTests.cpp
    LIBRARIES Schuimm::Resource
    LABELS unit)
target_compile_definitions(ResourceTests PRIVATE
    SCHUIMM_TEST_ASSET_DIR="${PROJECT_SOURCE_DIR}/Tests/Assets"
    SCHUIMM_DEMO_ASSET_DIR="${PROJECT_SOURCE_DIR}/Applications/EngineDemo/Assets"
    SCHUIMM_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
