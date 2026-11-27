# Known limitations

Deliberate scope limits (as of Milestone 2) and known platform caveats.

## Scope

* One window per application is assumed by the `Application` (backends can create more).
* No text input (characters, IME, dead keys): only physical key state. Keypad keys and some rare
  keys (Pause, PrintScreen, media keys) map to `Key::Unknown`.
* No gamepads, touch, cursor shape or cursor-capture (relative mouse) APIs.
* Fixed framebuffer format: RGBA8 with 24-bit depth and 8-bit stencil, no MSAA, no sRGB.
* Single-threaded except for the audio mixer thread; the Logger is the only thread-safe
  component. `ResourceCache`, `TextureCache`, `Font` and `AudioDevice` must be used from the main
  thread, and resource loading is synchronous (no asynchronous loading or streaming).

## Rendering

* One blend mode (premultiplied "over"). No additive, multiply or masking modes, no custom
  shaders or post-processing effects beyond drawing render targets as textures.
* Blending and premultiplication happen on sRGB-encoded values, not in linear light.
* Textures: RGBA8 only, no mipmaps, no compressed formats. Only dynamic textures can be updated
  after creation.
* Render targets have no depth or stencil buffer and no MSAA.
* Sprite layer ordering applies between two flushes. Changing the view, switching render targets
  or clearing flushes, so draws before and after such a call are never interleaved by layer.
* Up to 16 textures per batch (fewer if the driver reports fewer units). The fragment shader
  selects the texture with a branch chain; very old drivers may run that slower than a
  single-texture shader. The batch size is fixed at 4096 quads per draw call.
* A texture or render target must stay alive until the draws that use it are flushed (at the
  latest the end of the frame). This is documented, not checked.
* The camera has no rotation and no built-in shake or path behaviours; following is exponential
  smoothing with an optional dead zone.

## Text

* No shaping, ligatures, bidirectional text, vertical text or automatic word wrapping. One code
  point is one glyph; combining characters are not composed.
* Kerning: legacy `kern` tables and simple GPOS pair adjustment only (FreeType's basic support,
  locally patched to tolerate contextual lookups, see `ThirdParty/README.md`). Class-based
  context-dependent kerning is not applied.
* TrueType-outline fonts only (no CFF/PostScript outlines, colour or bitmap fonts, variable font
  axes are not exposed).
* A `Font` has one pixel size. Text scaled by camera zoom or by the window scale is resampled,
  not re-rasterised. The atlas has a fixed number of pages (4 × 512² by default); glyphs that do
  not fit are drawn as nothing. Glyphs are never evicted.

## Audio

* WAV (PCM 8/16/24/32-bit integer, 32-bit float; mono/stereo) is the only format. No Ogg Vorbis,
  FLAC or MP3, and no streaming: sounds are fully decoded into memory.
* Resampling to the device rate is linear interpolation (adequate for effects, not mastering
  quality). There is no per-voice pitch, panning, effects or 3D positioning.
* 32 simultaneous voices; `play()` returns an invalid instance when all are busy (no voice
  stealing or priorities).
* The output device is chosen once (the system default). Device changes (headphones plugged in)
  are handled by the OS or miniaudio's default-device routing, not by the engine; a device that
  disappears is not re-opened.
* Playback control is state, applied once per audio period (about 10 ms): a pause and resume
  within the same period cancel out, and only the last volume set in a period is ramped to.
  Nothing is lost, but changes faster than one period are not individually audible.
