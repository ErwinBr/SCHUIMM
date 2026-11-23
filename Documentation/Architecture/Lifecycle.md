# Application lifecycle and main loop

## Composition (EngineDemo/main.cpp)

```text
Logger  →  Platform (createNativePlatform)  →  Window  →  OpenGLSurface
        →  GraphicsDevice (takes the surface)  →  ApplicationListener (game)  →  Application
run()
teardown in reverse order: Application, listener, GraphicsDevice(+surface), Window, Platform
```

Each creation step returns a `Result`; failures are logged with their reason and the program
exits with code 1.

## States

```text
Created ──run()──► Running ◄──────► Suspended
                      │   minimised /   │
                      │   occluded /    │
                      │   (unfocused*)  │
                      └──► Stopping ◄───┘   requestQuit() or accepted close request
                               │
                               ▼
                            Stopped        onShutdown() has run, run() returned
```

`*` only with `ApplicationSettings::pauseWhenUnfocused`.

* `run()` calls `onInitialize`; an error aborts with state `Stopped` and is returned.
* A `CloseRequestedEvent` (window close button, Alt+F4, ⌘Q, WM_DELETE_WINDOW) asks
  `onCloseRequested`, which may veto it.
* **Suspension.** While minimised (or occluded on macOS) the loop neither updates nor renders and
  blocks in `Platform::waitEvents(0.1 s)` instead of spinning. On resume `Clock::resetDelta()`
  makes the next delta 0, so the time spent suspended never appears as one huge step.
* **Focus loss** releases all keys and buttons (`InputState::releaseAll`), because their
  key-up events will be delivered to another application.

## Main loop

One iteration of `Application::run()`:

```cpp
platform.pollEvents();            // or waitEvents() while suspended
dispatch window events;           // close, resize, focus, minimise, fullscreen, input → InputState
if (running) {
    input.update();               // pressed/held/released for this frame
    const double dt = clock.tick();
    listener.onUpdate(app, dt);
    if (framebuffer not empty) {
        graphics.beginFrame();
        listener.onRender(app, graphics.renderer());
        graphics.endFrame();
        graphics.present();       // swap; blocks for vblank when VSync is on
    }
}
```

This follows the loop structure requested for the milestone. Platform event processing and
`input.update()` are split by an explicit dispatch step, and presenting is part of the graphics
device because the device owns the context.

## Timing

`Clock::tick()` measures with `std::chrono::steady_clock` (QPC / `mach_absolute_time` /
`CLOCK_MONOTONIC`), clamps the returned delta to `ClockSettings::maximumDeltaSeconds`
(0.25 s), and keeps rolling statistics (average/min/max frame time and FPS over 120 frames,
computed from the unclamped measurements). The clamp protects the simulation against stalls
the loop cannot see: debugger breaks, drags on platforms without a refresh callback, system sleep.

## Modal loops and live resize

On Windows (`WM_ENTERSIZEMOVE`…`WM_EXITSIZEMOVE`) and macOS (live resize), the OS runs a nested
event loop *inside* `pollEvents()` while the user drags the window. The backends call the window's
live-resize callback from inside that loop (Windows on `WM_SIZE` and a timer, macOS on
`windowDidResize:` during live resize). `Application` dispatches the queued events and runs one
update and render step. The callback is only honoured while `pollEvents()` is executing and never
re-entrantly, which the unit tests verify. X11 has no such loop.

## OpenGL and threads

Everything runs on the main thread. The context is made current when created and before the
device is destroyed; no other thread touches it.
