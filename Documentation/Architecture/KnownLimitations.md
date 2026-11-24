# Known limitations

Deliberate scope limits (as of Milestone 1.5) and known platform caveats.

## Scope

* One window per application is assumed by the `Application` (backends can create more).
* No text input (characters, IME, dead keys): only physical key state. Keypad keys and some rare
  keys (Pause, PrintScreen, media keys) map to `Key::Unknown`.
* No gamepads, touch, cursor shape or cursor-capture (relative mouse) APIs.
* The renderer issues one draw call per primitive: no batching, no text rendering. There is no
  GPU resource management yet: textures are created explicitly from pixel data
  (`Renderer2D::createTexture`); a texture cache built on `ResourceCache<Image>` is Milestone 2
  work. The demo's textures are still generated procedurally (it ships no image assets).
* Fixed framebuffer format: RGBA8 with 24-bit depth and 8-bit stencil, no MSAA, no sRGB.
* Single-threaded; the Logger is the only thread-safe component. `ResourceCache` is not
  thread-safe and resource loading is synchronous (no asynchronous loading or streaming).

## Resources and assets

* PNG is the only image format. Images are always decoded to RGBA8; there is no greyscale or
  compressed-texture format, and no mipmaps.
* 16-bit PNGs without a `gAMA`/`sRGB` chunk are treated as linear and converted to sRGB (libpng's
  rule for the simplified API).
* Asset paths are compared case-sensitively, but macOS and Windows file systems are usually
  case-insensitive. A wrong-case path that works there fails on Linux; the engine does not
  detect the mismatch. Unicode normalisation (NFC/NFD) of file names is not handled either.
* An asset path cannot leave the asset root lexically (`..` is rejected), but symbolic links
  inside the root are followed.
* No packed asset archives, hot reloading or asset database; the asset path is the resource's
  identity.
* The file system layer passes relative paths to the OS unchanged, so they resolve against the
  current directory. Engine code uses absolute paths (`AssetLocator` always does).
* `ResourceCache` releases a resource only when its last holder lets go (shared ownership);
  there is no memory budget or eviction.
* No application packaging (installers, icons, Info.plist customisation beyond the bundle ID).

## Platform caveats

**All**
* Fullscreen is borderless/desktop-resolution; exclusive fullscreen with mode changes is not implemented.
* When VSync is "on", the actual behaviour depends on drivers and compositors, which may override
  the swap interval. The self-test reports this as a warning.
* `OpenGLSurface::makeCurrent()` does not report failure; a context that cannot be made current
  only shows up as GL errors.

**CI**
* Hosted Windows runners have no OpenGL 3.3 driver, so integration tests (real window and
  context) are built but not run there. They are run by hand on a Windows machine with a GPU.
  The job summary states this.
* Integration tests on Linux CI use Mesa's software renderer (llvmpipe) under Xvfb without a
  window manager, so fullscreen and swap control are not exercised there (the EngineDemo
  self-test covers them on real desktops).

**Windows**
* A window's pixel format can be set only once. Re-creating an OpenGL surface on the same window
  re-applies the same format, which works with the tested NVIDIA driver (`LifecycleTests`); a
  request for a *different* format on the same window would fail.
* Windows 7/8 support is untested (no machine available). DPI handling falls back to system DPI
  awareness before Windows 8.1 and treats all monitors alike.
* While the window is dragged, the modal loop is kept rendering with a `USER_TIMER_MINIMUM` timer,
  which runs at roughly 60–100 Hz.
* WGL extension entry points are loaded from a bootstrap context and reused for the real context.
  This is standard practice but assumes both use the same ICD (true for a single GPU per
  process).

**macOS**
* NSOpenGL is deprecated (since 10.14) and OpenGL tops out at 4.1. It still works on current
  macOS, including Apple Silicon through Apple's Metal-based GL implementation. A Metal backend is
  the long-term path (see [Graphics.md](Graphics.md)).
* macOS 10.13 runtime is untested; the code is compiled against 10.13 availability.
* A fully occluded window (e.g. a locked screen or a covered window) suspends the application.
  Integration tests that render frames, such as `EngineDemoSmoke`, therefore time out on a locked Mac.
  During the native fullscreen animation the window is briefly reported as occluded, so the
  application pauses for about half a second while AppKit animates its own snapshot.
* With `OBJC_DEBUG_MISSING_POOLS=YES`, a fixed number of objects (about 500, independent of run
  time) is still reported at start-up. They are autoreleased on AppKit's and OpenGL's own worker
  threads, not on the engine's main thread, and are outside the engine's control.

**Linux**
* X protocol errors are logged and execution continues. Losing the X server connection (an Xlib
  I/O error) still terminates the process, as Xlib requires.
* X11 only. On Wayland desktops it runs through Xwayland. A native Wayland backend is planned
  (see [Platform.md](Platform.md#adding-a-wayland-backend)).
* Fullscreen needs an EWMH window manager. Without one (bare Xvfb) it is unavailable and logged.
* HiDPI on X11 is not scaled: logical size equals pixel size.
* Key-to-`Key` mapping relies on XKB key names; exotic keyboard geometries fall back to keysyms
  of the first group/level.
