# Architecture overview

## Goals

A small, native, testable engine core built from one source tree on Windows, macOS and Linux.

**Milestones 1 and 1.5** provided:
* one window with an OpenGL 3.3 core context;
* keyboard and mouse input, accurate frame timing and logging;
* a main loop with a clean lifecycle;
* CI, a file system layer and asset loading.

**Milestone 2** adds a real 2D renderer:
* textures with a texture cache, a camera and sprite batching with strict ordering;
* render targets and text with glyph atlases;
* audio playback;
* an interactive demo scene that uses all of them.

Later milestones (scripting, adventure systems, editor) build on the interfaces described here,
never on native APIs.

## Project structure

| Directory | Library target | Contents |
|---|---|---|
| `Source/Core/Common` | `schuimm_core` | `Error`/`Result`/`Status`, assertions, geometry, `Transform2D`, string helpers |
| `Source/Core/Image` | `schuimm_core` | `Image`, `PixelFormat`: CPU-side pixel data, no graphics API |
| `Source/Core/Audio` | `schuimm_core` | `SoundData`: CPU-side PCM samples, no audio API |
| `Source/Core/Text` | `schuimm_core` | UTF-8 decoding and encoding |
| `Source/Core/Logging` | `schuimm_core` | `Logger`, `LogSink`, `ConsoleSink`, `FileSink` |
| `Source/Core/Timing` | `schuimm_core` | `Clock`, `FrameStatistics` |
| `Source/Core/Input` | `schuimm_core` | `Key`, `MouseButton`, `ButtonState`, `InputState` |
| `Source/Platform/Interface` | `schuimm_platform_interface` (headers) | `Platform`, `Window`, `WindowEvent`, `OpenGLSurface` |
| `Source/Platform/FileSystem` | `schuimm_filesystem` | UTF-8 path manipulation and file operations (POSIX or Win32) |
| `Source/Resource` | `schuimm_resource` | `AssetPath`, `AssetLocator`, `ResourceId`, `ResourceCache`; PNG, WAV and font loading ([Resources.md](Resources.md)) |
| `Source/Graphics/Interface`, `Source/Graphics/Common` | `schuimm_graphics` | `GraphicsDevice`, `Renderer2D`, `Texture`, `RenderTarget`, `Sprite`; `Camera2D`, `SpriteBatcher`, `TextLayout` ([Graphics.md](Graphics.md)) |
| `Source/GraphicsResources` | `schuimm_graphics_resources` | `TextureCache`, `Font`: assets → GPU resources through `Renderer2D` ([Text.md](Text.md)) |
| `Source/Audio` | `schuimm_audio` | `AudioDevice`, `Sound`, `Mixer`: playback without an audio API ([Audio.md](Audio.md)) |
| `Source/Audio/Miniaudio` | `schuimm_audio_miniaudio` | audio output through miniaudio |
| `Source/Core/Application` | `schuimm_application` | `Application`, `ApplicationListener`, lifecycle |
| `Source/Platform/<OS>` | `schuimm_platform` | exactly one native backend, chosen at configure time |
| `Source/Graphics/OpenGL` | `schuimm_graphics_opengl` | OpenGL 3.3 core `GraphicsDevice`, batched `Renderer2D`, render targets |
| `Applications/EngineDemo` | `EngineDemo` | composition root, demo scene and assets, self-test |
| `ThirdParty` | `schuimm_zlib`, `schuimm_png`, `schuimm_freetype`, `schuimm_miniaudio` | vendored C libraries, each used privately by one engine library ([README](../../ThirdParty/README.md)) |

Two folders were added to the structure requested for the milestone:

* `Source/Core/Input`: input *state tracking* is platform-independent and unit-tested, so it
  lives in Core. The native backends only translate OS events into `KeyEvent`/`Mouse*Event`.
* `Tests/TestFramework`: a ~200-line harness, so the tests need no external dependency.

`Source/Core/Application` is built as its own target (`schuimm_application`) because it depends
on the platform, graphics and audio *interfaces*, which in turn depend on `schuimm_core`.

## Dependency graph

```text
                           schuimm_core (Common, Image, Audio data, Text, Logging, Timing, Input)
            ▲                  ▲                   ▲                     ▲
 platform_interface     schuimm_graphics     schuimm_audio      schuimm_filesystem (POSIX | Win32)
   ▲       ▲              ▲      ▲    ▲          ▲     ▲                 ▲
   │       └─ schuimm_application ┘   │          │     │         schuimm_resource ──► libpng, zlib, FreeType
   │          (also ──► audio)        │          │     │                 ▲
   │                                  │          │     │                 │
   │         schuimm_graphics_opengl ─┤          │  audio_miniaudio      │
   │           (──► platform_interface)          │   (──► miniaudio)     │
   │                                  └── schuimm_graphics_resources ────┘
 schuimm_platform (Win32 | Cocoa | X11)
        ▲
        └──────── EngineDemo (links the concrete backends) ────────
```

* The resource layer depends neither on windowing, graphics nor audio.
* The graphics and audio layers do not depend on the resource layer or on each other. They meet
  the resource layer only in Core's data types (`Image`, `SoundData`), and in
  `schuimm_graphics_resources`, the one bridge that turns image and font assets into GPU
  resources.

Rules enforced by this layout:

