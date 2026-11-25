# SCHUIMM

A cross-platform 2D adventure game engine, written in C++23 directly on top of the operating
systems' own APIs: **Win32 + WGL** on Windows, **Cocoa + NSOpenGLContext** on macOS and
**X11 + GLX (with XKB)** on Linux. No SDL, GLFW, SFML, bgfx or other frameworks are used.

Milestone 1 delivered the application foundation: windowing, input, timing, logging, an OpenGL
3.3 core rendering foundation and the main loop. Milestone 1.5 added:
* lifecycle fixes;
* cross-platform CI on GitHub Actions;
* a file system layer;
* the start of a resource system: asset root and paths, typed resource identifiers, an explicit
  resource cache, and CPU-side PNG decoding.

Milestone 2 added graphics and audio:
* textures through the resource system (texture cache, per-texture filtering and wrapping);
* a 2D camera, a sprite batcher with multiple texture slots and strict draw order, and render
  targets;
* UTF-8 text with FreeType glyph atlases;
* audio playback (WAV, mixing, volume, looping) through miniaudio's device layer;
* an interactive demo scene that uses all of it.

Milestone 3 added scripting ([Documentation/Scripting](Documentation/Scripting/README.md)):
* SCHUIMM Script, a statically typed, Python-inspired language for game logic;
* a standalone compiler (`schuimmc`: lexer, parser, type checker, IR, optimizer) producing
  platform-independent, validated bytecode;
* a register VM with native engine functions and cooperative coroutines (`wait`, `yield`,
  instruction budgets), updated by the game loop;
* the EngineDemo runs two script coroutines alongside its scene.

The adventure systems and the editor come later.

## Repository layout

```text
CMakeLists.txt, CMakePresets.json   Build definition and presets per platform
cmake/                              Warning flags, platform backend selection
.github/                            CI workflow (Windows, macOS, Linux) and its test-runner script
Source/Core/                        Platform-independent: Common, Image, Audio data, Text (UTF-8), Logging, Timing, Input, Application
Source/Platform/Interface/          Platform, Window, OpenGLSurface, WindowEvent (engine-facing)
Source/Platform/{Windows,macOS,Linux/X11}/   Native backends (exactly one is compiled)
Source/Platform/FileSystem/         UTF-8 paths and file operations (POSIX or Win32)
Source/Resource/                    Asset paths and root, resource IDs and cache; PNG, WAV and font loading
Source/Graphics/Interface/          GraphicsDevice, Renderer2D, Texture, RenderTarget, Sprite, presentation math
Source/Graphics/Common/             Camera2D, SpriteBatcher, text layout (no graphics API)
Source/Graphics/OpenGL/             OpenGL 3.3 core backend: batched renderer, render targets, function loader
Source/GraphicsResources/           TextureCache and Font (glyph atlases): assets -> GPU resources
Source/Audio/                       AudioDevice, mixer, playback; Miniaudio/ output backend
Source/Script/                      Scripting: Bytecode/ (format, validation), Compiler/, Runtime/ (VM, scheduler)
Source/Room/                        Room resources (JSON) and room instances
ThirdParty/                         Vendored libpng, zlib, FreeType, miniaudio, nlohmann/json (sources, licences)
Applications/EngineDemo/            Demo scene, its assets, scripts and asset generator, self-test (composition root)
Applications/ScriptCompiler/        schuimmc, the command-line script compiler
Applications/ScriptRunner/          schuimm-run, runs compiled scripts headlessly
Tests/                              Self-contained test harness, unit and integration tests, test assets
Documentation/Architecture/         Architecture and process documentation
Documentation/Scripting/            Script language specification, compiler, bytecode, runtime; example scripts
Documentation/Rooms/Examples/       Example room resource and its room script
```

## Requirements

| Platform | Toolchain | System dependencies |
|---|---|---|
| Windows 7+ | Visual Studio 2022 (MSVC 19.3x+) with the Windows SDK, CMake ≥ 3.25 | none beyond the SDK (`user32`, `gdi32`, `opengl32`); a GPU driver providing OpenGL 3.3 |
| macOS 10.13+ | Xcode or Command Line Tools (Apple Clang with C++23), CMake ≥ 3.25, Ninja | Cocoa, OpenGL, CoreAudio and AudioToolbox frameworks (part of macOS) |
| Debian 13 | GCC 14 or Clang 19, CMake ≥ 3.25, Ninja | see below |

Debian packages:

```bash
sudo apt install build-essential cmake ninja-build libx11-dev libgl-dev libglx-dev libopengl-dev
```

`libx11-dev` provides Xlib *and* the XKB client API (`X11/XKBlib.h`); `libgl-dev`/`libglx-dev`/
`libopengl-dev` provide the GLVND `libGL`/`libGLX`/`libOpenGL` and GLX headers (CMake's
`OpenGL::GLX` target needs all three). Optional, for headless testing:
`sudo apt install xvfb mesa-utils`. Audio uses PulseAudio/PipeWire or ALSA if present, loaded at
run time (no build dependency). No other libraries need to be installed on any platform: the
third-party code (libpng, zlib, FreeType, miniaudio) is vendored in `ThirdParty/` and built with
the project (see [ThirdParty/README.md](ThirdParty/README.md)).

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

Test labels:
* `ctest -L unit` runs the headless unit tests (no display, GPU or sound device).
* `ctest -L integration` runs the windowed lifecycle, rendering and smoke tests, which need a
  display (on headless Linux use `xvfb-run -a ctest --preset linux-gcc-debug -L integration`).
