# Room resources (*.room.json), actor resources (*.actor.json), and live room instances with their
# objects and actors: the adventure runtime. Depends on the resource layer (asset paths, locator,
# cache), animation, and the script runtime (ScriptHost, programs, object handles), never on the
# script compiler or a graphics API (RoomGraphics draws rooms). JSON decoding (Schuimm::ResourceJson,
# nlohmann/json) is private: no public header includes it.
add_library(schuimm_room STATIC
    ActorInstance.cpp
    ActorLibrary.cpp
    ActorMovement.cpp
    ActorRenderState.cpp
    ObjectAnimation.cpp
    ActorResource.cpp
    ActorResourceLoader.cpp
    Facing.cpp
    ObjectHandles.cpp
    Perspective.cpp
    RoomInstance.cpp
    RoomLibrary.cpp
    RoomResourceLoader.cpp
    RoomScript.cpp)
add_library(Schuimm::Room ALIAS schuimm_room)
target_link_libraries(schuimm_room
    PUBLIC Schuimm::Resource Schuimm::Animation Schuimm::Navigation Schuimm::ScriptRuntime
    PRIVATE Schuimm::ResourceJson)
schuimm_configure_target(schuimm_room)
