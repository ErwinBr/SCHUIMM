# Architecture overview

## Goals of this milestone

A small, native, testable foundation: one window with an OpenGL 3.3 core context, keyboard and
mouse input, accurate frame timing, logging, and a main loop with a clean lifecycle, built
from one source tree on Windows, macOS and Linux. Later milestones (scripting, adventure systems,
editor) build on the interfaces described here, never on native APIs.

## Project structure

| Directory | Library target | Contents |
|---|---|---|
| `Source/Core/Common` | `schuimm_core` | `Error`/`Result`/`Status`, assertions, geometry, string helpers |
| `Source/Core/Image` | `schuimm_core` | `Image`, `PixelFormat`: CPU-side pixel data, no graphics API |
| `Source/Core/Logging` | `schuimm_core` | `Logger`, `LogSink`, `ConsoleSink`, `FileSink` |
| `Source/Core/Timing` | `schuimm_core` | `Clock`, `FrameStatistics` |
| `Source/Core/Input` | `schuimm_core` | `Key`, `MouseButton`, `ButtonState`, `InputState` |
| `Source/Platform/Interface` | `schuimm_platform_interface` (headers) | `Platform`, `Window`, `WindowEvent`, `OpenGLSurface` |
| `Source/Platform/FileSystem` | `schuimm_filesystem` | UTF-8 path manipulation and file operations (POSIX or Win32) |
| `Source/Resource` | `schuimm_resource` | `AssetPath`, `AssetLocator`, `ResourceId`, `ResourceCache`, PNG decoding ([Resources.md](Resources.md)) |
| `Source/Graphics/Interface` | `schuimm_graphics_interface` (headers) | `GraphicsDevice`, `Renderer2D`, `Texture`, presentation math |
| `Source/Core/Application` | `schuimm_application` | `Application`, `ApplicationListener`, lifecycle |
| `Source/Platform/<OS>` | `schuimm_platform` | exactly one native backend, chosen at configure time |
| `Source/Graphics/OpenGL` | `schuimm_graphics_opengl` | OpenGL 3.3 core `GraphicsDevice` + `Renderer2D` |
| `Applications/EngineDemo` | `EngineDemo` | composition root, demo scene, self-test |
| `ThirdParty` | `schuimm_png`, `schuimm_zlib` | vendored libpng and zlib, used only by the resource layer ([README](../../ThirdParty/README.md)) |

Two folders were added to the structure requested for the milestone:

* `Source/Core/Input`: input *state tracking* is platform-independent and unit-tested, so it
  lives in Core. The native backends only translate OS events into `KeyEvent`/`Mouse*Event`.
* `Tests/TestFramework`: a ~200-line harness, so the tests need no external dependency.

`Source/Core/Application` is built as its own target (`schuimm_application`) because it depends
on the platform and graphics *interfaces*, which in turn depend on `schuimm_core`.

## Dependency graph

```text
                 schuimm_core (Common, Image, Logging, Timing, Input)
                  ▲                      ▲                         ▲
   schuimm_platform_interface    schuimm_graphics_interface    schuimm_filesystem (POSIX | Win32)
        ▲          ▲                ▲            ▲                 ▲
        │     schuimm_application ──┘            │            schuimm_resource ──► libpng ──► zlib
        │                                        │
 schuimm_platform (Win32 | Cocoa | X11)   schuimm_graphics_opengl ──► platform_interface (OpenGLSurface)
        ▲                                        ▲
        └──────────────── EngineDemo ────────────┘
```

The resource layer depends neither on windowing nor on graphics, and the graphics layer does not
depend on the resource layer. They meet only in the `Image` type in Core.

Rules enforced by this layout:

* Win32, Cocoa, X11, GLX and WGL headers are only included in `Source/Platform/<OS>`.
  Objective-C appears only in `.mm` files and backend-internal headers.
* OpenGL is only called in `Source/Graphics/OpenGL`. The OpenGL backend reaches the window system
  exclusively through the abstract `OpenGLSurface`.
* POSIX and Win32 file APIs are only called in `Source/Platform/FileSystem/{Posix,Windows}`, and
  `png.h` is only included by `Source/Resource/Image/PngDecoder.cpp`.
* `Application` and all game code see interfaces only. The single place that names concrete
  backends is the composition root (`Applications/EngineDemo/main.cpp`).

## Design principles

