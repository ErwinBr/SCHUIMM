# ActorDemo: two rooms with actors (Applications/ActorDemo/Assets), to see actor resources,
# directional animation, mirroring, pivots, scale, visibility and room lifetime at work.
add_executable(ActorDemo
    ActorDemo.cpp
    main.cpp)
target_link_libraries(ActorDemo PRIVATE
    Schuimm::Application
    Schuimm::GraphicsOpenGL
    Schuimm::Platform
    Schuimm::RoomGraphics)
target_include_directories(ActorDemo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
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

# The walker's actor script: compiled with schuimmc like the EngineDemo scripts. It is referenced
# by walker.actor.json (and checked to exist) but not executed: actor scripting comes later.
set(actor_demo_script_source "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Actors/Walker/walker.sch")
set(actor_demo_script_bytecode "${CMAKE_CURRENT_BINARY_DIR}/Scripts/$<CONFIG>/walker.schb")
add_custom_command(
    OUTPUT "${actor_demo_script_bytecode}"
    COMMAND "$<TARGET_FILE:schuimmc>" "${actor_demo_script_source}" --name Actors/Walker/walker.sch
            -o "${actor_demo_script_bytecode}" --verify
    DEPENDS "${actor_demo_script_source}" schuimmc
    COMMENT "Compiling ActorDemo scripts"
    VERBATIM)
add_custom_target(ActorDemoScripts DEPENDS "${actor_demo_script_bytecode}")
add_dependencies(ActorDemo ActorDemoScripts)
set_property(TARGET ActorDemo APPEND PROPERTY LINK_DEPENDS "${actor_demo_script_bytecode}")

# Deploy the assets where the executable looks for them, replaced on every build.
add_custom_command(TARGET ActorDemo POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${actor_demo_asset_destination}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Assets" "${actor_demo_asset_destination}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${actor_demo_script_bytecode}"
            "${actor_demo_asset_destination}/Actors/Walker/walker.schb"
    COMMENT "Deploying ActorDemo assets and compiled scripts"
    VERBATIM)

if(WIN32)
    set_target_properties(ActorDemo PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:ActorDemo>")
endif()
