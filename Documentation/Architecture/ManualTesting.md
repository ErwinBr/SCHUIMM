# Manual integration testing

EngineDemo is the per-platform integration test. Automated parts run with `--self-test`; the rest
needs a person at the machine.

## Automated: `EngineDemo --self-test [--screenshot out.ppm]`

Runs 30 frames, then checks, in order, and logs `PASS`/`FAIL` for each step:

1. **opengl-context**: core profile, version ≥ 3.3.
2. **scene-assets**: the scene loaded its textures (9, through the texture cache), its five
   render targets and two fonts (glyph atlas pages), with no missing glyphs. If an audio device
   is open, a chime is played. Without one, the log says audio was not exercised; that is a
   warning, not a failure. The pixel transition is switched on for the next steps.
3. **resize**: `setSize(960×540)`, the size is reported back and a resize event arrives.
4. **fullscreen-enter** and **fullscreen-leave**: state and event round-trip.
5. **vsync**: frame rate with the swap interval at 0 versus 1 (the transition is switched off
   again first). It passes if "off" is more than 20 % faster; if the driver or compositor forces
   sync, this is a warning, not a failure.
6. **scene-render**: the last frame drew more than 100 quads in at least one draw call and switched
   render targets at least four times (world and minimap targets).
7. Optional screenshot of the composited back buffer (PPM), then clean shutdown.

Exit code 0 means all steps passed. The screenshot is inspected for the scene:
* the meadow background and the cobblestone road (a repeating texture);
* trees, a rock and flowers with transparent edges and soft shadows;
* the player in front of the road;
* the help panel (sized to its text), the minimap with the camera frame and the status panel;
* the Western European sample line "Café crème, naïve Noël, Ñandú, Größe, garçon, €5", rendered
  with correct accents and kerning.

## Manual checklist

Start `EngineDemo` normally and check:

| # | Check | How |
|---|---|---|
| 1 | Window opens with the scene | as described above; log shows `Scene loaded` and `Audio device started` |
| 2 | OpenGL version | log line `OpenGL version: …`, `Context: x.y core profile` |
| 3 | Walking and camera | WASD/arrows move the player smoothly; the camera follows with easing, stops at the world edges, and the minimap frame follows; Shift runs |
| 4 | Click to walk | clicking a point (also after zooming, resizing or in fullscreen) walks the player exactly there |
| 5 | Layer order | walk above and below trees and houses: the player is hidden behind objects lower on screen and in front of higher ones; flowers sway (rotation around their base) |
| 6 | Zoom | Q/E and the wheel zoom smoothly between 0.75× and 3×, 0 resets; UI and minimap stay fixed while the world and the shop signs scale |
| 7 | Render targets | Tab: the world dissolves into big pixels and dims; Tab again returns; the UI stays sharp |
| 8 | Text | accents in the sample line and the signs ("Bäckerei «Zoë»", "Café & Crêperie"); F1 hides/shows the help |
| 9 | Audio | Space plays a chime (rapid presses overlap); footsteps while walking; N pauses/resumes the ambience; −/= change the volume shown in the status line |
| 10 | Modifiers left/right | LeftShift vs RightShift etc. logged separately with `--verbose` |
| 11 | Layout independence | with AZERTY active, the physical "Q" key still zooms out (the US position) |
| 12 | Resize | drag the border: content stays letterboxed and keeps animating during the drag (Windows/macOS) |
| 13 | Fullscreen | F11 / Alt+Enter (and Ctrl+Cmd+F on macOS) in and out; window position and size restored |
| 14 | VSync | press V: FPS jumps from the display rate to hundreds or thousands, and back |
| 15 | Minimise | log shows `Suspended`, CPU drops to ~0 %, the ambience stops; restore shows `Resumed`, the ambience continues, no animation jump |
| 16 | Focus | hold a walking key and Alt-Tab/Cmd-Tab away: the player stops; `Window lost focus` |
| 17 | Close | window close button, Alt+F4 (Windows), ⌘Q (macOS): log ends with `Audio device stopped`, `Clean shutdown, exit code 0` |
| 18 | HiDPI | on a scaled display the scene is sharp (nearest scaling) and the framebuffer size in the title is the pixel size |

## Verification record (Milestone 2)

Same machines as below: macOS 26.6 on Apple M4 Max (Apple Clang 21); Debian 13 GNOME/Xwayland on
Intel UHD 630 (GCC 14.2, Clang 19.1); Windows 11 on NVIDIA RTX 4050 Laptop GPU (MSVC 19.41).

