# Platform layer

## Abstraction

```cpp
class Platform {            // one per process, created by createNativePlatform(logger)
    createWindow(WindowDescription) -> Result<unique_ptr<Window>>
    pollEvents()            // non-blocking native event pump
    waitEvents(timeout)     // blocks until an event or timeout (used while suspended)
    createDiagnosticSink()  // OutputDebugString / unified logging / none
};

class Window {
    size(), framebufferSize(), setSize(), setTitle()
    setFullscreen(), isFullscreen(), isFocused(), isMinimized()
    drainEvents(vector<WindowEvent>&)          // events queued during pollEvents()
    setLiveResizeCallback(function<void()>)    // redraw inside modal resize loops
    createOpenGLSurface(OpenGLSurfaceDescription) -> Result<unique_ptr<OpenGLSurface>>
};

class OpenGLSurface {       // the only seam between windowing and the OpenGL renderer
    makeCurrent(), swapBuffers(), setSwapInterval(), supportsSwapControl()
    getProcAddress(name), framebufferSize()
};
```

`WindowEvent` is a `std::variant` of `CloseRequestedEvent`, `WindowResizedEvent`,
`FocusChangedEvent`, `MinimizedChangedEvent`, `FullscreenChangedEvent`, `KeyEvent`,
`MouseButtonEvent`, `MouseMovedEvent` and `MouseWheelEvent`.

Each backend is a set of `final` classes behind these interfaces plus the free function
`createNativePlatform()`. CMake compiles exactly one backend (`cmake/PlatformSelection.cmake`).

## Windows (Win32 + WGL), `Source/Platform/Windows`

**Window creation.** `Win32Platform` registers one window class with `CS_OWNDC` (OpenGL needs a
private, persistent device context) and makes the process DPI-aware (per-monitor v2 → per-monitor →
system, resolved dynamically so the binary still loads on Windows 7). `Win32Window` creates a
`WS_OVERLAPPEDWINDOW`, passes `this` through `CREATESTRUCT` and stores it in `GWLP_USERDATA`; the
static window procedure forwards messages to the instance. Sizes are logical (scaled by
`GetDpiForWindow`/`LOGPIXELSX`); `WM_DPICHANGED` applies the suggested rectangle.

**Messages.** `WM_CLOSE` becomes a `CloseRequestedEvent` (DefWindowProc is not allowed to destroy
the window). `WM_SIZE` reports resizes and minimisation. Keys use the hardware scan code from
`lParam` (layout-independent) with the extended-key bit; `WM_SYSKEYDOWN` is handled so Alt/F10
work as normal keys, `SC_KEYMENU` is swallowed so pressing Alt does not freeze the loop in the
menu modal loop, and Alt+F4 is still passed on. Mouse buttons use `SetCapture` while held; wheel
deltas are divided by `WHEEL_DELTA`.

**Modal loops.** While the user drags or resizes the window, `DefWindowProc` runs its own message
loop and `pollEvents()` does not return. `WM_ENTERSIZEMOVE` starts a timer; `WM_TIMER` and
`WM_SIZE` invoke the live-resize callback so the application keeps updating and drawing.

**Fullscreen.** Borderless: the style is switched to `WS_POPUP` and the window covers the
monitor's rectangle (`MonitorFromWindow`/`GetMonitorInfo`). The windowed placement is saved and
restored. No display mode change happens.

**OpenGL context.** Modern WGL context creation is a three-step process because `opengl32.dll`
only exposes OpenGL 1.1 and legacy `wglCreateContext`:

1. Create a hidden bootstrap window, set a classic `PIXELFORMATDESCRIPTOR` format, create a legacy
   context, make it current, and query `wglGetExtensionsStringARB`. From it load
   `wglChoosePixelFormatARB`, `wglCreateContextAttribsARB` and `wglSwapIntervalEXT`.
2. Destroy the bootstrap window and context. A window's pixel format can be set only once,
   which is why the bootstrap needs a window of its own.
3. On the real window, choose an accelerated RGBA8/D24S8 double-buffered format with
   `wglChoosePixelFormatARB`, apply it with `SetPixelFormat`, and create the context with
   `WGL_CONTEXT_MAJOR/MINOR_VERSION_ARB = 3.3` and `WGL_CONTEXT_CORE_PROFILE_BIT_ARB`.

`getProcAddress` uses `wglGetProcAddress`, falling back to `GetProcAddress(opengl32.dll)` for
OpenGL 1.1 entry points (and for drivers that return the sentinels 1, 2, 3 or −1). VSync uses
`WGL_EXT_swap_control`. The demo exports `NvOptimusEnablement`/`AmdPowerXpressRequestHighPerformance`
so hybrid laptops use the discrete GPU.

## macOS (Cocoa + NSOpenGLContext), `Source/Platform/macOS`

**Application lifecycle.** `CocoaPlatform` sets up `NSApplication` without a nib: regular
activation policy, a minimal menu bar (Hide, Quit ⌘Q, Minimize ⌘M, Enter Full Screen ⌃⌘F),
`finishLaunching`, and activation. The engine drives the event loop itself with
`nextEventMatchingMask:untilDate:distantPast` + `sendEvent:` instead of `[NSApp run]`.
`applicationShouldTerminate:` returns `NSTerminateCancel` and turns ⌘Q, the Quit menu item or
logout into close requests, so shutdown always runs through the engine and releases GL
resources deterministically. A local event monitor forwards key-up events while ⌘ is held,
which AppKit otherwise swallows.

