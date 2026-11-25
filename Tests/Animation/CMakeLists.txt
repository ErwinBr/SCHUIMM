# Animation resources, playback and tick timing. Headless: image sizes come from a fake (or from
# PNG headers in a scratch asset root), no graphics context.
schuimm_add_test(AnimationTests
    SOURCES AnimationTests.cpp
    LIBRARIES Schuimm::Animation
    LABELS unit)
target_include_directories(AnimationTests PRIVATE "${PROJECT_SOURCE_DIR}/Tests/Resource")
target_compile_definitions(AnimationTests PRIVATE
    SCHUIMM_TEST_ASSET_DIR="${PROJECT_SOURCE_DIR}/Tests/Assets"
    SCHUIMM_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
