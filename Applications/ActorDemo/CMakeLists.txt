# ActorDemo: rooms with actors (Applications/ActorDemo/Assets), to see actor resources,
# directional animation, mirroring, pivots, scale, visibility, room lifetime, navigation and
# script-driven adventure behaviour (the courtyard's room script) at work.
add_executable(ActorDemo
    ActorDemo.cpp
    main.cpp
    ../EngineDemo/Screenshot.cpp) # PPM writer for --screenshot
target_link_libraries(ActorDemo PRIVATE
    Schuimm::Application
    Schuimm::GraphicsOpenGL
    Schuimm::Platform
    Schuimm::Adventure
    Schuimm::RoomGraphics)
target_include_directories(ActorDemo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../EngineDemo")
schuimm_configure_target(ActorDemo)

if(APPLE)
    set_target_properties(ActorDemo PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.schuimm.ActorDemo"
        MACOSX_BUNDLE_BUNDLE_NAME "ActorDemo"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    set(actor_demo_asset_destination "$<TARGET_BUNDLE_CONTENT_DIR:ActorDemo>/Resources/Assets")
else()
    # Its own directory (bin/ActorDemo/<config>): the asset root is "Assets" next to the
    # executable, and EngineDemo deploys its own Assets next to itself in bin/<config>. Sharing
    # that directory would let each demo's deployment replace the other's assets.
    set_target_properties(ActorDemo PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/ActorDemo")
    set(actor_demo_asset_destination "$<TARGET_FILE_DIR:ActorDemo>/Assets")
endif()

# Scripts, compiled with schuimmc at build time:
#   * the walker's actor script: referenced by walker.actor.json (and checked to exist) but not
#     executed (actor scripting comes later);
#   * the courtyard's room script, which uses the adventure API: compiled with
#     --interface adventure, as every adventure script is.
set(actor_demo_scripts_dir "${CMAKE_CURRENT_BINARY_DIR}/Scripts/$<CONFIG>")
set(actor_demo_walker_source "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Actors/Walker/walker.sch")
set(actor_demo_walker_bytecode "${actor_demo_scripts_dir}/walker.schb")
set(actor_demo_courtyard_source "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Rooms/Courtyard/Courtyard.sch")
set(actor_demo_courtyard_bytecode "${actor_demo_scripts_dir}/Courtyard.schb")
add_custom_command(
    OUTPUT "${actor_demo_walker_bytecode}"
    COMMAND "$<TARGET_FILE:schuimmc>" "${actor_demo_walker_source}" --name Actors/Walker/walker.sch
            -o "${actor_demo_walker_bytecode}" --verify
    DEPENDS "${actor_demo_walker_source}" schuimmc
    COMMENT "Compiling the ActorDemo walker script"
    VERBATIM)
add_custom_command(
    OUTPUT "${actor_demo_courtyard_bytecode}"
    COMMAND "$<TARGET_FILE:schuimmc>" "${actor_demo_courtyard_source}" --interface adventure
            --name Rooms/Courtyard/Courtyard.sch -o "${actor_demo_courtyard_bytecode}" --verify
    DEPENDS "${actor_demo_courtyard_source}" schuimmc
    COMMENT "Compiling the ActorDemo courtyard script"
    VERBATIM)
add_custom_target(ActorDemoScripts DEPENDS "${actor_demo_walker_bytecode}" "${actor_demo_courtyard_bytecode}")
add_dependencies(ActorDemo ActorDemoScripts)
set_property(TARGET ActorDemo APPEND PROPERTY LINK_DEPENDS "${actor_demo_walker_bytecode}" "${actor_demo_courtyard_bytecode}")

# Deploy the assets where the executable looks for them, replaced on every build.
add_custom_command(TARGET ActorDemo POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${actor_demo_asset_destination}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Assets" "${actor_demo_asset_destination}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${actor_demo_walker_bytecode}"
            "${actor_demo_asset_destination}/Actors/Walker/walker.schb"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${actor_demo_courtyard_bytecode}"
            "${actor_demo_asset_destination}/Rooms/Courtyard/Courtyard.schb"
    COMMAND "${CMAKE_COMMAND}" -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/LocalAssets
            -DDESTINATION=${actor_demo_asset_destination}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Tools/deploy_local_assets.cmake"
    COMMENT "Deploying ActorDemo assets and compiled scripts"
    VERBATIM)

if(WIN32)
    set_target_properties(ActorDemo PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:ActorDemo>")
endif()
