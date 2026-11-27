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

# The game's scripts, compiled with schuimmc at build time into one program: the game resource
# Assets/ActorDemo.game.json lists the rooms, and through them the build finds the room scripts
# and the actors' scripts (schuimmc --game, GameBuild.h).
set(actor_demo_scripts_dir "${CMAKE_CURRENT_BINARY_DIR}/Scripts/$<CONFIG>")
set(actor_demo_program "${actor_demo_scripts_dir}/ActorDemo.schb")
file(GLOB_RECURSE actor_demo_script_inputs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Assets/*.sch" "${CMAKE_CURRENT_SOURCE_DIR}/Assets/*.json")
add_custom_command(
    OUTPUT "${actor_demo_program}"
    COMMAND "$<TARGET_FILE:schuimmc>" --game ActorDemo.game.json --assets "${CMAKE_CURRENT_SOURCE_DIR}/Assets"
            -o "${actor_demo_program}" --verify
    DEPENDS ${actor_demo_script_inputs} schuimmc
    COMMENT "Compiling the ActorDemo game scripts"
    VERBATIM)
add_custom_target(ActorDemoScripts DEPENDS "${actor_demo_program}")
add_dependencies(ActorDemo ActorDemoScripts)
set_property(TARGET ActorDemo APPEND PROPERTY LINK_DEPENDS "${actor_demo_program}")

# Deploy the assets where the executable looks for them, replaced on every build.
add_custom_command(TARGET ActorDemo POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${actor_demo_asset_destination}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Assets" "${actor_demo_asset_destination}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${actor_demo_asset_destination}/Scripts"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${actor_demo_program}"
            "${actor_demo_asset_destination}/Scripts/ActorDemo.schb"
    COMMAND "${CMAKE_COMMAND}" -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/LocalAssets
            -DDESTINATION=${actor_demo_asset_destination}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Tools/deploy_local_assets.cmake"
    COMMENT "Deploying ActorDemo assets and compiled scripts"
    VERBATIM)

if(WIN32)
    set_target_properties(ActorDemo PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:ActorDemo>")
endif()