* `ctest -L audio` needs an audio output device.

GitHub Actions builds Debug and Release on Windows (MSVC), macOS (Apple Clang) and Linux (GCC 14
and Clang 19):
* the unit tests run everywhere;
* the integration tests run on Linux (Xvfb + Mesa);
* the audio tests run on Linux (PulseAudio null sink).

The hosted Windows and macOS runners have no usable OpenGL driver and no guaranteed sound
device; their jobs say so explicitly instead of silently skipping. See
[ContinuousIntegration.md](Documentation/Architecture/ContinuousIntegration.md).

## Running the demo

```bash
./Build/linux-gcc/bin/Debug/EngineDemo                       # Linux
./Build/macos-clang/bin/Debug/EngineDemo.app/Contents/MacOS/EngineDemo   # macOS (or `open` the .app)
Build\windows-msvc\bin\Debug\EngineDemo.exe                  # Windows
```

The demo is a small world (1600×900) on a 640×360 virtual screen:
* a background image and y-sorted props with transparency;
* a walking player the camera follows;
* shop signs as world-space text, and a screen-space UI with a minimap rendered into a render
  target;
* a render-target "pixel dissolve" transition;
* sound effects and a looping ambience.

Controls:

| Key or input | Action |
|---|---|
| **WASD** or **arrows** | walk (hold **Shift** to run) |
| **click** | walk to that point |
| **Q** / **E** or **mouse wheel** | zoom (**0** resets) |
| **Space** | play a chime (presses overlap) |
| **N** | pause or resume the ambience |
| **−** / **=** | master volume |
| **Tab** | pixel transition through render targets |
| **F1** | show or hide the help |
| **F11** or **Alt+Enter** (macOS also **Ctrl+Cmd+F**) | fullscreen |
| **V** | toggle VSync |
| **R** | reset the window size |
| **Esc** | quit |

The UI shows the frame rate, batching statistics, zoom and audio state.

Options:
* `--self-test`: a scripted check of the scene, resize, fullscreen and VSync, with a pass/fail
  exit code;
* `--frames N`, `--screenshot FILE.ppm`, `--vsync on|off`, `--fullscreen`;
* `--assets DIR` (absolute path), `--no-audio`;
* `--log FILE`, `--verbose`.

See `EngineDemo --help`. Scripts: the demo runs `Applications/EngineDemo/Scripts/demo.sch`
(compiled at build time); to compile and run scripts yourself, see
[Documentation/Scripting/README.md](Documentation/Scripting/README.md#quick-start). The assets are regenerated with
`python3 Applications/EngineDemo/Tools/generate_demo_assets.py`; the font is Noto Sans (SIL OFL).

## Verification status

Verified by building and running on real machines (Milestone 2; see
[ManualTesting.md](Documentation/Architecture/ManualTesting.md) for exactly what was and was not checked):

| | macOS 26.6 (Apple M4 Max) | Debian 13 GNOME/Xwayland (Intel UHD 630) | Windows 11 (RTX 4050) |
|---|---|---|---|
| Build Debug + Release, no warnings | ✅ Apple Clang 21 | ✅ GCC 14 + Clang 19 | ✅ MSVC 19.41 |
| Unit tests (162 cases in 6 executables) | ✅ | ✅ | ✅ |
| Integration tests (lifecycle, rendering pixel checks, demo smoke) | ✅ | ✅ (under Xvfb) | ✅ |
| Audio on a real output device | ✅ CoreAudio | ✅ PulseAudio (PipeWire) | ✅ WASAPI |
| Demo self-test (scene, context, resize, fullscreen, VSync) | ✅ GL 4.1 core | ✅ GL 4.6 core | ✅ GL 3.3 core |
| Demo scene screenshot inspected | ✅ | ✅ | ✅ |
| Interactive play with keyboard/mouse | ☐ manual | ☐ manual | ☐ manual |

Windows 7 and macOS 10.13 are the *targeted* minimums: API usage and compiler settings, with
macOS availability errors on for all code, and the x86_64 macOS build declares 10.13. No such
system was available, so runtime support there is unverified.

## Documentation

* [Architecture overview](Documentation/Architecture/Overview.md): structure, dependencies, ownership, error handling
* [Platform layer](Documentation/Architecture/Platform.md): windows, events and OpenGL contexts per OS; file system; adding Wayland
* [Resources and assets](Documentation/Architecture/Resources.md): asset root and paths, resource IDs, ownership, image, sound and font loading
* [Rooms](Documentation/Architecture/Rooms.md): the room resource format (JSON), loading and validation, script association, room instances
* [Graphics](Documentation/Architecture/Graphics.md): coordinate systems, textures and the texture cache, camera, sprite batching, render targets, the OpenGL backend
* [Text](Documentation/Architecture/Text.md): UTF-8, fonts, glyph atlases, layout
* [Audio](Documentation/Architecture/Audio.md): architecture, formats, threading, ownership
* [Application lifecycle and main loop](Documentation/Architecture/Lifecycle.md)
* [Input handling](Documentation/Architecture/Input.md)
* [Build instructions](Documentation/Architecture/Building.md)
* [Continuous integration](Documentation/Architecture/ContinuousIntegration.md)
* [Manual integration testing](Documentation/Architecture/ManualTesting.md)
* [Known limitations](Documentation/Architecture/KnownLimitations.md)