* WAV files must be strictly consistent (see [Audio.md](Audio.md#formats)): a trailing partial
  frame, an unknown extensible sub-format or inconsistent valid bits make the whole file fail
  instead of being repaired.

## Resources and assets

* The EngineDemo needs `--assets` to be an absolute path (the engine never resolves paths against
  the working directory).
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

## Rooms

See [Rooms.md](Rooms.md).
* There is no interaction detection or room transition yet.
* Regions are not checked against the room bounds (walkable areas and scale-map points are).
* Loading reports the first error only, not every error in the file.
* Room object state (`RoomInstance` objects) is reachable from C++ only; there are no natives
  over it yet, and no lookup from an arbitrary `ObjectRef` to its live room.
* A `RoomInstance` must be destroyed between frames, not while the `ScriptHost` updates (checked
  in every build; violating it stops the program). Coroutines it did not start are not cancelled when it is destroyed; their handles
  just become stale.
* Room ids are not checked for uniqueness across a game (a single file cannot know the others).

## Actors and animation

See [Actors.md](Actors.md) and [Animation.md](Animation.md).
* Actor scripts are referenced and checked to exist, but not loaded or executed; there are no
  script-facing actor functions.
* Changing to a different directional clip restarts it (the phase is not carried over), also
  when a walk turns at a waypoint.
* Depth is the ground y; there are no occlusion masks, foreground layers or per-pixel depth.
* Walking is not exposed to scripts yet (Milestone 4.4), and there is no script-level waiting for
  a walk. The movement state keeps only the latest request, so 4.4 must deliver each request's
  outcome to its waiter ([Actors.md](Actors.md#walking)).

## Navigation and movement

See [Navigation.md](Navigation.md) and [Actors.md](Actors.md#movement).
* Actors are points: no clearance, no collision or avoidance between actors (they may pass through
  one another), no acceleration.
* Navigation is static: no dynamic obstacles, navigation links or one-way areas.
* Paths are geometrically shortest; travel time (perspective speed) does not affect the route.
* At most 32 walkable areas, 256 walkable-area points and 512 navigation corners per room
  (visibility-graph cost).
* Authored polygon points must be more than 1/64 px apart (the geometry tolerance).
* No animation events, blending, state machines, multiple tracks or skeletal animation.
* Actor state is not saved or restored yet.
* Image sizes for animation frames come from the PNG header, but `AssetLocator` reads whole
  files, so the file is still read (not decompressed) to find its size.

## Scripting

Language scope (version 3.0, see [Language.md](../Scripting/Language.md#12-limitations-version-30)):
* no `break`/`continue`, `//=`, `%=`, lists, dictionaries, structures (besides `vec2`),
  constants or exception handling (the files of a game form one program, without imports);
* no joining of coroutines (`wait(coroutine)`), spawned blocks, closures or coroutine groups;
* `str()` does not convert vectors, enums or references (only `print` shows them);
* object handlers reach object state only through natives (no `self.property` syntax yet);
  nothing dispatches player interactions to handlers yet (the interaction milestone);
* ActorDemo's camera follows the hero every frame, so a script's `set_camera_position` there lasts
  only until the next frame (zoom is kept);
* the adventure API acts on one active room; the room lifecycle (`init`, `ready`, `exit`) is not
  dispatched automatically and there are no room transitions yet: the game calls
  `AdventureRuntime::enterRoom` and `dispatchRoom` itself;
* a script cannot call another entity's handlers, and actors do not persist across rooms (their
  instance variables reset with the room);
* a game is compiled in one run (no incremental build cache); natives are linked by exact
  signature, and engine enums by qualified name and members.
* Format 3.0 does not read 1.x or 2.x bytecode: games are recompiled from source.

Runtime:
* Coroutine state cannot be saved or restored (no save games of running scripts yet).
* Natives with a result cannot suspend: there is no "wait, then return a value" mechanism yet
  (needed later for e.g. dialogue choices).
* No hot reloading: a changed script needs a rebuild. The demo's bytecode is a build output that
  depends on the script and on `schuimmc`; with the Visual Studio generator (which ignores
  `LINK_DEPENDS`) the regenerated bytecode is only redeployed when EngineDemo relinks, so
  rebuild EngineDemo after editing a script.
* Waits use the engine's clamped frame delta, so after a stall longer than 0.25 s waits run
  late by the clamped amount (as all game time does).
* Register types are verified at load time, but whether a register is written before it is read
  is not; the VM therefore still checks operand tags per instruction. This costs a little speed
  but keeps execution memory-safe for any bytecode.
* String values use a non-atomic reference count: scripts and their values belong to the game
  thread.

macOS 10.13: the scripting code avoids APIs newer than 10.13 and builds with
`-Werror=unguarded-availability-new`. Because Apple Silicon builds are checked against macOS 11
only, it was additionally built for x86_64 with deployment target 10.13 (binaries carry
`LC_VERSION_MIN_MACOSX 10.13`), and the unit tests passed under Rosetta on macOS 26. It has not
been run on a real 10.13 machine.

## Platform caveats

**All**
* Fullscreen is borderless/desktop-resolution; exclusive fullscreen with mode changes is not implemented.
* When VSync is "on", the actual behaviour depends on drivers and compositors, which may override
  the swap interval. The self-test reports this as a warning.
* `OpenGLSurface::makeCurrent()` does not report failure; a context that cannot be made current
  only shows up as GL errors.

**CI**
* Hosted Windows and macOS runners have no usable OpenGL driver (Windows: GDI OpenGL 1.1 only;
  macOS: a VM with no hardware-accelerated pixel format), so integration tests (real window and
  context) are built but not run there. They are run by hand on machines with a GPU. The job
  summaries state this.
* Real audio output is tested in CI only on Linux, against a PulseAudio null sink. On Windows
  and macOS the `audio` test runs as an informational probe whose result (an output device or
  none) is reported but does not decide the job. The mixer and the miniaudio code path are
  covered everywhere by unit tests on the null backend.
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
* macOS 10.13 runtime is untested (no machine available). The x86_64 build declares 10.13 as
  its minimum and every engine and third-party source is compiled with availability errors
  against 10.13; the x86_64 build was run under Rosetta on a current macOS. arm64 binaries
  require macOS 11 by definition.
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
