# Known limitations

Deliberate scope limits of this milestone and known platform caveats.

## Scope

* One window per application is assumed by the `Application` (backends can create more).
* No text input (characters, IME, dead keys): only physical key state. Keypad keys and some rare
  keys (Pause, PrintScreen, media keys) map to `Key::Unknown`.
* No gamepads, touch, cursor shape or cursor-capture (relative mouse) APIs.
* The renderer issues one draw call per primitive: no batching, no resource manager, no image
  decoding (demo textures are generated procedurally), no text rendering.
* Fixed framebuffer format: RGBA8 with 24-bit depth and 8-bit stencil, no MSAA, no sRGB.
* Single-threaded; the Logger is the only thread-safe component.
* No application packaging (installers, icons, Info.plist customisation beyond the bundle ID).

## Platform caveats

**All**
* Fullscreen is borderless/desktop-resolution; exclusive fullscreen with mode changes is not implemented.
* When VSync is "on", the actual behaviour depends on drivers and compositors, which may override
  the swap interval. The self-test reports this as a warning.

**Windows**
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

**Linux**
* X11 only. On Wayland desktops it runs through Xwayland. A native Wayland backend is planned
  (see [Platform.md](Platform.md#adding-a-wayland-backend)).
* Fullscreen needs an EWMH window manager. Without one (bare Xvfb) it is unavailable and logged.
* HiDPI on X11 is not scaled: logical size equals pixel size.
* Key-to-`Key` mapping relies on XKB key names; exotic keyboard geometries fall back to keysyms
  of the first group/level.
