# The adventure scripting API.
#   schuimm_adventure_api  <- schuimm_script_bytecode   the interface declaration only (the compiler links it)
#   schuimm_adventure      <- room, graphics, script runtime, adventure_api
#                             AdventureContext: binds the natives to the active room and camera
add_library(schuimm_adventure_api STATIC AdventureInterface.cpp)
add_library(Schuimm::AdventureApi ALIAS schuimm_adventure_api)
target_link_libraries(schuimm_adventure_api PUBLIC Schuimm::ScriptBytecode)
schuimm_configure_target(schuimm_adventure_api)

add_library(schuimm_adventure STATIC AdventureContext.cpp)
add_library(Schuimm::Adventure ALIAS schuimm_adventure)
target_link_libraries(schuimm_adventure PUBLIC Schuimm::AdventureApi Schuimm::Room Schuimm::Graphics Schuimm::ScriptRuntime)
schuimm_configure_target(schuimm_adventure)