* **Dependency injection, no singletons.** `Logger`, `Platform`, `Window` and `GraphicsDevice`
  are created in `main` and passed by reference. The `Clock` takes its time source as a function,
  which makes it deterministic in tests. The only process-wide state lives where the OS forces it
  (AppKit's `NSApp` and the per-frame autorelease pool, Win32 window class registration, the Xlib
  error handlers) and is confined to the backend. There is no global asset root or resource
  manager: `AssetLocator` and `ResourceCache` are created by the application and passed on.
* **Interfaces only where a second implementation exists or is planned.** `Platform`, `Window`,
  `OpenGLSurface`, `GraphicsDevice`, `Renderer2D`, `Texture` and `LogSink` each have native and
  mock/alternative implementations. Everything else is a concrete class.
* **Events are queued, not called back.** Native callbacks only append to a per-window
  `WindowEventQueue`; the Application consumes events after `pollEvents()` returns. Engine code
  therefore never runs inside an OS callback, except for the explicit live-resize refresh
  (see [Lifecycle.md](Lifecycle.md)).

## Error handling

Fallible operations return `Result<T>` (`std::expected<T, Error>`) or `Status`
(`std::expected<void, Error>`). `Error` carries an `ErrorCode` category and a human-readable
message (for example the shader compile log or the WGL failure reason). Engine code does not
throw. Exceptions only appear in the test harness (`REQUIRE`).

Programming errors (violated invariants) use `SCHUIMM_ASSERT` (Debug builds) or `SCHUIMM_VERIFY`
(always on), which print the location and abort.

## Resource ownership

* Every owner is a `std::unique_ptr` or an RAII value; there are no owning raw pointers.
  The one shared ownership is deliberate: CPU-side resources in a `ResourceCache` are
  `std::shared_ptr<const T>`, so unloading never invalidates a user (see
  [Resources.md](Resources.md#loading-ownership-and-lifetime-resourcecachet)).
* OpenGL object names are wrapped in move-only `gl::Handle<Traits>` objects (buffers, vertex arrays,
  textures, framebuffers, shaders, programs).
* Objective-C objects are held by ARC-managed strong references inside the backend classes.
  Delegates and views get a non-owning back pointer that is cleared (`detach`) before the C++
  object dies, so late AppKit callbacks cannot reach a destroyed object.
* Destruction order is explicit and checked:

  | Must be destroyed first | …before | Enforced by |
  |---|---|---|
  | `Texture` | `GraphicsDevice` (renderer) | live-texture counter, `SCHUIMM_VERIFY` |
  | `GraphicsDevice` (owns the `OpenGLSurface`) | `Window` | `hasSurface_` flag, `SCHUIMM_VERIFY` |
  | `Window` | `Platform` | declaration order in `main` |

  The graphics device makes its context current before deleting GL objects, and the surface
  releases the context (`wglDeleteContext`, `glXDestroyContext`, `clearDrawable`) before the window
  is destroyed. `main` tears everything down in reverse creation order and logs "Clean shutdown".

## Threading

The foundation is single-threaded. Window creation, event processing and every OpenGL call run on
the main thread, which also owns the GL context (the context is made current once at creation
and again before teardown). `Logger` is thread-safe so that future worker threads can log.

## Portability constraints worth knowing

* **macOS 10.13 deployment target:** libc++ on 10.13 lacks `std::format`, `std::print` and
  `std::filesystem` (10.15). The code uses `concat()`/`formatFixed()` (streams/`snprintf`) and
  its own file system layer (`Source/Platform/FileSystem`, POSIX and Win32 underneath; see
  [Platform.md](Platform.md#file-system-sourceplatformfilesystem-target-schuimm_filesystem)).
  `std::filesystem` is not used anywhere. Clang enforces this with
  `-Werror=unguarded-availability-new`.
* **Paths** are UTF-8 strings with `/` separators throughout the engine. They are converted to
  UTF-16 and `\` only inside the Win32 file system implementation.
* **Windows 7 target:** `_WIN32_WINNT=0x0601`. Newer APIs (`SetProcessDpiAwarenessContext`,
  `GetDpiForWindow`, `SetProcessDpiAwareness`) are resolved at run time with fallbacks.
* **Units:** window sizes and mouse positions are *logical* units (points on macOS, 96-DPI units
  on Windows, pixels on X11); framebuffer sizes are always physical pixels.
