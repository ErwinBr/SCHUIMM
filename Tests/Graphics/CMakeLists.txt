# Backend-independent graphics code and the texture cache: camera, CPU-side sprite batching
# and resources on top of a fake renderer. Headless: no window, no OpenGL context.
schuimm_add_test(GraphicsTests
    SOURCES
        CameraTests.cpp
        SpriteBatcherTests.cpp
        TextTests.cpp
        TextureCacheTests.cpp
    LIBRARIES Schuimm::GraphicsResources
    LABELS unit)
target_include_directories(GraphicsTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/Tests/Resource")
target_compile_definitions(GraphicsTests PRIVATE
    SCHUIMM_TEST_ASSET_DIR="${PROJECT_SOURCE_DIR}/Tests/Assets"
    SCHUIMM_DEMO_ASSET_DIR="${PROJECT_SOURCE_DIR}/Applications/EngineDemo/Assets"
    SCHUIMM_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