| Check | macOS | Debian 13 | Windows 11 |
|---|---|---|---|
| Debug + Release build, zero warnings (`-Werror` / `/WX`) | ✅ | ✅ GCC and Clang | ✅ |
| `ctest -L unit`: CoreTests (62 cases), PlatformTests (2), FileSystemTests (13), ResourceTests (39), GraphicsTests (34), AudioTests (12) | ✅ Debug and Release | ✅ GCC and Clang, Debug and Release | ✅ Debug and Release |
| `ctest -L integration`: LifecycleTests (4), RenderingTests (9 pixel-checked cases), EngineDemoSmoke | ✅ Debug and Release | ✅ under Xvfb/llvmpipe (all four builds) | ✅ Debug and Release (desktop session via `schtasks /it`) |
| `ctest -L audio`: AudioDeviceSmoke on the real default device | ✅ CoreAudio, 44.1 kHz | ✅ PulseAudio on PipeWire, 48 kHz (all four builds) | ✅ WASAPI, 48 kHz |
| `EngineDemo --self-test` (Release) | ✅ GL 4.1 Metal | ✅ GL 4.6 Mesa on the GNOME session | ✅ GL 3.3 NVIDIA |
| Demo screenshot inspected | ✅ | ✅ | ✅ |
| Pixel transition screenshot inspected | ✅ (with a temporary local build that starts with the transition on, then reverted) | ☐ | ☐ |
| x86_64 build for 10.13 (`LC_VERSION_MIN_MACOSX 10.13`), unit tests and self-test under Rosetta | ✅ | n/a | n/a |
| AddressSanitizer + UndefinedBehaviorSanitizer: unit tests, RenderingTests, 120 demo frames | ✅ no reports | not run | not run |
| ThreadSanitizer: AudioTests (3 runs, incl. the null backend's audio thread) and AudioDeviceSmoke | ✅ no reports | not run | not run |
| GitHub Actions | ☐ not run yet (needs a push) | ☐ | ☐ |

Notes:

* Not tested on real 10.13 or Windows 7 machines (none available). The x86_64 macOS build was
  compiled with availability errors against 10.13 for every source, including the third-party
  libraries, and run under Rosetta on macOS 26.
* GCC's Release build (`-O3 -Wnull-dereference`) found a possible null dereference in the mixer's
  command handling that the other compilers did not report. The code was restructured so a voice
  command always has a valid voice.
* FreeType's basic GPOS kerning gave Noto Sans no kerning at all (see
  [Text.md](Text.md#fonts-fontface)); found by `FontFace_KerningPairs` and fixed by a marked local
  patch.
* Apple's OpenGL rejected draws while sampler units in the texture array had no texture bound
  (driver message in `RenderingTests`); unused slots are now bound to the white texture.
* Interactive play with keyboard and mouse (checklist 3–9 and 15–16) was not done: the machines
  were driven remotely. Movement, camera, zoom, layering, transition, text and audio were verified
  through the automated tests, the self-test and screenshots, but not by a person playing.

## Verification record (Milestone 1.5)

Machines: macOS 26.6 on Apple M4 Max (Apple Clang 21); Debian 13 GNOME/Xwayland on Intel UHD 630
(GCC 14.2, Clang 19.1); Windows 11 on NVIDIA RTX 4050 Laptop GPU (MSVC 19.41, VS 2022 Build
Tools). Each build was configured from an empty build directory.

| Check | macOS | Debian 13 | Windows 11 |
|---|---|---|---|
| Debug + Release build, zero warnings (`-Werror` / `/WX`) | ✅ | ✅ GCC and Clang | ✅ |
| `ctest -L unit`: CoreTests (54 cases), PlatformTests (2), FileSystemTests (13), ResourceTests (26) | ✅ | ✅ GCC and Clang, Debug and Release | ✅ Debug and Release |
| `ctest -L integration`: LifecycleTests (4 cases), EngineDemoSmoke | ✅ Debug and Release | ✅ under Xvfb (all four builds) and on the GNOME session | ✅ Debug and Release (desktop session via `schtasks /it`) |
| `EngineDemo --self-test` (Release) | ✅ GL 4.1 Metal | ✅ GL 4.6 Mesa | ✅ GL 3.3 NVIDIA |
| Clean shutdown, exit code 0 | ✅ also from fullscreen | ✅ | ✅ |
| No per-frame autorelease leak (`OBJC_DEBUG_MISSING_POOLS=YES`, 30 vs 600 frames) | ✅ count constant, none on the main thread | n/a | n/a |
| X protocol error is logged and not fatal (deliberate `XDestroyWindow` of a bogus id) | n/a | ✅ | n/a |
| `.github/scripts/run-tests.sh` | ✅ | ✅ including `xvfb-run` wrapper | ✅ under Git Bash |
| GitHub Actions: build + unit tests | ✅ hosted `macos-15` runner (integration: reported as not run) | ✅ GCC 14 and Clang 19 on `ubuntu-24.04`, Debug and Release, unit + integration under Xvfb | ✅ hosted `windows-2022` runner (integration: reported as not run) |

Notes:

* `LifecycleTests` also re-creates the OpenGL surface on the same window, which re-applies the
  pixel format on Windows. That works with the NVIDIA driver, so the suspected WGL problem was not
  reproduced and that code was left unchanged (see
  [KnownLimitations.md](KnownLimitations.md)).
* First CI run: Windows passed. On macOS the build and unit tests passed, but the hosted VM has
  no hardware-accelerated OpenGL, so the integration tests are now reported as not run there
  (like Windows). On Linux, configuration failed because the Ubuntu image lacks `libopengl-dev`
  (without it CMake creates no `OpenGL::GLX` target); the package was added to the workflow and
  to the documented package list.
* Second CI run: everything passed except the Clang 18 jobs. Ubuntu 24.04's default Clang 18
  cannot use `std::expected` from libstdc++ 14 (it requires `__cpp_concepts >= 202002`), so the
  job now uses Clang 19, which is also the version tested on Debian.
* Third CI run: all Windows and Linux jobs passed, including Clang 19. The macOS jobs were never
  started: GitHub reported that no hosted macOS arm64 runner could be acquired (a capacity
  problem on GitHub's side). After re-running them (macOS Debug needed two attempts to get a
  runner), all eight jobs of that run passed.
* Physical keyboard/mouse checks (checklist 3–7, 10, 11) were not repeated; Milestone 1.5 did not
  change input handling.

## Verification record (Milestone 1)

Machines: macOS 26.6 on Apple M4 Max; Debian 13 (GNOME on Wayland, X11 backend via Xwayland) on
Intel UHD 630; Windows 11 on NVIDIA RTX 4050 Laptop GPU (125 % scaling).

| Check | macOS | Debian 13 | Windows 11 |
|---|---|---|---|
| Debug + Release build, zero warnings | ✅ | ✅ GCC 14.2 and Clang 19 | ✅ MSVC 19.41 |
| `ctest` (51 unit tests, key map tests, smoke test) | ✅ | ✅ (smoke test under Xvfb) | ✅ |
| Window + context creation (logged version) | ✅ 4.1 Metal core | ✅ 4.6 Mesa core (Xvfb: 4.5 llvmpipe) | ✅ 3.3 NVIDIA core |
| Shader compilation, triangle, textured quad, alpha (screenshot) | ✅ | ✅ | ✅ |
| Self-test resize | ✅ | ✅ | ✅ |
| Self-test fullscreen enter/leave | ✅ | ✅ (mutter EWMH) | ✅ |
| Self-test VSync (off vs on FPS) | ✅ 3637 vs 104 | ✅ 2557 vs 61.5 | ✅ 2704 vs 146 |
| Clean shutdown (exit 0, resources released) | ✅ also via AppleEvent quit | ✅ | ✅ |
| Suspension while hidden | ✅ 0 % CPU while the screen was locked | unit-tested | unit-tested |
| Physical keyboard / mouse (checklist 3–6) | ◐ Space, Up, left button seen pressed/released; rest open | ☐ | ☐ |
| Live-resize redraw, minimise/restore, focus (7, 10, 11) | ☐ | ☐ | ☐ |

Notes:

* Physical input could not be exercised remotely: the machines were driven over ssh, and macOS
  desktop control lacked the Accessibility permission. The translation tables and the input state
  machine are unit-tested on every platform, but the checklist items marked ☐ still need a person
  at each machine.
* The macOS self-test was re-run after the occlusion change (hidden windows report as minimised)
  and passes. That change first left the demo stuck suspended at start-up, because AppKit reports a
  new window as occluded until it is on screen; this was found in a manual run and fixed by
  keeping `isMinimized()` and the events on one tracked state.
* Windows 7 and macOS 10.13 were not available; support there is by construction only
  (`_WIN32_WINNT=0x0601` with dynamically loaded newer APIs, `-mmacosx-version-min=10.13` with
  availability errors enabled).

## Running remotely

* **Windows over ssh** runs in a non-interactive session. To test on the visible desktop, launch
  through a scheduled task with `/it`, e.g. `schtasks /create /tn SchuimmDemo /tr C:\path\run-demo.cmd
  /sc once /st 23:59 /it /f` then `schtasks /run /tn SchuimmDemo`.
* **Linux over ssh**: `DISPLAY=:0 XAUTHORITY=/run/user/$UID/.mutter-Xwaylandauth.* EngineDemo --self-test`
  targets the logged-in GNOME session; `xvfb-run -a EngineDemo …` runs headless (no window
  manager, so fullscreen is unavailable and swap control is absent).
