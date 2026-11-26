# The adventure scripting API: interface/binding, entity references, state natives, walk_to,
# animation operations, camera and room lifetime. Headless (PNG headers only).
schuimm_add_test(AdventureTests
    SOURCES AdventureTests.cpp
    LIBRARIES Schuimm::Adventure Schuimm::ScriptCompiler
    LABELS unit)
target_include_directories(AdventureTests PRIVATE
    "${PROJECT_SOURCE_DIR}/Tests/Resource"
    "${PROJECT_SOURCE_DIR}/Tests/Script")
target_compile_definitions(AdventureTests PRIVATE
    SCHUIMM_TEST_ASSET_DIR="${PROJECT_SOURCE_DIR}/Tests/Assets"
    SCHUIMM_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
