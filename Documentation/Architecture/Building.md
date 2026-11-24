# Build instructions

Requirements per platform are listed in the [README](../../README.md#requirements). All builds are
out-of-source under `Build/<preset>/`; no paths are hard-coded.

## Presets

| Configure preset | Generator | Compiler | Build/test presets |
|---|---|---|---|
| `windows-msvc` | Visual Studio 17 2022, x64 | MSVC | `windows-msvc-debug`, `windows-msvc-release` |
| `macos-clang` | Ninja Multi-Config | Apple Clang | `macos-clang-debug`, `macos-clang-release` |
| `linux-gcc` | Ninja Multi-Config | GCC | `linux-gcc-debug`, `linux-gcc-release` |
| `linux-clang` | Ninja Multi-Config | Clang | `linux-clang-debug`, `linux-clang-release` |

```bash
cmake --preset <configure-preset>
cmake --build --preset <configure-preset>-debug
ctest --preset <configure-preset>-debug                 # all tests
ctest --preset <configure-preset>-debug -L unit         # unit tests only (no display, GPU or sound device)
ctest --preset <configure-preset>-debug -L integration  # needs a desktop session + OpenGL 3.3
ctest --preset <configure-preset>-debug -L audio        # needs an audio output device
```

The same commands run in CI on every push; see
[ContinuousIntegration.md](ContinuousIntegration.md).

## Tests

| Executable | Label | Covers |
|---|---|---|
| `CoreTests` | `unit` | application lifecycle (mock platform, fake renderer, script host service), clock, input state, logger, presentation math, `Image`, `Transform2D`, UTF-8, errors |
| `PlatformTests` | `unit` | native key-code tables of the configured backend |
| `FileSystemTests` | `unit` | path manipulation (both path styles) and file operations on the real file system, below the build tree |
| `ResourceTests` | `unit` | asset paths, asset root, resource cache, PNG and WAV decoding, font loading and rasterisation (assets in `Tests/Assets` and the demo font) |
| `GraphicsTests` | `unit` | camera, sprite batcher (vertices, slots, splitting, ordering, memory reuse), shelf packer, text layout, `Font` atlas, `TextureCache`, all on a fake renderer |
| `ScriptTests` | `unit` | script lexer, parser, type checker, IR, optimizer, bytecode writer/reader/validator (incl. corruption sweep), VM, natives, scheduler, end-to-end (examples and demo scripts, optimized vs. unoptimized) |
| `ScriptCompilerCli`, `ScriptRunnerCli`, `ScriptCompilerCliRejectsErrors` | `unit` | `schuimmc` compiles an example, `schuimm-run` runs the bytecode, invalid source fails |
| `AudioTests` | `unit` | `AudioDevice` and mixer through a manual backend (state, volume, looping, voice pool, ownership), and the miniaudio device on its null backend |
| `LifecycleTests` | `integration` | real platform/window/OpenGL context/graphics device created, used, destroyed and re-created |
| `RenderingTests` | `integration` | pixel checks of the presented frame: texture upload and premultiplied blending, filtering and wrapping, flips/rotation/view, multi-texture batches and order, render targets (incl. nested), dynamic texture updates, invalid sizes, resource counts |
| `EngineDemoSmoke` | `integration` | `EngineDemo --frames 30` (the full demo scene, assets deployed next to the executable) |
| `AudioDeviceSmoke` | `audio` | plays a short tone on the default output device |

Every test must have the label `unit`, `integration` or `audio`; `schuimm_add_test` stops
configuration otherwise. Test executables take an optional substring filter or `--list`
(`ResourceTests ImageDecoder`).

### Windows

Install Visual Studio 2022 (Community or Build Tools) with *Desktop development with C++*
(includes the Windows SDK and CMake). From PowerShell or cmd:

```bat
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release
Build\windows-msvc\bin\Release\EngineDemo.exe
```

If `cmake` is not on `PATH`, use the copy shipped with VS:
`%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`.
You can also open the folder in Visual Studio, which picks up `CMakePresets.json`.

### macOS

```bash
xcode-select --install            # if no Xcode/CLT yet
brew install cmake ninja          # or any other CMake ≥ 3.25 + Ninja
cmake --preset macos-clang
cmake --build --preset macos-clang-release
open Build/macos-clang/bin/Release/EngineDemo.app
```

The deployment target is fixed at 10.13 (`CMAKE_OSX_DEPLOYMENT_TARGET`). By default the build is
for the host architecture; for a universal binary add
`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`. 10.13 itself only runs on x86_64; arm64 binaries
always require macOS 11, and the linker records that minimum for them (`otool -l` shows
`minos 11.0` for arm64 and `LC_VERSION_MIN_MACOSX 10.13` for x86_64).

The EngineDemo's assets are copied into `EngineDemo.app/Contents/Resources/Assets` on every build
(next to the executable on Windows and Linux).

### Debian 13

```bash
sudo apt install build-essential cmake ninja-build libx11-dev libgl-dev libglx-dev libopengl-dev
cmake --preset linux-gcc
cmake --build --preset linux-gcc-release
./Build/linux-gcc/bin/Release/EngineDemo
```

Headless (CI, ssh) testing of the windowed parts uses Xvfb with Mesa's llvmpipe:

```bash
sudo apt install xvfb mesa-utils
xvfb-run -a ctest --preset linux-gcc-release -L integration
```

Audio needs no build packages: miniaudio loads PulseAudio (`libpulse`, also served by PipeWire)
or ALSA at run time. Without a sound server or card, the demo runs silently and the `audio` test
fails (for headless machines, a PulseAudio null sink works; see
[ContinuousIntegration.md](ContinuousIntegration.md)).

| Package | Provides |
|---|---|
| `libx11-dev` | Xlib, Xutil, Xatom and the XKB client API (`X11/XKBlib.h`) |
| `libgl-dev` | GLVND `libGL` and `GL/gl.h` |
| `libglx-dev` | GLVND `libGLX` and `GL/glx.h`, `GL/glxext.h` |
| `libopengl-dev` | GLVND `libOpenGL`; without it CMake's FindOpenGL reports GLX as found but creates no `OpenGL::GLX` target (configuration stops with a clear message) |
| `xvfb`, `mesa-utils` (optional) | virtual X server, `glxinfo` for diagnostics |

CMake finds these with `find_package(X11)` (requires `X11_Xkb_FOUND`) and
`find_package(OpenGL COMPONENTS GLX)`.

## Options

| Option | Default | Effect |
|---|---|---|
| `SCHUIMM_BUILD_TESTS` | `ON` | build tests and register them with CTest |
| `SCHUIMM_BUILD_DEMO` | `ON` | build EngineDemo (and the integration smoke test); also builds `schuimmc`, which compiles the demo scripts |
| `SCHUIMM_BUILD_TOOLS` | `ON` | build the script tools `schuimmc` (compiler) and `schuimm-run` (headless runner) |
| `SCHUIMM_WARNINGS_AS_ERRORS` | `ON` | `/WX` or `-Werror` |
| `SCHUIMM_LINUX_BACKEND` | `X11` | Linux windowing backend (only X11 exists so far) |

## Third-party code

libpng 1.6.59, zlib 1.3.2, FreeType 2.15.0 and miniaudio 0.11.26 are vendored in `ThirdParty/`
and built as static C libraries (the project therefore enables C as well as C++). They need no
installation and nothing is downloaded at build time. See
[ThirdParty/README.md](../../ThirdParty/README.md) for sources, checksums, licences and the
local changes.

Third-party code is compiled with its warnings silenced and without `-Werror`, except the macOS
availability check, which stays an error for all of it (`-Wno-everything
-Werror=unguarded-availability-new`; `-w` would silence it too).

Warnings: MSVC `/W4 /permissive-`; GCC/Clang `-Wall -Wextra -Wpedantic -Wshadow
-Wnon-virtual-dtor -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion`; on macOS
additionally `-Werror=unguarded-availability-new`.