**Window.** `CocoaWindow` owns an `NSWindow` (`releasedWhenClosed = NO`, ownership stays with ARC)
whose content view is `SchuimmContentView`, a flipped `NSView` (y grows downwards, as in the
engine) with `wantsBestResolutionOpenGLSurface` for Retina. The view receives keys (`keyDown:`,
`keyUp:`, `flagsChanged:` with device-dependent masks to tell left and right modifiers apart),
mouse buttons, movement (via a tracking area) and scroll events (`hasPreciseScrollingDeltas`
trackpad pixels are scaled to approximate wheel lines). `SchuimmWindowDelegate` turns
`windowShouldClose:` (returns `NO`), resizes, backing-scale changes, key/resign, miniaturise,
occlusion and fullscreen transitions into events. A window that is minimised or fully occluded
is reported as minimised: AppKit does not throttle `flushBuffer` for invisible windows, so
rendering them would spin at thousands of frames per second even with VSync on.

**Fullscreen** uses the native fullscreen space (`toggleFullScreen:` with
`NSWindowCollectionBehaviorFullScreenPrimary`). It animates asynchronously;
`FullscreenChangedEvent` arrives when the transition completes.

**OpenGL context.** `NSOpenGLPixelFormat` with `NSOpenGLPFAOpenGLProfile =
NSOpenGLProfileVersion3_2Core`, accelerated and double-buffered, RGBA8/D24S8. macOS offers no
way to request exactly 3.3: the 3.2 core profile returns the newest core version available
(4.1 on all supported Macs), so the renderer stays within the 3.3 feature set and checks the
version. The context is attached with `setView:`, receives `update` on every geometry or backing
change, uses `NSOpenGLContextParameterSwapInterval` for VSync, and is released with
`clearDrawable`. Entry points come from `dlsym` on `OpenGL.framework`. NSOpenGL is deprecated
since 10.14 but remains the native OpenGL path; `GL_SILENCE_DEPRECATION` is set.

Objective-C types never appear in engine-facing headers. The backend-internal headers
(`CocoaWindow.h`, …) are only included by `.mm` files, which are compiled with ARC.

## Linux (X11 + GLX + XKB), `Source/Platform/Linux/X11`

**Display and visual.** `createNativePlatform` opens the display, requires GLX ≥ 1.3 and picks a
`GLXFBConfig` (RGBA8, D24S8, double-buffered, true colour) *before* any window exists, because on
X11 the framebuffer configuration determines the window's visual. Windows are created with that
visual and a matching colormap.

**Keyboard (XKB).** `XkbSetDetectableAutoRepeat` stops the server from inventing KeyRelease
events for held keys. A 256-entry key-code table is built from the XKB key *names* (`AC01`,
`FK11`, …, physical positions, independent of layout) with a keysym fallback
(`XkbKeycodeToKeysym`). It is rebuilt on `MappingNotify`. Auto-repeat presses are flagged
`repeat`.

**Events.** `WM_DELETE_WINDOW` (via `WM_PROTOCOLS`) is the close request; `ConfigureNotify`
reports resizes; `FocusIn/FocusOut` (ignoring grab-induced ones) focus; `_NET_WM_STATE` and
`WM_STATE` property changes report fullscreen and minimisation; buttons 4–7 are wheel notches.
`waitEvents` uses `poll()` on the connection's file descriptor.

**Fullscreen** follows EWMH: a `_NET_WM_STATE` client message (add/remove
`_NET_WM_STATE_FULLSCREEN`) is sent to the root window, and the window manager's property
change confirms it. Without an EWMH window manager (e.g. bare Xvfb), fullscreen is reported as
unavailable.

**OpenGL context.** `glXCreateContextAttribsARB` (requires `GLX_ARB_create_context_profile`)
with 3.3 core attributes. Because failures are reported as asynchronous X protocol errors, which
by default terminate the process, creation runs inside a scoped error trap
(`XSetErrorHandler` + `XSync`). VSync uses `GLX_EXT_swap_control`, falling back to
`GLX_MESA_swap_control`.

**Header hygiene.** Xlib defines macros such as `Status`, `Bool` and `None`.
`X11Headers.h` is the only place they are included, always after engine headers, and it
`#undef`s `Status`, which would otherwise collide with `schuimm::Status`.

## Adding a Wayland backend

The engine-facing API needs no change. The steps:

1. Add `Source/Platform/Linux/Wayland/` with `WaylandPlatform`, `WaylandWindow` and an
   **EGL-based** `WaylandOpenGLSurface` (`wl_egl_window` + `eglCreateContext` with
   `EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT`, `eglGetProcAddress`, `eglSwapInterval`).
   `OpenGLSurface` already abstracts exactly what EGL provides, and the OpenGL renderer would
   work on it unchanged.
2. Implement windows with `xdg-shell` (`xdg_toplevel` close/configure/fullscreen events map
   directly onto `CloseRequestedEvent`, `WindowResizedEvent` and `FullscreenChangedEvent`),
   input with `wl_seat`/`wl_keyboard`/`wl_pointer`, and keyboard translation with
   **libxkbcommon**. Wayland delivers evdev key codes; their XKB key names are the same `AC01`
   style names that `translateXkbKeyName` already maps. Window decorations need
   `xdg-decoration` or client-side decorations (e.g. libdecor).
3. Generate protocol code with `wayland-scanner` in CMake, add `Wayland` to
   `SCHUIMM_LINUX_BACKEND`, and select the sources in `Source/Platform/CMakeLists.txt`.
4. Optionally build both backends and choose at run time in `createNativePlatform`
   (`WAYLAND_DISPLAY` set → Wayland, else X11). Only that function changes.

Today the X11 backend runs on Wayland desktops through Xwayland (verified on Debian 13 GNOME).
