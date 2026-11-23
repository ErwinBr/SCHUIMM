# ---------------------------------------------------------------------------------------------
# Platform interface: headers only, no native dependencies.
# ---------------------------------------------------------------------------------------------
add_library(schuimm_platform_interface INTERFACE)
add_library(Schuimm::PlatformInterface ALIAS schuimm_platform_interface)
target_link_libraries(schuimm_platform_interface INTERFACE Schuimm::Core)

# ---------------------------------------------------------------------------------------------
# Native backend: exactly one is compiled, chosen by cmake/PlatformSelection.cmake.
# ---------------------------------------------------------------------------------------------
add_library(schuimm_platform STATIC)
add_library(Schuimm::Platform ALIAS schuimm_platform)
target_link_libraries(schuimm_platform PUBLIC Schuimm::PlatformInterface)
schuimm_configure_target(schuimm_platform)

if(SCHUIMM_PLATFORM STREQUAL "Windows")
    target_sources(schuimm_platform PRIVATE
        Windows/Win32KeyMap.cpp
        Windows/Win32OpenGLSurface.cpp
        Windows/Win32Platform.cpp
        Windows/Win32Window.cpp)
    # Windows 7 is the minimum supported version: newer APIs are loaded dynamically.
    target_compile_definitions(schuimm_platform PRIVATE
        _WIN32_WINNT=0x0601 WINVER=0x0601 WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_link_libraries(schuimm_platform PRIVATE user32 gdi32 opengl32)

elseif(SCHUIMM_PLATFORM STREQUAL "macOS")
    target_sources(schuimm_platform PRIVATE
        macOS/CocoaKeyMap.cpp
        macOS/CocoaOpenGLSurface.mm
        macOS/CocoaPlatform.mm
        macOS/CocoaWindow.mm)
    # NSOpenGL is deprecated since 10.14 but remains the native OpenGL path on macOS.
    target_compile_definitions(schuimm_platform PRIVATE GL_SILENCE_DEPRECATION)
    target_compile_options(schuimm_platform PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
        -Werror=unguarded-availability-new)
    target_link_libraries(schuimm_platform PRIVATE "-framework Cocoa" "-framework OpenGL")

elseif(SCHUIMM_PLATFORM STREQUAL "Linux")
    find_package(X11 REQUIRED)
    find_package(OpenGL REQUIRED COMPONENTS GLX)
    if(NOT X11_Xkb_FOUND)
        message(FATAL_ERROR "XKB headers (X11/XKBlib.h) not found. Install libx11-dev.")
    endif()
    target_sources(schuimm_platform PRIVATE
        Linux/X11/X11KeyMap.cpp
        Linux/X11/X11OpenGLSurface.cpp
        Linux/X11/X11Platform.cpp
        Linux/X11/X11Window.cpp)
    target_link_libraries(schuimm_platform PRIVATE X11::X11 OpenGL::GLX)
endif()
