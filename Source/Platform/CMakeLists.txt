# ---------------------------------------------------------------------------------------------
# Platform interface: headers only, no native dependencies.
# ---------------------------------------------------------------------------------------------
add_library(schuimm_platform_interface INTERFACE)
add_library(Schuimm::PlatformInterface ALIAS schuimm_platform_interface)
target_link_libraries(schuimm_platform_interface INTERFACE Schuimm::Core)

# ---------------------------------------------------------------------------------------------
# File system: path manipulation (pure, Path.cpp) and operations (POSIX or Win32). Independent of
# the windowing backend, so tools and headless tests can use it without a display.
# ---------------------------------------------------------------------------------------------
add_library(schuimm_filesystem STATIC
    FileSystem/FileSystemCommon.cpp
    FileSystem/Path.cpp)
add_library(Schuimm::FileSystem ALIAS schuimm_filesystem)
target_link_libraries(schuimm_filesystem PUBLIC Schuimm::Core)
schuimm_configure_target(schuimm_filesystem)
if(SCHUIMM_PLATFORM STREQUAL "Windows")
    target_sources(schuimm_filesystem PRIVATE FileSystem/Windows/Win32FileSystem.cpp)
    target_compile_definitions(schuimm_filesystem PRIVATE
        _WIN32_WINNT=0x0601 WINVER=0x0601 WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
else()
    target_sources(schuimm_filesystem PRIVATE FileSystem/Posix/PosixFileSystem.cpp)
    if(APPLE)
        target_compile_options(schuimm_filesystem PRIVATE -Werror=unguarded-availability-new)
    endif()
endif()

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
        macOS/CocoaFramePool.mm
        macOS/CocoaKeyMap.cpp
        macOS/CocoaOpenGLSurface.mm
        macOS/CocoaPlatform.mm
        macOS/CocoaWindow.mm)
    # NSOpenGL is deprecated since 10.14 but remains the native OpenGL path on macOS.
    target_compile_definitions(schuimm_platform PRIVATE GL_SILENCE_DEPRECATION)
    target_compile_options(schuimm_platform PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
        -Werror=unguarded-availability-new)
    # The per-frame autorelease pool needs manual reference counting (NSAutoreleasePool).
    set_source_files_properties(macOS/CocoaFramePool.mm PROPERTIES COMPILE_OPTIONS "-fno-objc-arc")
    target_link_libraries(schuimm_platform PRIVATE "-framework Cocoa" "-framework OpenGL")

elseif(SCHUIMM_PLATFORM STREQUAL "Linux")
    find_package(X11 REQUIRED)
    find_package(OpenGL REQUIRED COMPONENTS GLX)
    # FindOpenGL only creates OpenGL::GLX when the GLVND libOpenGL is present as well; without it
    # the GLX component is still reported as found, and the error would only surface later.
    if(NOT TARGET OpenGL::GLX)
        message(FATAL_ERROR "OpenGL::GLX is unavailable: the GLVND libraries are incomplete. "
                            "Install libgl-dev, libglx-dev and libopengl-dev.")
    endif()
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
