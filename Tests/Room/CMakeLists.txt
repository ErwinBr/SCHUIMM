# Room resources and instances: JSON parsing and validation, asset verification, script
# association, object handles and coroutine cleanup. Headless: scripts are compiled in-process
# into a scratch asset root, images are only checked for existence.
schuimm_add_test(RoomTests
    SOURCES RoomTests.cpp
    LIBRARIES Schuimm::Room Schuimm::ScriptCompiler Schuimm::ThirdParty::Json
    LABELS unit)
target_include_directories(RoomTests PRIVATE
    "${PROJECT_SOURCE_DIR}/Tests/Resource"
    "${PROJECT_SOURCE_DIR}/Tests/Script")
target_compile_definitions(RoomTests PRIVATE
    SCHUIMM_TEST_ASSET_DIR="${PROJECT_SOURCE_DIR}/Tests/Assets"
    SCHUIMM_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    SCHUIMM_ROOM_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/Documentation/Rooms/Examples")
