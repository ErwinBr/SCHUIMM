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

# Deploy the assets where the executable looks for them (defaultAssetRootCandidates): next to
# the executable, or in the bundle's Resources on macOS. Replaced on every build, so removed
# or renamed assets do not linger.
#
# Scripts are compiled here with schuimmc into Assets/Scripts/*.schb: the demo itself loads only
# validated bytecode and contains no compiler. LINK_DEPENDS re-runs this step when a script
# changes (Ninja and Makefile generators; with Visual Studio rebuild EngineDemo after editing).
set(demo_scripts "${CMAKE_CURRENT_SOURCE_DIR}/Scripts/demo.sch")
add_dependencies(EngineDemo schuimmc)
set_property(TARGET EngineDemo APPEND PROPERTY LINK_DEPENDS ${demo_scripts})
add_custom_command(TARGET EngineDemo POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${demo_asset_destination}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Assets" "${demo_asset_destination}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${demo_asset_destination}/Scripts"
    COMMAND "$<TARGET_FILE:schuimmc>" "${CMAKE_CURRENT_SOURCE_DIR}/Scripts/demo.sch" --name Scripts/demo.sch
            -o "${demo_asset_destination}/Scripts/demo.schb" --verify
    COMMENT "Copying EngineDemo assets and compiling its scripts"
    VERBATIM)

if(WIN32)
    # Console subsystem keeps log output visible when started from a terminal;
    # the window is created explicitly, so WinMain is not required.
    set_target_properties(EngineDemo PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:EngineDemo>")
endif()
