add_executable(EngineDemo
    DemoAssets.cpp
    DemoListener.cpp
    DemoOptions.cpp
    main.cpp)
target_link_libraries(EngineDemo PRIVATE
    Schuimm::Application
    Schuimm::Platform
    Schuimm::GraphicsOpenGL)
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
endif()

if(WIN32)
    # Console subsystem keeps log output visible when started from a terminal;
    # the window is created explicitly, so WinMain is not required.
    set_target_properties(EngineDemo PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:EngineDemo>")
endif()
