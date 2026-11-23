# SCHUIMM

Native foundation of a cross-platform 2D adventure game engine, written in C++23 directly on
top of the operating systems' own APIs: **Win32 + WGL** on Windows, **Cocoa + NSOpenGLContext**
on macOS and **X11 + GLX (with XKB)** on Linux. No SDL, GLFW, SFML or other frameworks are used.

This milestone delivers the application foundation only: windowing, input, timing, logging, an
OpenGL 3.3 core rendering foundation and the main loop. Scripting, the adventure systems and the
editor are later milestones.

## Repository layout

```text
CMakeLists.txt, CMakePresets.json   Build definition and presets per platform
cmake/                              Warning flags, platform backend selection
Source/Core/                        Platform-independent: Common, Logging, Timing, Input, Application
Source/Platform/Interface/          Platform, Window, OpenGLSurface, WindowEvent (engine-facing)
Source/Platform/{Windows,macOS,Linux/X11}/   Native backends (exactly one is compiled)
Source/Graphics/Interface/          GraphicsDevice, Renderer2D, Texture, presentation math
Source/Graphics/OpenGL/             OpenGL 3.3 core backend with an in-house function loader
Applications/EngineDemo/            Demo and manual integration test (composition root)
Tests/                              Self-contained test harness, unit tests, smoke test
Documentation/Architecture/         Architecture and process documentation
```

## Requirements

| Platform | Toolchain | System dependencies |
|---|---|---|
| Windows 7+ | Visual Studio 2022 (MSVC 19.3x+) with the Windows SDK, CMake ≥ 3.25 | none beyond the SDK (`user32`, `gdi32`, `opengl32`); a GPU driver providing OpenGL 3.3 |
| macOS 10.13+ | Xcode or Command Line Tools (Apple Clang with C++23), CMake ≥ 3.25, Ninja | Cocoa and OpenGL frameworks (part of macOS) |
| Debian 13 | GCC 14 or Clang 19, CMake ≥ 3.25, Ninja | see below |

Debian packages:

```bash
sudo apt install build-essential cmake ninja-build libx11-dev libgl-dev libglx-dev
```

`libx11-dev` provides Xlib *and* the XKB client API (`X11/XKBlib.h`); `libgl-dev`/`libglx-dev`
provide the GLVND `libGL`/`libGLX` and GLX headers. Optional, for headless testing:
`sudo apt install xvfb mesa-utils`. No third-party libraries are required on any platform.

## Building and testing

Each platform has a configure preset plus Debug/Release build and test presets.

```bash
cmake --preset macos-clang          # or linux-gcc, linux-clang, windows-msvc
cmake --build --preset macos-clang-debug
ctest --preset macos-clang-debug
```

On Windows, run the same commands with `windows-msvc` from any shell; the Visual Studio generator
finds MSVC without a Developer Command Prompt. The CMake bundled with VS 2022 Build Tools works.

Binaries end up in `Build/<preset>/bin/<Config>/`. On macOS the demo is an application bundle,
`EngineDemo.app`.

`ctest -L unit` runs only the unit tests; `ctest -L integration` runs the windowed smoke test,
which needs a display (on headless Linux use `xvfb-run -a ctest --preset linux-gcc-debug`).

## Running the demo

```bash
./Build/linux-gcc/bin/Debug/EngineDemo                       # Linux
./Build/macos-clang/bin/Debug/EngineDemo.app/Contents/MacOS/EngineDemo   # macOS (or `open` the .app)
Build\windows-msvc\bin\Debug\EngineDemo.exe                  # Windows
```

Controls: **Esc** quit · **F11** or **Alt+Enter** fullscreen (macOS also **Ctrl+Cmd+F**) ·
**V** toggle VSync · **R** reset window size · **arrow keys** move the triangle ·
**mouse** moves the sprite · **left/right/middle button** tint it · **wheel** scales it.
The title bar shows the frame rate, VSync state and framebuffer size; key and mouse transitions
are logged.

Options: `--self-test` (scripted resize, fullscreen and VSync check with pass/fail exit code),
`--frames N`, `--screenshot FILE.ppm`, `--vsync on|off`, `--fullscreen`, `--log FILE`,
`--verbose`. See `EngineDemo --help`.

## Verification status

Verified by building and running on real machines (see
[ManualTesting.md](Documentation/Architecture/ManualTesting.md) for exactly what was and was not checked):

| | macOS 26.6 (Apple M4 Max) | Debian 13 GNOME/Xwayland (Intel UHD 630) | Windows 11 (RTX 4050) |
|---|---|---|---|
| Build Debug + Release, no warnings | ✅ | ✅ GCC 14 + Clang 19 | ✅ MSVC 19.41 |
| Unit tests (51) + smoke test | ✅ | ✅ | ✅ |
| Self-test: context, resize, fullscreen, VSync | ✅ GL 4.1 core | ✅ GL 4.6 core | ✅ GL 3.3 core |
| Rendering screenshot inspected | ✅ | ✅ | ✅ |
| Physical keyboard/mouse input | ◐ partly (manual run) | ☐ manual | ☐ manual |

Windows 7 and macOS 10.13 are the *targeted* minimums (API usage and compiler settings), but no
such system was available, so runtime support there is unverified.

## Documentation

* [Architecture overview](Documentation/Architecture/Overview.md): structure, dependencies, ownership, error handling
* [Platform layer](Documentation/Architecture/Platform.md): windows, events and OpenGL contexts per OS; adding Wayland
* [Graphics](Documentation/Architecture/Graphics.md): the OpenGL foundation; replacing the renderer
* [Application lifecycle and main loop](Documentation/Architecture/Lifecycle.md)
* [Input handling](Documentation/Architecture/Input.md)
* [Build instructions](Documentation/Architecture/Building.md)
* [Manual integration testing](Documentation/Architecture/ManualTesting.md)
* [Known limitations](Documentation/Architecture/KnownLimitations.md)
