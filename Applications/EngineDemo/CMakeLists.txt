add_executable(EngineDemo
    DemoListener.cpp
    DemoOptions.cpp
    DemoScene.cpp
    Screenshot.cpp
    main.cpp)
target_link_libraries(EngineDemo PRIVATE
    Schuimm::Application
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
add_custom_command(TARGET EngineDemo POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${demo_asset_destination}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Assets" "${demo_asset_destination}"
    COMMENT "Copying EngineDemo assets"
    VERBATIM)

if(WIN32)
    # Console subsystem keeps log output visible when started from a terminal;
    # the window is created explicitly, so WinMain is not required.
    set_target_properties(EngineDemo PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:EngineDemo>")
endif()
