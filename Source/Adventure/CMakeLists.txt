# The adventure layer.
#   schuimm_adventure_api   <- schuimm_script_bytecode   the interface declaration only (the compiler links it)
#   schuimm_game_resource   <- resource                  the game resource (*.game.json)
#   schuimm_game_build      <- compiler, room, game_resource, adventure_api
#                              the game build: source discovery and project compilation (tools only)
#   schuimm_adventure       <- room, graphics, script runtime, adventure_api, game_resource
#                              AdventureContext, AdventureNatives, AdventureRuntime
add_library(schuimm_adventure_api STATIC AdventureInterface.cpp)
add_library(Schuimm::AdventureApi ALIAS schuimm_adventure_api)
target_link_libraries(schuimm_adventure_api PUBLIC Schuimm::ScriptBytecode)
schuimm_configure_target(schuimm_adventure_api)

add_library(schuimm_game_resource STATIC GameResource.cpp)
add_library(Schuimm::GameResource ALIAS schuimm_game_resource)
target_link_libraries(schuimm_game_resource PUBLIC Schuimm::Resource PRIVATE Schuimm::ResourceJson)
schuimm_configure_target(schuimm_game_resource)

add_library(schuimm_game_build STATIC GameBuild.cpp)
add_library(Schuimm::GameBuild ALIAS schuimm_game_build)
target_link_libraries(schuimm_game_build PUBLIC Schuimm::GameResource Schuimm::ScriptCompiler Schuimm::Room Schuimm::AdventureApi)
schuimm_configure_target(schuimm_game_build)

add_library(schuimm_adventure STATIC AdventureContext.cpp AdventureRuntime.cpp)
add_library(Schuimm::Adventure ALIAS schuimm_adventure)
target_link_libraries(schuimm_adventure PUBLIC Schuimm::AdventureApi Schuimm::GameResource Schuimm::Room Schuimm::Graphics
                      Schuimm::ScriptRuntime)
schuimm_configure_target(schuimm_adventure)