* Win32, Cocoa, X11, GLX and WGL headers are only included in `Source/Platform/<OS>`.
  Objective-C appears only in `.mm` files and backend-internal headers.
* OpenGL is only called in `Source/Graphics/OpenGL`. The OpenGL backend reaches the window system
  exclusively through the abstract `OpenGLSurface`.
* POSIX and Win32 file APIs are only called in `Source/Platform/FileSystem/{Posix,Windows}`.
* Each third-party library is included by exactly one engine file: `png.h` by
  `Source/Resource/Image/PngDecoder.cpp`, FreeType by `Source/Resource/Font/FontFace.cpp`,
  `miniaudio.h` by `Source/Audio/Miniaudio/MiniaudioAudioDevice.cpp`.
* `Application` and all game code see interfaces only. The single place that names concrete
  backends is the composition root (`Applications/EngineDemo/main.cpp`).

## Design principles

* **Dependency injection, no singletons.** `Logger`, `Platform`, `Window`, `GraphicsDevice`
  and `AudioDevice` are created in `main` and passed by reference. The `Clock` takes its time source as a function,
  which makes it deterministic in tests. The only process-wide state lives where the OS forces it
  (AppKit's `NSApp` and the per-frame autorelease pool, Win32 window class registration, the Xlib
  error handlers) and is confined to the backend. There is no global asset root or resource
  manager: `AssetLocator` and `ResourceCache` are created by the application and passed on.
* **Interfaces only where a second implementation exists or is planned.** `Platform`, `Window`,
  `OpenGLSurface`, `GraphicsDevice`, `Renderer2D`, `Texture`, `RenderTarget`, `GlyphSource`,
  `AudioBackend` and `LogSink` each have native and test/alternative implementations.
  Everything else is a concrete class (e.g. `AudioDevice`, `Camera2D`, `TextureCache`).
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
  Shared ownership is deliberate and limited to immutable resources: `ResourceCache` entries,
  `TextureCache` textures and audio `Sound`s are `std::shared_ptr<const T>`, so unloading never
  invalidates a user (see [Resources.md](Resources.md#loading-ownership-and-lifetime-resourcecachet)).
* OpenGL object names are wrapped in move-only `gl::Handle<Traits>` objects (buffers, vertex arrays,
  textures, framebuffers, shaders, programs).
* Objective-C objects are held by ARC-managed strong references inside the backend classes.
  Delegates and views get a non-owning back pointer that is cleared (`detach`) before the C++
  object dies, so late AppKit callbacks cannot reach a destroyed object.
* Destruction order is explicit and checked:

  | Must be destroyed first | …before | Enforced by |
  |---|---|---|
  | `Texture`, `RenderTarget`, `Font`, `TextureCache` | `GraphicsDevice` (renderer) | live texture/target counters, `SCHUIMM_VERIFY` |
  | `AudioDevice` | `Platform` | declaration order in `main` (sounds may outlive the device) |
  | `GraphicsDevice` (owns the `OpenGLSurface`) | `Window` | `hasSurface_` flag, `SCHUIMM_VERIFY` |
  | `Window` | `Platform` | declaration order in `main` |

  The graphics device makes its context current before deleting GL objects, and the surface
  releases the context (`wglDeleteContext`, `glXDestroyContext`, `clearDrawable`) before the window
  is destroyed. `main` tears everything down in reverse creation order and logs "Clean shutdown".

## Threading

Engine code runs on the main thread. Window creation, event processing, resource loading and
every OpenGL call happen there, and the main thread also owns the GL context (made current once
at creation and again before teardown).

The exception is audio. The audio backend runs its own real-time thread, which only executes
the mixer. It communicates with the main thread through a lock-free command queue and atomics,
never through locks (see [Audio.md](Audio.md#threading-model)). `Logger` is thread-safe.

## Portability constraints worth knowing

* **macOS 10.13 deployment target:**
  * libc++ on 10.13 lacks `std::format`, `std::print` and `std::filesystem` (10.15), aligned
    `new` (10.14) and `atomic::wait` (11). The code uses `concat()`/`formatFixed()`
    (streams/`snprintf`) and its own file system layer (`Source/Platform/FileSystem`, POSIX and
    Win32 underneath; see
    [Platform.md](Platform.md#file-system-sourceplatformfilesystem-target-schuimm_filesystem)).
    `std::filesystem` is not used anywhere.
  * Clang rejects any newer libc++ symbol at compile time. Every engine target and every vendored
    library is compiled with `-Werror=unguarded-availability-new`, so a call to a system API
    newer than 10.13 (e.g. in CoreAudio) is a build error.
  * Note that 10.13 applies to the x86_64 slice: Apple Silicon (arm64) binaries always get a
    minimum of macOS 11, which is what the linker records for them.
  * The run time on a real 10.13 machine is untested (see
    [KnownLimitations.md](KnownLimitations.md)).
* **Paths** are UTF-8 strings with `/` separators throughout the engine. They are converted to
  UTF-16 and `\` only inside the Win32 file system implementation.
* **Windows 7 target:** `_WIN32_WINNT=0x0601`. Newer APIs (`SetProcessDpiAwarenessContext`,
  `GetDpiForWindow`, `SetProcessDpiAwareness`) are resolved at run time with fallbacks.
* **Units:** window sizes and mouse positions are *logical* units (points on macOS, 96-DPI units
  on Windows, pixels on X11); framebuffer sizes are always physical pixels.
