# Room resources (*.room.json) and live room instances: the first part of the adventure layer.
# Depends on the resource layer (asset paths, locator, cache) and the script runtime (ScriptHost,
# programs, object handles), never on the script compiler. nlohmann/json is private: only
# RoomResourceLoader.cpp includes it.
add_library(schuimm_room STATIC
    RoomInstance.cpp
    RoomLibrary.cpp
    RoomResourceLoader.cpp
    RoomScript.cpp)
add_library(Schuimm::Room ALIAS schuimm_room)
target_link_libraries(schuimm_room
    PUBLIC Schuimm::Resource Schuimm::ScriptRuntime
    PRIVATE Schuimm::ThirdParty::Json)
schuimm_configure_target(schuimm_room)
