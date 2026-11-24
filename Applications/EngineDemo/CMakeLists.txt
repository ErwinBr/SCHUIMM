add_executable(EngineDemo
    DemoListener.cpp
    DemoOptions.cpp
    DemoScene.cpp
    Screenshot.cpp
    main.cpp)
target_link_libraries(EngineDemo PRIVATE
    Schuimm::Application
    Schuimm::ScriptRuntime
    Schuimm::AudioMiniaudio
    Schuimm::GraphicsOpenGL
    Schuimm::GraphicsResources
    Schuimm::Platform)
target_include_directories(EngineDemo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
schuimm_configure_target(EngineDemo)

if(APPLE)
    # A real application bundle gives the process a bundle identifier, Dock icon and normal
    # activation behaviour. Run it with `open EngineDemo.app` or EngineDemo.app/Contents/MacOS/EngineDemo.
    set_target_properties(EngineDemo PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.schuimm.EngineDemo"
        MACOSX_BUNDLE_BUNDLE_NAME "EngineDemo"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    set(demo_asset_destination "$<TARGET_BUNDLE_CONTENT_DIR:EngineDemo>/Resources/Assets")
else()
    set(demo_asset_destination "$<TARGET_FILE_DIR:EngineDemo>/Assets")
endif()

# Scripts: schuimmc compiles them into the build tree as a declared output, so a changed script
# *or* a rebuilt compiler regenerates the bytecode. The demo itself loads only validated
# bytecode and contains no compiler. Per configuration, because the command uses that
# configuration's schuimmc.
set(demo_script_source "${CMAKE_CURRENT_SOURCE_DIR}/Scripts/demo.sch")
set(demo_script_bytecode "${CMAKE_CURRENT_BINARY_DIR}/Scripts/$<CONFIG>/demo.schb")
add_custom_command(
    OUTPUT "${demo_script_bytecode}"
    COMMAND "$<TARGET_FILE:schuimmc>" "${demo_script_source}" --name Scripts/demo.sch
            -o "${demo_script_bytecode}" --verify
    DEPENDS "${demo_script_source}" schuimmc
    COMMENT "Compiling EngineDemo scripts"
    VERBATIM)
add_custom_target(EngineDemoScripts DEPENDS "${demo_script_bytecode}")
add_dependencies(EngineDemo EngineDemoScripts)
# Relink, and so redeploy below, when the bytecode changes (Ninja and Makefile generators;
# with Visual Studio rebuild EngineDemo after editing a script).
set_property(TARGET EngineDemo APPEND PROPERTY LINK_DEPENDS "${demo_script_bytecode}")

# Deploy the assets where the executable looks for them (defaultAssetRootCandidates): next to
# the executable, or in the bundle's Resources on macOS. Replaced on every build, so removed
# or renamed assets do not linger. The compiled scripts go to Assets/Scripts.
add_custom_command(TARGET EngineDemo POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${demo_asset_destination}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Assets" "${demo_asset_destination}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${demo_asset_destination}/Scripts"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${demo_script_bytecode}" "${demo_asset_destination}/Scripts/demo.schb"
    COMMENT "Deploying EngineDemo assets and compiled scripts"
    VERBATIM)

if(WIN32)
    # Console subsystem keeps log output visible when started from a terminal;
    # the window is created explicitly, so WinMain is not required.
    set_target_properties(EngineDemo PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:EngineDemo>")
endif()
