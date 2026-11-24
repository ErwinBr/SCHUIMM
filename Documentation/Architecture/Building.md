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
ctest --preset <configure-preset>-debug                 # unit + integration tests
ctest --preset <configure-preset>-debug -L unit         # unit tests only (no display needed)
ctest --preset <configure-preset>-debug -L integration  # needs a desktop session + OpenGL 3.3
```

The same commands run in CI on every push; see
[ContinuousIntegration.md](ContinuousIntegration.md).

## Tests

| Executable | Label | Covers |
|---|---|---|
| `CoreTests` | `unit` | application lifecycle (mock platform), clock, input state, logger, presentation math, `Image`, errors |
| `PlatformTests` | `unit` | native key-code tables of the configured backend |
| `FileSystemTests` | `unit` | path manipulation (both path styles) and file operations on the real file system, below the build tree |
| `ResourceTests` | `unit` | asset paths, asset root, resource cache, PNG decoding (assets in `Tests/Assets`) |
| `LifecycleTests` | `integration` | real platform/window/OpenGL context/graphics device created, used, destroyed and re-created |
| `EngineDemoSmoke` | `integration` | `EngineDemo --frames 30` |

Every test must have the label `unit` or `integration`; `schuimm_add_test` stops configuration
otherwise. Test executables take an optional substring filter or `--list`
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
`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` (10.13 itself only runs on x86_64).

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
xvfb-run -a ctest --preset linux-gcc-release
```

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
| `SCHUIMM_BUILD_DEMO` | `ON` | build EngineDemo (and the integration smoke test) |
| `SCHUIMM_WARNINGS_AS_ERRORS` | `ON` | `/WX` or `-Werror` |
| `SCHUIMM_LINUX_BACKEND` | `X11` | Linux windowing backend (only X11 exists so far) |

## Third-party code

libpng 1.6.59 and zlib 1.3.2 are vendored in `ThirdParty/` and built as static C libraries
(the project therefore enables C as well as C++). They need no installation and nothing is
downloaded at build time. See [ThirdParty/README.md](../../ThirdParty/README.md) for sources,
checksums and licences. Third-party code is compiled with its warnings silenced and without
`-Werror`.

Warnings: MSVC `/W4 /permissive-`; GCC/Clang `-Wall -Wextra -Wpedantic -Wshadow
-Wnon-virtual-dtor -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion`; on macOS
additionally `-Werror=unguarded-availability-new`.
